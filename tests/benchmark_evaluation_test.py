"""Analytical CSV fixtures for the maintained benchmark_contract check."""

import copy
import csv
import json
import math
import os
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

from tools.python.benchmark.evaluate import HEADER, evaluate_run, summary


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


if __name__ == "__main__":
    unittest.main()
