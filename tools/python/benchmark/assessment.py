"""Versioned teaching rules. Display code consumes these decisions unchanged."""

import math


VERSION = 1
LIMITS = {
    "hold_position_peak_rad": math.radians(0.5),
    "hold_velocity_rms_radps": math.radians(1),
    "overshoot_rad": math.radians(2),
    "held_position_peak_rad": math.radians(1),
    "step_settling_s": 1.5,
    "tracking_error_rms_rad": math.radians(1),
    "tracking_error_peak_rad": math.radians(2),
    "recovery_settling_s": 0.5,
    "switch_settling_s": 2.0,
    "tail_error_rms_rad": math.radians(0.5),
}
SHORT_RESPONSES = {"brief_target", "resume_motion"}
TEACHING_CASES = {"posture_hold", "joint_step", "joint_tracking", "target_switch"}


def expected_phases(case_id):
    """Describe the phase interface, without repeating reference functions/times."""
    if case_id == "posture_hold":
        return {f"pose_{n}_{suffix}": (kind, None if suffix == "approach" else [])
                for n in range(1, 4) for suffix, kind in (("approach", "response"), ("hold", "hold"))}
    if case_id == "joint_step":
        return {"initialize": ("initialization", []), **{
            f"j{joint}_step_{step}": ("response", [joint]) for joint in range(1, 7) for step in range(1, 6)}}
    if case_id == "joint_tracking":
        return {"initialize": ("initialization", []), **{
            f"j{joint}_{suffix}": (kind, [joint]) for joint in range(1, 7)
            for suffix, kind in (("tracking", "tracking"), ("recovery", "recovery"))}}
    if case_id == "target_switch":
        return {"initialize": ("initialization", []), **{
            name: ("response", list(range(1, 7)) if name in {"multi_axis", "return_to_a"} else [4, 6])
            for name in ("multi_axis", "return_to_a", "brief_target", "withdraw", "resume_motion",
                         "reverse", "coupled_forward", "coupled_reverse", "recover")}}
    return {}


def aggregate(statuses, incomplete=False):
    statuses = set(statuses)
    if incomplete or "INCOMPLETE" in statuses:
        return "INCOMPLETE"
    if "FAIL" in statuses:
        return "FAIL"
    if "PASS" in statuses:
        return "PASS"
    return "OBSERVE"


def rules(case_id, phase, active):
    kind = phase["kind"]
    if kind == "initialization" or (case_id == "posture_hold" and kind != "hold"):
        return []
    if case_id == "posture_hold":
        return [("position_error_max_abs_rad", "hold_position_peak_rad"),
                ("velocity_rms_radps", "hold_velocity_rms_radps")]
    checks = []
    if not active:
        checks.append(("position_error_max_abs_rad", "held_position_peak_rad"))
    if case_id == "joint_tracking" and kind == "tracking":
        if active:
            checks += [("position_error_rms_rad", "tracking_error_rms_rad"),
                       ("position_error_max_abs_rad", "tracking_error_peak_rad")]
        return checks
    if active:
        if kind == "response":
            checks.append(("overshoot_rad", "overshoot_rad"))
        if case_id == "joint_step":
            checks.append(("settling_time_s", "step_settling_s"))
        elif case_id == "joint_tracking":
            checks.append(("settling_time_s", "recovery_settling_s"))
        elif phase["id"] not in SHORT_RESPONSES:
            checks.append(("settling_time_s", "switch_settling_s"))
    if case_id != "target_switch" or phase["id"] not in SHORT_RESPONSES:
        checks.append(("tail_position_error_rms_rad", "tail_error_rms_rad"))
    return checks


