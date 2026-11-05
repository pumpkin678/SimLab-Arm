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

from .assessment import SPEED_TOLERANCE, assess, checks


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
        error_peak = max(range(len(samples)), key=lambda index: abs(errors[index]))
        velocity_peak = max(range(len(samples)), key=lambda index: abs(velocities[index]))
        metrics = {"joint": joint + 1, "role": "active" if joint + 1 in active else "held",
                   "initial_position_error_rad": errors[0], "initial_velocity_radps": velocities[0],
                   "position_error_mean_rad": math.fsum(errors) / len(errors),
                   "position_error_rms_rad": math.sqrt(math.fsum(e * e for e in errors) / len(errors)),
                   "position_error_max_abs_rad": abs(errors[error_peak]),
                   "position_error_max_abs_time_s": samples[error_peak].time,
                   "velocity_peak_abs_radps": abs(velocities[velocity_peak]),
                   "velocity_peak_abs_time_s": samples[velocity_peak].time,
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
            metrics["tail_position_error_rms_rad"] = math.sqrt(math.fsum(e * e for e in tail) / len(tail)) if valid_tail else None
            metrics["tail_reason"] = "recorded" if valid_tail else ("incomplete_phase" if not complete else "phase_too_short")
            if joint + 1 in active:
                metrics.update(response_times(samples, joint, phase, timestep, complete))
            if joint + 1 in active and phase["kind"] == "response":
                # The first response starts from the measured initial pose; later responses
                # use the last input before the target change, not a regenerated reference.
                previous = rows[first - 1].target[joint] if first else samples[0].position[joint]
                change = samples[0].target[joint] - previous
                direction = 1 if change > 0 else -1 if change < 0 else 0
                metrics["response_direction"] = direction
                metrics["response_target_rad"] = samples[0].target[joint]
                peak = max(range(len(samples)), key=lambda index: -direction * errors[index])
                metrics["overshoot_rad"] = max(0.0, -direction * errors[peak]) if direction else None
                metrics["overshoot_time_s"] = samples[peak].time if metrics["overshoot_rad"] else None
                metrics["overshoot_reason"] = ("recorded" if complete else "partial_observation") if direction else "no_target_change"
        report["joints"].append(metrics)
    return report


def evaluate_speed(case, rows):
    """Use every trace boundary plus the final successful endpoint, including initialization."""
    limits = case.get("speed_limits_radps")
    if limits is not None:
        require(isinstance(limits, list) and len(limits) == 6 and
                all(finite(value) and value > 0 for value in limits), "Invalid joint speed limits")
    final = case.get("final_velocity_sample")
    if final is not None:
        require(isinstance(final, dict) and finite(final.get("time_s")) and final["time_s"] >= 0 and
                isinstance(final.get("velocity_radps"), list) and len(final["velocity_radps"]) == 6 and
                all(finite(value) for value in final["velocity_radps"]), "Invalid final velocity sample")
        expected = rows[-1].time + case["timestep_s"] if rows else 0
        require(abs(final["time_s"] - expected) <= TIME_TOLERANCE, "Final velocity time does not match recorded cycles")
    samples = [(row.time, row.velocity) for row in rows]
    if final is not None:
        samples.append((final["time_s"], final["velocity_radps"]))
    end = final["time_s"] if final is not None else (rows[-1].time + case["timestep_s"] if rows else 0)
    complete = case["status"] == "completed" and final is not None and limits is not None
    reason = "complete" if complete else ("speed_limits_missing" if limits is None else
             "final_velocity_missing" if final is None else "incomplete_case")
    report = {"status": "completed" if complete else "partial", "reason": reason,
              "samples": len(samples), "window_s": [0, end], "joints": []}
    for joint in range(6):
        peak = max(samples, key=lambda sample: abs(sample[1][joint]), default=None)
        limit = limits[joint] if limits is not None else None
        exceeded = [sample for sample in samples if limit is not None and abs(sample[1][joint]) > limit + SPEED_TOLERANCE]
        # Duration counts recorded step-start intervals. A final-only violation has zero
        # observed interval duration, but is still a failure; no dwell exemption applies.
        duration = sum(case["timestep_s"] for sample in exceeded if sample[0] < end - TIME_TOLERANCE)
        report["joints"].append({"joint": joint + 1,
            "speed_peak_abs_radps": abs(peak[1][joint]) if peak is not None else None,
            "speed_peak_abs_time_s": peak[0] if peak is not None else None,
            "first_overspeed_time_s": exceeded[0][0] if exceeded else None,
            "overspeed_samples": len(exceeded), "overspeed_duration_s": duration})
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
        limits = case.get("torque_limits_nm")
        if limits is not None:
            require(isinstance(limits, list) and len(limits) == 6, "Invalid torque limit metadata")
            for limit in limits:
                require(type(limit["limited"]) is bool and (not limit["limited"] or
                        (finite(limit["lower"]) and finite(limit["upper"]) and limit["lower"] <= limit["upper"])),
                        "Invalid torque limit metadata")
        rows = read_trace(directory, case)
        times = [row.time for row in rows]
        phases = [evaluate_phase(phase, rows, bisect_left(times, phase["start_s"]),
                                 bisect_left(times, phase["end_s"]), case["timestep_s"])
                  for phase in case["phases"]]
        case["evaluation"] = {"status": "completed" if case["status"] == "completed" else "partial",
                              "phases": phases, "speed": evaluate_speed(case, rows)}
        if not phases:
            case["evaluation"]["status"] = "not_applicable"
    result["evaluation"] = {
        "format_version": 1, "status": "completed" if result["status"] == "completed" else "partial",
        "criteria": {"position_band_rad": POSITION_BAND, "velocity_band_radps": VELOCITY_BAND,
                     "minimum_hold_s": MINIMUM_HOLD, "tail_duration_s": TAIL_DURATION,
                     "speed_tolerance_radps": SPEED_TOLERANCE}}
    assess(result)
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
    decision = result["evaluation"]["assessment"]
    lines = [f"[{decision['status']}] Teaching assessment v{decision['version']} | execution: {result['status']}",
             "Response band: 1 deg, 5 deg/s, held for 0.2 s; tail window: 0.5 s.",
             "Selected cases: " + ", ".join(decision["selected_cases"]),
             f"Teaching cases in this selection: {len(decision['teaching_cases'])}; this is not a claim about unselected cases."]
    for case in result["cases"]:
        case_decision = case["evaluation"]["assessment"]
        lines.append(f"[{case_decision['status']}] {case['id']} ({case['samples']} cycles)")
        if case_decision["missing_phases"]:
            lines.append("  Missing phases: " + ", ".join(case_decision["missing_phases"]))
        lines.extend("  " + reason for reason in case_decision["reasons"])
        for joint in case["evaluation"].get("speed", {}).get("joints", []):
            if joint["overspeed_samples"]:
                lines.append(f"  J{joint['joint']} overspeed: peak {math.degrees(joint['speed_peak_abs_radps']):.6g} deg/s; "
                             f"first {joint['first_overspeed_time_s']:.6g} s; "
                             f"recorded duration {joint['overspeed_duration_s']:.6g} s; {joint['overspeed_samples']} samples")
        warnings = [(phase["id"], [f"J{joint['joint']}" for joint in phase["joints"] if joint.get("held_start_in_band") is False])
                    for phase in case["evaluation"]["phases"]]
        warnings = [(phase, joints) for phase, joints in warnings if joints]
        if len(warnings) > 3:
            lines.append(f"  Held joints outside band at start in {len(warnings)} phases; see report for initial states.")
        else:
            lines.extend(f"  {phase}: held joints outside band at start: " + ",".join(joints) for phase, joints in warnings)
    failures = [check for check in checks(result) if check["status"] in {"FAIL", "INCOMPLETE"}]
    groups = [[check for check in failures if check["case_id"] == case["id"]] for case in result["cases"]]
    displayed = []
    for index in range(max(map(len, groups), default=0)):
        for group in groups:
            if index < len(group) and len(displayed) < 20:
                displayed.append(group[index])
    for check in displayed:
        factor = 180 / math.pi if check["unit"].startswith("rad") else 1
        unit = {"rad": "deg", "rad/s": "deg/s"}.get(check["unit"], check["unit"])
        actual = f"{check['value'] * factor:.6g}" if check["value"] is not None else "n/a"
        limit = f"{check['limit'] * factor:.6g}" if check["limit"] is not None else "n/a"
        if check["status"] == "FAIL" and check["value"] is not None and actual == limit:
            actual, limit = f"{check['value'] * factor:.17g}", f"{check['limit'] * factor:.17g}"
        lines.append(f"[{check['status']}] {check['case_id']}/{check['phase_id']}/J{check['joint']} "
                     f"{check['metric']}: {actual} {unit}; limit <= {limit} {unit}; {check['reason']}")
    if len(failures) > len(displayed):
        lines.append(f"Showing {len(displayed)} of {len(failures)} failed/incomplete checks; full details are in result.json and report.html.")
    return "\n".join(lines)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--run-directory", type=Path, required=True)
    args = parser.parse_args()
    try:
        from .report import write_report
        result = evaluate_run(args.run_directory)
        path = write_report(args.run_directory, result)
        message = summary(result) + f"\nReport: {path}"
        with (args.run_directory / "run.log").open("a", encoding="utf-8") as log:
            log.write(message + "\n")
        colors = {"PASS": "32", "FAIL": "31", "INCOMPLETE": "33", "OBSERVE": "90"}
        for line in message.splitlines():
            status = next((status for status in colors if line.startswith(f"[{status}]")), None)
            print(f"\033[{colors[status]}m{line}\033[0m" if status and sys.stdout.isatty() and not os.getenv("NO_COLOR") else line)
        return 1 if result["status"] == "failed" else 0
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
