"""Evaluate recorded phases without regenerating references or running a controller."""

from __future__ import annotations

import argparse
from bisect import bisect_left
import csv
import json
import math
import os
from pathlib import Path
import sys
import tempfile
from typing import NamedTuple


HEADER = ["trace_version", "reset_index", "step_index", "simulation_time_s", "cycle_timestep_s"] + [
    f"{prefix}_j{joint}_{suffix}"
    for prefix, suffix in (("input", "position_rad"), ("feedback", "position_rad"),
                           ("feedback", "velocity_radps"), ("request", "torque_nm"), ("applied", "torque_nm"))
    for joint in range(1, 7)]
POSITION_BAND = math.radians(1)
VELOCITY_BAND = math.radians(5)
MINIMUM_HOLD = 0.2
TAIL_DURATION = 0.5
TIME_TOLERANCE = 1e-8  # Covers accumulated double time roundoff, not a missing physics cycle.
KINDS = {"initialization", "response", "hold", "tracking", "recovery"}


class Sample(NamedTuple):
    time: float
    target: tuple[float, ...]
    position: tuple[float, ...]
    velocity: tuple[float, ...]
    request: tuple[float, ...]
    applied: tuple[float, ...]


def require(condition, message):
    if not condition:
        raise ValueError(message)


def finite(value):
    return isinstance(value, (int, float)) and not isinstance(value, bool) and math.isfinite(value)


def validate_phases(case):
    phases = case["phases"]
    require(isinstance(phases, list), "Missing phase list")
    end = 0
    ids = set()
    for phase in phases:
        require(isinstance(phase["id"], str) and phase["id"] and phase["id"] not in ids, "Invalid phase ID")
        ids.add(phase["id"])
        require(finite(phase["start_s"]) and finite(phase["end_s"]) and
                phase["start_s"] == end < phase["end_s"] <= case["duration_s"], "Invalid phase timeline")
        require(phase["kind"] in KINDS, "Unknown phase kind")
        active = phase["active_joints"]
        require(isinstance(active, list) and all(type(j) is int and 1 <= j <= 6 for j in active) and
                len(set(active)) == len(active), "Invalid active joints")
        end = phase["end_s"]
    require(not phases or end == case["duration_s"], "Incomplete phase timeline")


def read_trace(directory, case):
    count = case["samples"]
    require(type(count) is int and 0 <= count < 2**64, "Invalid successful cycle count")
    name = case["trace"]
    require(isinstance(name, str) and name == case["id"] + ".csv" and
            "/" not in name and "\\" not in name, "Invalid trace filename")
    path = directory / name
    timestep = case["timestep_s"]
    expected = case["expected_steps"]
    require(case["last_step"] == (count - 1 if count else None), "Last step disagrees with sample count")
    if timestep is None:
        require(count == 0 and expected is None and case["status"] in {"not_started", "failed"},
                "Missing model timestep")
    else:
        require(finite(timestep) and timestep > 0 and type(expected) is int and
                expected == math.ceil(case["duration_s"] / timestep) and count <= expected,
                "Invalid timestep or cycle budget")
    if case["status"] == "completed":
        require(count == expected, "Completed case has incomplete cycle count")
    if not path.exists():
        require(count == 0 and timestep is None, f"Missing trace: {name}")
        return []
    samples = []
    with path.open(newline="", encoding="utf-8") as source:
        reader = csv.reader(source, strict=True)
        require(next(reader, None) == HEADER, f"Trace v1 needs its exact 35-column header: {name}")
        for index, row in enumerate(reader):
            require(len(row) == len(HEADER), f"Incomplete row in {name}:{index + 2}")
            require(row[:3] == ["1", "0", str(index)], f"Invalid version/reset/step in {name}:{index + 2}")
            values = tuple(float(value) for value in row[3:])
            require(all(math.isfinite(value) for value in values), f"Non-finite trace value: {name}")
            time, cycle_dt = values[:2]
            require(timestep is not None and abs(time - index * timestep) <= TIME_TOLERANCE and
                    (not samples or time > samples[-1].time), f"Invalid simulation time: {name}")
            require(math.isclose(cycle_dt, timestep, rel_tol=1e-6, abs_tol=0), f"Invalid control timestep: {name}")
            samples.append(Sample(time, *(values[start:start + 6] for start in range(2, 32, 6))))
    require(len(samples) == count, f"Trace rows disagree with successful cycles: {name}")
    return samples


