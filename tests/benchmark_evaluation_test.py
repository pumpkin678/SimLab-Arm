"""Analytical CSV fixtures for the maintained benchmark_contract check."""

import copy
import csv
import json
import math
import os
import io
from contextlib import redirect_stdout, redirect_stderr
from html.parser import HTMLParser
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

from tools.python.benchmark.evaluate import HEADER, evaluate_run, summary
from tools.python.benchmark import evaluate as evaluator
from tools.python.benchmark.assessment import LIMITS, aggregate, assess, checks, expected_phases, make_check
from tools.python.benchmark.report import reduce_curve, write_report


class EvaluationTest(unittest.TestCase):
    def setUp(self):
        self.root = Path(tempfile.mkdtemp(prefix="evaluation-", dir=os.environ["SIMLAB_BENCHMARK_TEST_OUTPUT"]))
        self.number = 0

    def fixture(self, *, kind="response", count=20, status="completed", phases=None, update=None):
        self.number += 1
        directory = self.root / f"run with spaces {self.number}"
        directory.mkdir()
        if phases is None:
            phases = [{"id": "measured", "start_s": 0, "end_s": 1,
                       "kind": kind, "active_joints": [1]}]
        case = {"id": "controlled", "duration_s": 1, "trace": "controlled.csv", "status": status,
                "reason": "fixture", "samples": count, "last_step": count - 1 if count else None,
                "timestep_s": 0.05, "expected_steps": 20, "parameters": {}, "phases": phases}
        result = {"format_version": 1, "trace_version": 1, "status": status, "cases": [case],
                  "preserved": {"model": "fixture", "configuration": "Release"}}
        self.save_result(directory, result)
        with (directory / "controlled.csv").open("w", newline="", encoding="utf-8") as output:
            writer = csv.writer(output)
            writer.writerow(HEADER)
            for index in range(count):
                row = {"target": [1, 0, 0, 0, 0, 0], "position": [0, 0, 0, 0, 0, 0],
                       "velocity": [0] * 6, "request": [2] * 6, "applied": [1.5] * 6}
                if update:
                    update(index, row)
                writer.writerow([1, 0, index, index * 0.05, 0.05] +
                                row["target"] + row["position"] + row["velocity"] + row["request"] + row["applied"])
        return directory

    def save_result(self, directory, result):
        (directory / "result.json").write_text(json.dumps(result), encoding="utf-8")

    def phase(self, result, index=0):
        return result["cases"][0]["evaluation"]["phases"][index]

    def test_hold_and_torque_metrics(self):
        def update(index, row):
            row["position"][0] = 0.9
            row["velocity"][0] = -0.04 if index % 2 else 0.04
            row["position"][1] = -0.03
        result = evaluate_run(self.fixture(kind="hold", update=update))
        joint = self.phase(result)["joints"][0]
        self.assertAlmostEqual(joint["position_error_mean_rad"], 0.1)
        self.assertAlmostEqual(joint["position_error_rms_rad"], 0.1)
        self.assertAlmostEqual(joint["velocity_rms_radps"], 0.04)
        self.assertEqual(joint["request_torque_peak_abs_nm"], 2)
        self.assertEqual(joint["applied_torque_peak_abs_nm"], 1.5)
        held = self.phase(result)["joints"][1]
        self.assertEqual(held["role"], "held")
        self.assertEqual(held["position_error_max_abs_rad"], 0.03)
        self.assertIn("deg/s", summary(result))

    def test_first_arrival_differs_from_final_settling(self):
        def update(index, row):
            row["position"][0] = 1 if 4 <= index < 8 or index >= 10 else 0
        joint = self.phase(evaluate_run(self.fixture(update=update)))["joints"][0]
        self.assertAlmostEqual(joint["arrival_time_s"], 0.2)
        self.assertAlmostEqual(joint["settling_time_s"], 0.5)
        self.assertEqual(joint["tail_position_error_mean_rad"], 0)

    def test_velocity_band_and_insufficient_dwell(self):
        def update(index, row):
            row["position"][0] = 1
            row["velocity"][0] = math.radians(6) if index < 18 else 0
        joint = self.phase(evaluate_run(self.fixture(update=update)))["joints"][0]
        self.assertIsNone(joint["arrival_time_s"])
        self.assertIsNone(joint["settling_time_s"])
        self.assertEqual(joint["arrival_reason"], "band_not_held")

    def test_positive_and_negative_overshoot(self):
        for sign in (1, -1):
            def update(index, row):
                row["target"][0] = sign
                row["position"][0] = 0 if index == 0 else sign * 1.1
            joint = self.phase(evaluate_run(self.fixture(update=update)))["joints"][0]
            self.assertAlmostEqual(joint["overshoot_rad"], 0.1)

    def test_tracking_does_not_have_step_metrics(self):
        def update(index, row):
            row["target"][0] = math.sin(index * 0.1)
            row["position"][0] = row["target"][0] - 0.02
        joint = self.phase(evaluate_run(self.fixture(kind="tracking", update=update)))["joints"][0]
        self.assertAlmostEqual(joint["position_error_rms_rad"], 0.02)
        self.assertNotIn("overshoot_rad", joint)
        self.assertNotIn("arrival_time_s", joint)

    def test_recovery_is_separate_from_tracking_and_has_no_step_overshoot(self):
        phases = [{"id": "tracking", "start_s": 0, "end_s": 0.5, "kind": "tracking", "active_joints": [1]},
                  {"id": "recovery", "start_s": 0.5, "end_s": 1, "kind": "recovery", "active_joints": [1]}]
        def update(index, row):
            row["position"][0] = 0.9 if index < 12 else 1
        result = evaluate_run(self.fixture(phases=phases, update=update))
        self.assertAlmostEqual(self.phase(result)["joints"][0]["position_error_rms_rad"], 0.1)
        recovery = self.phase(result, 1)["joints"][0]
        self.assertAlmostEqual(recovery["arrival_time_s"], 0.1)
        self.assertAlmostEqual(recovery["settling_time_s"], 0.1)
        self.assertNotIn("overshoot_rad", recovery)
        partial = evaluate_run(self.fixture(count=12, status="cancelled", phases=phases, update=update))
        recovery = self.phase(partial, 1)["joints"][0]
        self.assertIsNone(recovery["settling_time_s"])
        self.assertEqual(recovery["settling_reason"], "incomplete_phase")

    def test_held_initial_error_and_velocity_are_explained_without_hiding_errors(self):
        for mode in ("position", "velocity"):
            def update(index, row):
                row["position"][0] = 1
                if index == 0:
                    row[mode][1] = 0.2
            result = evaluate_run(self.fixture(kind="tracking", update=update))
            joint = self.phase(result)["joints"][1]
            self.assertFalse(joint["held_start_in_band"])
            self.assertAlmostEqual(joint["initial_position_error_rad"], -0.2 if mode == "position" else 0)
            self.assertAlmostEqual(joint["initial_velocity_radps"], 0.2 if mode == "velocity" else 0)
            self.assertIn("held joints outside band at start: J2", summary(result))
            self.assertTrue(self.phase(result)["joints"][2]["held_start_in_band"])

    def test_phase_boundary_and_short_tail(self):
        phases = [{"id": "brief", "start_s": 0, "end_s": 0.2, "kind": "response", "active_joints": [1]},
                  {"id": "after", "start_s": 0.2, "end_s": 1, "kind": "response", "active_joints": [1]}]
        def update(index, row):
            row["target"][0] = 1 if index < 4 else -1
            row["position"][0] = row["target"][0]
        result = evaluate_run(self.fixture(phases=phases, update=update))
        self.assertEqual(self.phase(result)["samples"], 4)
        self.assertEqual(self.phase(result, 1)["samples"], 16)
        self.assertIsNone(self.phase(result)["joints"][0]["tail_position_error_mean_rad"])
        self.assertEqual(self.phase(result)["joints"][0]["arrival_time_s"], 0)
        self.assertEqual(self.phase(result, 1)["joints"][0]["overshoot_rad"], 0)

    def test_partial_and_zero_sample_runs(self):
        phases = [{"id": "done", "start_s": 0, "end_s": 0.5, "kind": "response", "active_joints": [1]},
                  {"id": "partial", "start_s": 0.5, "end_s": 1, "kind": "response", "active_joints": [1]}]
        for status in ("cancelled", "failed"):
            directory = self.fixture(count=12, status=status, phases=phases)
            result = evaluate_run(directory)
            self.assertEqual(result["status"], status)
            self.assertEqual(self.phase(result)["status"], "completed")
            joint = self.phase(result, 1)["joints"][0]
            self.assertEqual(self.phase(result, 1)["status"], "partial")
            self.assertIsNone(joint["arrival_time_s"])
            self.assertIsNone(joint["settling_time_s"])
            self.assertIsNone(joint["tail_position_error_mean_rad"])
            self.assertEqual(joint["arrival_reason"], "incomplete_phase")
        result = evaluate_run(self.fixture(count=0, status="cancelled"))
        self.assertEqual(self.phase(result)["status"], "not_started")
        self.assertEqual(self.phase(result)["joints"], [])

    def test_not_started_and_model_load_failure_without_csv(self):
        for status in ("not_started", "failed"):
            directory = self.fixture(count=0, status="failed")
            result = json.loads((directory / "result.json").read_text())
            result["cases"][0].update(status=status, timestep_s=None, expected_steps=None)
            self.save_result(directory, result)
            (directory / "controlled.csv").unlink()
            evaluated = evaluate_run(directory)
            self.assertEqual(evaluated["cases"][0]["status"], status)

    def test_initialization_excluded_and_repeated_evaluation(self):
        directory = self.fixture(kind="initialization")
        original = json.loads((directory / "result.json").read_text())
        first = evaluate_run(directory)
        self.assertTrue(self.phase(first)["excluded"])
        self.assertEqual(self.phase(first)["joints"], [])
        self.assertEqual(evaluate_run(directory), first)
        del first["evaluation"]
        del first["cases"][0]["evaluation"]
        self.assertEqual(first, original)
        self.assertEqual(sorted(p.name for p in directory.iterdir()), ["controlled.csv", "result.json"])

    def test_corrupt_trace_preserves_result(self):
        def header(rows): rows[0][0] = "wrong"
        def version(rows): rows[1][0] = "2"
        def reset(rows): rows[1][1] = "1"
        def step(rows): rows[2][2] = "0"
        def time(rows): rows[2][3] = "0"
        def dt(rows): rows[1][4] = "0.01"
        def nonfinite(rows): rows[1][5] = "nan"
        def truncated(rows): rows.pop()
        def incomplete(rows): rows[1].pop()
        for corrupt in (header, version, reset, step, time, dt, nonfinite, truncated, incomplete):
            directory = self.fixture()
            path = directory / "controlled.csv"
            with path.open(newline="") as source:
                rows = list(csv.reader(source))
            corrupt(rows)
            with path.open("w", newline="") as output:
                csv.writer(output).writerows(rows)
            before = (directory / "result.json").read_bytes()
            with self.assertRaises(ValueError):
                evaluate_run(directory)
            self.assertEqual((directory / "result.json").read_bytes(), before)
            self.assertFalse(list(directory.glob("*.tmp")))

    def test_missing_trace_and_invalid_metadata(self):
        directory = self.fixture()
        (directory / "controlled.csv").unlink()
        before = (directory / "result.json").read_bytes()
        with self.assertRaises(ValueError):
            evaluate_run(directory)
        self.assertEqual((directory / "result.json").read_bytes(), before)
        directory = self.fixture()
        original = json.loads((directory / "result.json").read_text())
        for change in ({"samples": 19}, {"phases": [{"id": "bad", "start_s": 0.1, "end_s": 1,
                                                    "kind": "hold", "active_joints": []}]}):
            result = copy.deepcopy(original)
            result["cases"][0].update(change)
            self.save_result(directory, result)
            before = (directory / "result.json").read_bytes()
            with self.assertRaises(ValueError):
                evaluate_run(directory)
            self.assertEqual((directory / "result.json").read_bytes(), before)

    def test_atomic_replace_failure_preserves_result(self):
        directory = self.fixture()
        before = (directory / "result.json").read_bytes()
        with patch("tools.python.benchmark.evaluate.os.replace", side_effect=OSError("replacement denied")):
            with self.assertRaises(OSError):
                evaluate_run(directory)
        self.assertEqual((directory / "result.json").read_bytes(), before)
        self.assertFalse(list(directory.glob("*.tmp")))

    def passing_result(self, case_id):
        contracts = expected_phases(case_id) or {"observed": ("tracking", [1])}
        definitions, measurements = [], []
        for index, (name, (kind, active)) in enumerate(contracts.items()):
            active = active if active is not None else list(range(1, 7))
            definitions.append({"id": name, "start_s": index * 2, "end_s": index * 2 + 2,
                                "kind": kind, "active_joints": active})
            joints = [{"joint": j, "role": "active" if j in active else "held",
                       "position_error_max_abs_rad": math.radians(0.1), "position_error_rms_rad": math.radians(0.1),
                       "velocity_rms_radps": math.radians(0.1), "overshoot_rad": math.radians(0.2),
                       "settling_time_s": 0.25, "tail_position_error_rms_rad": math.radians(0.1),
                       "held_start_in_band": True} for j in range(1, 7)]
            measurements.append({"id": name, "status": "completed", "samples": 2000, "joints": joints})
        return {"status": "completed", "cases": [{"id": case_id, "status": "completed", "samples": len(contracts)*2000,
                "phases": definitions, "evaluation": {"phases": measurements}}], "evaluation": {}}

    def measured(self, result, phase_id, joint=1):
        return next(p for p in result["cases"][0]["evaluation"]["phases"] if p["id"] == phase_id)["joints"][joint-1]

    def test_all_rule_boundaries_are_inclusive(self):
        phase = {"id": "test", "start_s": 0, "end_s": 4}
        metric_for = {"hold_velocity_rms_radps": "velocity_rms_radps", "step_settling_s": "settling_time_s",
                      "recovery_settling_s": "settling_time_s", "switch_settling_s": "settling_time_s",
                      "overshoot_rad": "overshoot_rad", "tracking_error_rms_rad": "position_error_rms_rad",
                      "tail_error_rms_rad": "tail_position_error_rms_rad"}
        for key, limit in LIMITS.items():
            metric = metric_for.get(key, "position_error_max_abs_rad")
            joint = {"joint": 1, metric: limit}
            check = make_check("test", phase, {"status": "completed"}, joint, metric, key)
            self.assertEqual(check["status"], "PASS", key)
            joint[metric] = math.nextafter(limit, math.inf)
            self.assertEqual(make_check("test", phase, {"status": "completed"}, joint, metric, key)["status"], "FAIL", key)

    def test_real_teaching_profiles_and_selected_scope(self):
        cases = []
        for name in ("posture_hold", "joint_step", "joint_tracking", "target_switch"):
            result = self.passing_result(name)
            assess(result)
            self.assertEqual(result["evaluation"]["assessment"]["status"], "PASS", name)
            self.assertEqual(result["evaluation"]["assessment"]["selected_cases"], [name])
            self.assertTrue(list(checks(result)))
            cases += result["cases"]
        combined = {"status": "completed", "cases": cases, "evaluation": {}}
        assess(combined)
        self.assertEqual(combined["evaluation"]["assessment"]["status"], "PASS")
        for name in ("joint_sine", "joint_tracking_stress", "custom_case"):
            result = self.passing_result(name)
            self.measured(result, "observed")["position_error_max_abs_rad"] = 100
            assess(result)
            self.assertEqual(result["evaluation"]["assessment"]["status"], "OBSERVE")
            self.assertFalse(list(checks(result)))

    def test_tail_rms_does_not_hide_opposite_errors(self):
        def update(index, row):
            row["position"][0] = 1 + math.radians(1) * (-1 if index % 2 else 1)
        joint = self.phase(evaluate_run(self.fixture(update=update)))["joints"][0]
        self.assertAlmostEqual(joint["tail_position_error_mean_rad"], 0)
        self.assertAlmostEqual(joint["tail_position_error_rms_rad"], math.radians(1))
        phase = {"id": "test", "start_s": 0, "end_s": 1}
        self.assertEqual(make_check("test", phase, {"status": "completed"}, joint,
                                   "tail_position_error_rms_rad", "tail_error_rms_rad")["status"], "FAIL")

    def test_missing_settling_partial_and_legacy_layout(self):
        result = self.passing_result("joint_step")
        joint = self.measured(result, "j1_step_1")
        joint["settling_time_s"] = None
        assess(result)
        self.assertEqual(result["evaluation"]["assessment"]["status"], "FAIL")
        result["cases"][0]["status"] = result["status"] = "cancelled"
        measured = next(p for p in result["cases"][0]["evaluation"]["phases"] if p["id"] == "j1_step_1")
        measured["status"] = "partial"
        joint["overshoot_rad"] = math.radians(3)
        assess(result)
        self.assertEqual(result["evaluation"]["assessment"]["status"], "INCOMPLETE")
        self.assertTrue(any(c["status"] == "FAIL" for c in checks(result)))
        self.assertEqual(aggregate(["OBSERVE", "PASS", "FAIL", "INCOMPLETE"]), "INCOMPLETE")
        legacy = self.passing_result("joint_tracking")
        legacy["cases"][0]["phases"][1]["id"] = "j1_fast"
        legacy["cases"][0]["evaluation"]["phases"][1]["id"] = "j1_fast"
        assess(legacy)
        self.assertEqual(legacy["evaluation"]["assessment"]["status"], "INCOMPLETE")
        self.assertIn("j1_tracking", legacy["cases"][0]["evaluation"]["assessment"]["missing_phases"])

    def test_terminal_summary_keeps_failure_output_bounded(self):
        result = self.passing_result("joint_step")
        for phase in result["cases"][0]["evaluation"]["phases"]:
            for joint in phase["joints"]:
                joint["position_error_max_abs_rad"] = joint["tail_position_error_rms_rad"] = 100
                joint["settling_time_s"] = None
        assess(result)
        message = summary(result)
        self.assertEqual(result["evaluation"]["assessment"]["status"], "FAIL")
        self.assertLessEqual(sum(line.startswith("[FAIL] joint_step/") for line in message.splitlines()), 20)
        self.assertIn("full details are in result.json and report.html", message)

    def test_short_responses_do_not_require_arrival_or_steady_tail(self):
        result = self.passing_result("target_switch")
        for phase in ("brief_target", "resume_motion"):
            for joint in (4, 6):
                entry = self.measured(result, phase, joint)
                entry["settling_time_s"] = None
                entry["tail_position_error_rms_rad"] = 100
        assess(result)
        self.assertEqual(result["evaluation"]["assessment"]["status"], "PASS")
        self.measured(result, "withdraw", 3)["position_error_max_abs_rad"] = math.radians(1.01)
        self.measured(result, "withdraw", 3)["held_start_in_band"] = False
        assess(result)
        self.assertEqual(result["evaluation"]["assessment"]["status"], "FAIL")

    def test_downsampling_preserves_channel_extrema_and_decision_locations(self):
        rows = []
        for index in range(3200):
            values = [0.0] * 30
            values[0] = math.sin(index * 0.02)
            values[6] = math.cos(index * 0.03)
            values[12] = 123.5 if index == 517 else 0
            values[18] = -8.25 if index == 1003 else 0
            values[24] = 9.75 if index == 2200 else 0
            rows.append((index * 0.001, index, tuple(values)))
        reduced = reduce_curve(rows, 0, 0, 3.2, [0.617])
        self.assertTrue(reduced["downsampled"])
        self.assertEqual(reduced["original_samples"], len(rows))
        kept = reduced["rows"]
        self.assertEqual([kept[0][7], kept[-1][7]], [0, 3199])
        self.assertIn(617, [row[7] for row in kept])
        self.assertEqual(max(row[4] for row in kept), math.degrees(123.5))
        self.assertEqual(min(row[5] for row in kept), -8.25)
        self.assertEqual(max(row[6] for row in kept), 9.75)
        error = [(row[2][0] - row[2][6]) * 180 / math.pi for row in rows]
        self.assertAlmostEqual(max(row[3] for row in kept), max(error), places=12)
        self.assertAlmostEqual(min(row[3] for row in kept), min(error), places=12)
        self.assertEqual([row[7] for row in kept], sorted(set(row[7] for row in kept)))

    def test_self_contained_report_escaping_empty_and_repeat(self):
        class Parser(HTMLParser):
            def __init__(self): super().__init__(); self.tags = []
            def handle_starttag(self, tag, attrs): self.tags.append((tag, dict(attrs)))
        for count, status in ((20, "completed"), (0, "cancelled")):
            directory = self.fixture(count=count, status=status)
            result = evaluate_run(directory)
            result["model"] = '</script><script>alert("injected")</script>& model $app'
            original_csv = (directory / "controlled.csv").read_bytes()
            original = copy.deepcopy(result)
            path = write_report(directory, result)
            document = path.read_text(encoding="utf-8")
            self.assertNotIn(result["model"], document)
            parser = Parser(); parser.feed(document)
            self.assertEqual(len([tag for tag, attrs in parser.tags if tag == "script"]), 3)
            self.assertFalse(any((tag == "script" and "src" in attrs) or (tag == "link" and "href" in attrs) for tag, attrs in parser.tags))
            payload = document.split('<script id="report-data" type="application/json">', 1)[1].split('</script>', 1)[0]
            embedded = json.loads(payload)
            self.assertEqual(embedded["result"], result)
            self.assertEqual(embedded["series"][0][0][0]["original_samples"], count)
            self.assertEqual(bool(embedded["series"][0][0][0]["rows"]), bool(count))
            self.assertIn("plotly_relayout", document)
            self.assertIn("hovertemplate", document)
            self.assertEqual(write_report(directory, result), path)
            self.assertEqual(result, original)
            self.assertEqual((directory / "controlled.csv").read_bytes(), original_csv)
            self.assertFalse(list(directory.glob("*.tmp")))

    def test_report_replace_failure_and_cli_exit_contract(self):
        directory = self.fixture()
        result = evaluate_run(directory)
        path = directory / "report.html"; path.write_text("old report")
        with patch("tools.python.benchmark.report.os.replace", side_effect=OSError("replacement denied")):
            with self.assertRaises(OSError): write_report(directory, result)
        self.assertEqual(path.read_text(), "old report")
        self.assertFalse(list(directory.glob("*.tmp")))
        for verdict, execution in (("PASS", "completed"), ("FAIL", "completed"), ("OBSERVE", "completed"), ("INCOMPLETE", "cancelled")):
            result["status"] = execution
            result["evaluation"]["assessment"]["status"] = verdict
            with patch.object(evaluator, "evaluate_run", return_value=result), \
                 patch("tools.python.benchmark.report.write_report", return_value=path), \
                 patch("sys.argv", ["evaluate", "--run-directory", str(directory)]), redirect_stdout(io.StringIO()):
                self.assertEqual(evaluator.main(), 0)
        before = (directory / "controlled.csv").read_bytes()
        with patch("tools.python.benchmark.report.write_report", side_effect=OSError("report failed")), \
             patch("sys.argv", ["evaluate", "--run-directory", str(directory)]), redirect_stdout(io.StringIO()), redirect_stderr(io.StringIO()):
            self.assertEqual(evaluator.main(), 1)
        self.assertEqual((directory / "controlled.csv").read_bytes(), before)
        self.assertEqual(json.loads((directory / "result.json").read_text())["status"], "completed")


if __name__ == "__main__":
    unittest.main()