def make_check(case_id, phase, measured, joint, metric, limit_key):
    value = joint.get(metric)
    complete = measured["status"] == "completed"
    reason = "within_limit"
    if metric not in joint:
        status, reason = "INCOMPLETE", "metric_missing"
    elif value is None:
        if not complete:
            status, reason = "INCOMPLETE", "incomplete_phase"
        elif metric == "settling_time_s":
            status, reason = "FAIL", "band_not_held_until_end"
        elif metric == "overshoot_rad" and joint.get("overshoot_reason") == "no_target_change":
            status, reason = "OBSERVE", "no_target_change"
        else:
            status, reason = "INCOMPLETE", "metric_unavailable"
    elif value > LIMITS[limit_key]:
        status, reason = "FAIL", "limit_exceeded"
    elif not complete:
        status, reason = "INCOMPLETE", "incomplete_phase"
    else:
        status = "PASS"
    window = [phase["start_s"], phase["end_s"]]
    time = None
    if metric == "tail_position_error_rms_rad":
        window[0] = max(window[0], window[1] - 0.5)
    elif metric == "settling_time_s" and value is not None:
        time = phase["start_s"] + value
    else:
        time = joint.get({"position_error_max_abs_rad": "position_error_max_abs_time_s",
                          "velocity_rms_radps": "velocity_peak_abs_time_s",
                          "overshoot_rad": "overshoot_time_s"}.get(metric, ""))
    return {"case_id": case_id, "phase_id": phase["id"], "joint": joint["joint"],
            "metric": metric, "limit_key": limit_key, "value": value, "limit": LIMITS[limit_key],
            "unit": "s" if metric.endswith("_s") else "rad/s" if metric.endswith("_radps") else "rad",
            "status": status, "reason": reason, "window_s": window, "time_s": time}


def assess(result):
    for case in result["cases"]:
        expected = expected_phases(case["id"])
        measured_phases = {phase["id"]: phase for phase in case["evaluation"]["phases"]}
        reasons = []
        for phase in case["phases"]:
            measured = measured_phases[phase["id"]]
            for joint in measured["joints"]:
                joint["assessment"] = {"status": "OBSERVE", "checks": []}
            phase_status = "OBSERVE"
            reason = "observation_only"
            if expected:
                contract = expected.get(phase["id"])
                if contract is None or contract[0] != phase["kind"] or (
                        contract[1] is not None and contract[1] != phase["active_joints"]):
                    phase_status, reason = "INCOMPLETE", "unsupported_phase_layout"
                    reasons.append(phase["id"] + ": " + reason)
                else:
                    for joint in measured["joints"]:
                        checks = [make_check(case["id"], phase, measured, joint, metric, limit)
                                  for metric, limit in rules(case["id"], phase, joint["role"] == "active")]
                        joint["assessment"] = {"status": aggregate(check["status"] for check in checks), "checks": checks}
                    applicable = bool(rules(case["id"], phase, True) or rules(case["id"], phase, False))
                    phase_status = aggregate((joint["assessment"]["status"] for joint in measured["joints"]),
                                             incomplete=applicable and measured["status"] != "completed")
                    reason = "checks_evaluated" if applicable else "observation_only"
            measured["assessment"] = {"status": phase_status, "reason": reason}
        missing = sorted(set(expected) - set(measured_phases))
        case_status = aggregate((phase["assessment"]["status"] for phase in measured_phases.values()),
                                incomplete=bool(expected) and (bool(missing) or case["status"] != "completed"))
        case["evaluation"]["assessment"] = {"status": case_status, "missing_phases": missing, "reasons": reasons}
    result["evaluation"]["assessment"] = {
        "version": VERSION, "status": aggregate(
            (case["evaluation"]["assessment"]["status"] for case in result["cases"]),
            incomplete=result["status"] != "completed"),
        "selected_cases": [case["id"] for case in result["cases"]],
        "teaching_cases": [case["id"] for case in result["cases"] if case["id"] in TEACHING_CASES],
        "limits": dict(LIMITS)}


def checks(result):
    for case in result["cases"]:
        for phase in case["evaluation"]["phases"]:
            for joint in phase["joints"]:
                yield from joint["assessment"]["checks"]