def response_times(samples, joint, phase, timestep, complete):
    if not complete:
        return {"arrival_time_s": None, "arrival_reason": "incomplete_phase",
                "settling_time_s": None, "settling_reason": "incomplete_phase"}
    start = None
    arrival = None
    for row in samples:
        inside = (abs(row.target[joint] - row.position[joint]) <= POSITION_BAND and
                  abs(row.velocity[joint]) <= VELOCITY_BAND)
        if inside:
            if start is None:
                start = row.time
            # Each recorded pre-step state represents one model cycle.
            covered_end = min(row.time + timestep, phase["end_s"])
            if arrival is None and covered_end - start >= MINIMUM_HOLD - TIME_TOLERANCE:
                arrival = max(0.0, start - phase["start_s"])
        else:
            start = None
    settling = None
    if start is not None and phase["end_s"] - start >= MINIMUM_HOLD - TIME_TOLERANCE:
        settling = max(0.0, start - phase["start_s"])
    return {"arrival_time_s": arrival, "arrival_reason": "reached" if arrival is not None else "band_not_held",
            "settling_time_s": settling, "settling_reason": "settled" if settling is not None else "band_not_held_until_end"}


def evaluate_phase(phase, rows, first, last, timestep):
    samples = rows[first:last]
    start, end = phase["start_s"], phase["end_s"]
    complete = bool(samples) and samples[0].time <= start + timestep + TIME_TOLERANCE and \
        samples[-1].time + timestep >= end - TIME_TOLERANCE
    status = "completed" if complete else ("partial" if samples else "not_started")
    report = {"id": phase["id"], "status": status, "samples": len(samples), "joints": []}
    if phase["kind"] == "initialization":
        report["excluded"] = True
        return report
    if not samples:
        return report
    if phase["kind"] in {"response", "hold", "recovery"}:
        require(all(row.target == samples[0].target for row in samples), "Constant-target phase contains changing inputs")
    active = phase["active_joints"]
    for joint in range(6):
        errors = [row.target[joint] - row.position[joint] for row in samples]
        velocities = [row.velocity[joint] for row in samples]
        metrics = {"joint": joint + 1, "role": "active" if joint + 1 in active else "held",
                   "initial_position_error_rad": errors[0], "initial_velocity_radps": velocities[0],
                   "position_error_mean_rad": math.fsum(errors) / len(errors),
                   "position_error_rms_rad": math.sqrt(math.fsum(e * e for e in errors) / len(errors)),
                   "position_error_max_abs_rad": max(map(abs, errors)),
                   "velocity_peak_abs_radps": max(map(abs, velocities)),
                   "request_torque_peak_abs_nm": max(abs(row.request[joint]) for row in samples),
                   "applied_torque_peak_abs_nm": max(abs(row.applied[joint]) for row in samples)}
        if joint + 1 not in active:
            metrics["held_start_in_band"] = abs(errors[0]) <= POSITION_BAND and abs(velocities[0]) <= VELOCITY_BAND
        if phase["kind"] == "hold":
            metrics["velocity_rms_radps"] = math.sqrt(math.fsum(v * v for v in velocities) / len(velocities))
        if phase["kind"] in {"response", "recovery"}:
            tail = [row.target[joint] - row.position[joint] for row in samples if row.time >= end - TAIL_DURATION]
            valid_tail = complete and end - start >= TAIL_DURATION and bool(tail)
            metrics["tail_position_error_mean_rad"] = math.fsum(tail) / len(tail) if valid_tail else None
            metrics["tail_reason"] = "recorded" if valid_tail else ("incomplete_phase" if not complete else "phase_too_short")
            if joint + 1 in active:
                metrics.update(response_times(samples, joint, phase, timestep, complete))
            if joint + 1 in active and phase["kind"] == "response":
                # The first response starts from the measured initial pose; later responses
                # use the last input before the target change, not a regenerated reference.
                previous = rows[first - 1].target[joint] if first else samples[0].position[joint]
                change = samples[0].target[joint] - previous
                direction = 1 if change > 0 else -1 if change < 0 else 0
                metrics["overshoot_rad"] = max(0.0, max(-direction * error for error in errors)) if direction else None
                metrics["overshoot_reason"] = ("recorded" if complete else "partial_observation") if direction else "no_target_change"
        report["joints"].append(metrics)
    return report


