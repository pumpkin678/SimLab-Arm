"""Build a self-contained offline report from assessed results and recorded curves."""

from bisect import bisect_left
import csv
from datetime import datetime, timezone
import json
import html
import math
import os
from pathlib import Path
from string import Template
import tempfile


HERE = Path(__file__).resolve().parent
PLOTLY = HERE.parents[2] / "third_party/plotly/plotly-basic-3.1.0.min.js"
BUCKETS = 128


def check_assets():
    for path in (PLOTLY, PLOTLY.with_name("LICENSE.MIT"), HERE / "report_template.html", HERE / "report.js"):
        if not path.is_file():
            raise FileNotFoundError(f"Missing offline report asset: {path}")
    with PLOTLY.open(encoding="utf-8") as source:
        if "v3.1.0" not in source.read(256):
            raise ValueError("The offline report requires Plotly.js basic 3.1.0")


def plotting_rows(directory, case):
    if not case["samples"]:
        return []
    # Evaluation has validated this CSV. This pass only extracts chart channels.
    rows = []
    with (directory / case["trace"]).open(newline="", encoding="utf-8") as source:
        reader = csv.reader(source)
        next(reader)
        for index, row in enumerate(reader):
            values = tuple(map(float, row[5:]))
            rows.append((float(row[3]), index, values))
    return rows


def reduce_curve(rows, joint, start, end, preserve_times=()):
    """Retain real samples, every channel's extrema, and decision locations."""
    degrees = 180 / math.pi
    curve = []
    for time, step, values in rows:
        target, position = values[joint], values[joint + 6]
        curve.append([time, target * degrees, position * degrees, (target - position) * degrees,
                      values[joint + 12] * degrees, values[joint + 18], values[joint + 24], step])
    if len(curve) <= 512:
        return {"rows": curve, "original_samples": len(curve), "downsampled": False}
    keep = {0, len(curve) - 1}
    buckets = {}
    for index, row in enumerate(curve):
        bucket = min(BUCKETS - 1, max(0, int((row[0] - start) / (end - start) * BUCKETS)))
        buckets.setdefault(bucket, []).append(index)
    for indices in buckets.values():
        keep.update((indices[0], indices[-1]))
        for channel in range(1, 7):
            keep.add(min(indices, key=lambda index: curve[index][channel]))
            keep.add(max(indices, key=lambda index: curve[index][channel]))
    times = [row[0] for row in curve]
    for time in preserve_times:
        if time is None:
            continue
        index = min(len(curve) - 1, bisect_left(times, time))
        keep.add(index)
        if index:
            keep.add(index - 1)
    return {"rows": [curve[index] for index in sorted(keep)], "original_samples": len(curve),
            "downsampled": len(keep) < len(curve)}


def build_data(directory, result):
    series = []
    for case in result["cases"]:
        rows = plotting_rows(directory, case)
        times = [row[0] for row in rows]
        case_series = []
        for phase, measured in zip(case["phases"], case["evaluation"]["phases"]):
            first, last = bisect_left(times, phase["start_s"]), bisect_left(times, phase["end_s"])
            phase_rows = rows[first:last]
            phase_series = []
            for joint in range(6):
                preserve = [phase["start_s"], phase["end_s"]]
                speeds = case["evaluation"].get("speed", {}).get("joints", [])
                if speeds:
                    preserve += [speeds[joint]["speed_peak_abs_time_s"], speeds[joint]["first_overspeed_time_s"]]
                if measured["joints"]:
                    metrics = measured["joints"][joint]
                    preserve += [phase["start_s"] + metrics[key] for key in ("arrival_time_s", "settling_time_s")
                                 if metrics.get(key) is not None]
                    preserve += [metrics[key] for key in ("position_error_max_abs_time_s", "velocity_peak_abs_time_s", "overshoot_time_s")
                                 if metrics.get(key) is not None]
                    for check in metrics["assessment"]["checks"]:
                        preserve += [check["time_s"], *check["window_s"]]
                phase_series.append(reduce_curve(phase_rows, joint, phase["start_s"], phase["end_s"], preserve))
            case_series.append(phase_series)
        series.append(case_series)
    return {"result": result, "series": series,
            "generated_at": datetime.now(timezone.utc).isoformat(), "plotly_version": "3.1.0"}


def write_report(directory, result):
    check_assets()
    directory = Path(directory).resolve()
    data = json.dumps(build_data(directory, result), ensure_ascii=False, allow_nan=False, separators=(",", ":"))
    # The JSON script is inert, but HTML parsers still recognize closing tags.
    for literal, escaped in (("<", "\\u003c"), (">", "\\u003e"), ("&", "\\u0026"),
                             ("\u2028", "\\u2028"), ("\u2029", "\\u2029")):
        data = data.replace(literal, escaped)
    plotly = PLOTLY.read_text(encoding="utf-8").replace("</script", "<\\/script")
    document = Template((HERE / "report_template.html").read_text(encoding="utf-8")).substitute(
        plotly=plotly, data=data, app=(HERE / "report.js").read_text(encoding="utf-8"),
        license=html.escape(PLOTLY.with_name("LICENSE.MIT").read_text(encoding="utf-8")))
    temporary = None
    path = directory / "report.html"
    try:
        with tempfile.NamedTemporaryFile(mode="w", encoding="utf-8", newline="\n", dir=directory,
                                         prefix=".report-", suffix=".tmp", delete=False) as output:
            temporary = Path(output.name)
            output.write(document)
            output.flush()
            os.fsync(output.fileno())
        os.replace(temporary, path)
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)
    return path