def evaluate_run(directory):
    directory = Path(directory).resolve()
    path = directory / "result.json"
    def invalid_constant(value):
        raise ValueError(f"Non-finite JSON value: {value}")
    with path.open(encoding="utf-8") as source:
        result = json.load(source, parse_constant=invalid_constant)
    require(result["format_version"] == 1 and result["trace_version"] == 1, "Unsupported result or trace version")
    require(result["status"] in {"completed", "cancelled", "failed"}, "Run has not finalized")
    require(isinstance(result["cases"], list) and result["cases"], "Missing cases")
    ids = set()
    for case in result["cases"]:
        require(isinstance(case["id"], str) and case["id"] not in ids and case["id"], "Invalid case ID")
        ids.add(case["id"])
        require(case["status"] in {"completed", "cancelled", "failed", "not_started"}, "Case has not finalized")
        require(finite(case["duration_s"]) and case["duration_s"] > 0, "Invalid case duration")
        validate_phases(case)
        rows = read_trace(directory, case)
        times = [row.time for row in rows]
        phases = [evaluate_phase(phase, rows, bisect_left(times, phase["start_s"]),
                                 bisect_left(times, phase["end_s"]), case["timestep_s"])
                  for phase in case["phases"]]
        case["evaluation"] = {"status": "completed" if case["status"] == "completed" else "partial", "phases": phases}
        if not phases:
            case["evaluation"]["status"] = "not_applicable"
    result["evaluation"] = {
        "format_version": 1, "status": "completed" if result["status"] == "completed" else "partial",
        "criteria": {"position_band_rad": POSITION_BAND, "velocity_band_radps": VELOCITY_BAND,
                     "minimum_hold_s": MINIMUM_HOLD, "tail_duration_s": TAIL_DURATION}}
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(mode="w", encoding="utf-8", newline="\n", dir=directory,
                                         prefix=".evaluation-", suffix=".tmp", delete=False) as output:
            temporary = Path(output.name)
            json.dump(result, output, indent=2, ensure_ascii=False, allow_nan=False)
            output.write("\n")
            output.flush()
            os.fsync(output.fileno())
        os.replace(temporary, path)
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)
    return result


def summary(result):
    lines = [f"Evaluation {result['evaluation']['status']}: measured values only; no performance pass/fail.",
             "Response band: 1 deg, 5 deg/s, held for 0.2 s; tail window: 0.5 s."]
    for case in result["cases"]:
        for phase in case["evaluation"]["phases"]:
            label = f"{case['id']}/{phase['id']}: {phase['status']}"
            if phase.get("excluded"):
                lines.append(label + " (initialization excluded)")
                continue
            joints = phase["joints"]
            if not joints:
                lines.append(label + " (no samples)")
                continue
            peak_speed = max(j["velocity_peak_abs_radps"] for j in joints)
            for role in ("active", "held"):
                group = [joint for joint in joints if joint["role"] == role]
                if group:
                    worst = max(group, key=lambda joint: joint["position_error_max_abs_rad"])
                    label += f"; {role} max error J{worst['joint']} {math.degrees(worst['position_error_max_abs_rad']):.3f} deg"
            label += f"; peak speed {math.degrees(peak_speed):.3f} deg/s"
            inherited = [str(joint["joint"]) for joint in joints if joint.get("held_start_in_band") is False]
            if inherited:
                label += "; held joints outside band at start: " + ",".join("J" + joint for joint in inherited)
            for joint in joints:
                if "arrival_time_s" not in joint:
                    continue
                arrival, settling = joint["arrival_time_s"], joint["settling_time_s"]
                a = f"{arrival:.3f}s" if arrival is not None else "n/a"
                s = f"{settling:.3f}s" if settling is not None else "n/a"
                label += f"; J{joint['joint']} arrival/settling {a}/{s}"
            lines.append(label)
    return "\n".join(lines)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--run-directory", type=Path, required=True)
    args = parser.parse_args()
    try:
        message = summary(evaluate_run(args.run_directory))
        with (args.run_directory / "run.log").open("a", encoding="utf-8") as log:
            log.write(message + "\n")
        print(message)
        return 0
    except (OSError, ValueError, KeyError, TypeError, OverflowError, csv.Error) as error:
        message = f"Evaluation failed: {error}"
        try:
            with (args.run_directory / "run.log").open("a", encoding="utf-8") as log:
                log.write(message + "\n")
        except OSError:
            pass
        print(message, file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
