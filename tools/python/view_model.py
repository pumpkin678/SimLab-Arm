"""Inspect native MJCF qpos with MuJoCo's viewer, without stepping dynamics."""

from __future__ import annotations

import argparse
from pathlib import Path
import time

import mujoco
import mujoco.viewer
import numpy as np

ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--scene", type=Path, default=ROOT / "assets/description/scene.xml")
    parser.add_argument("--q-deg", type=float, nargs=6, default=[0] * 6,
                        metavar=("J1", "J2", "J3", "J4", "J5", "J6"))
    args = parser.parse_args()
    if not np.isfinite(args.q_deg).all():
        parser.error("Joint angles must be finite.")
    model = mujoco.MjModel.from_xml_path(str(args.scene.resolve()))
    if (model.nq, model.nv, model.nu) != (6, 6, 6):
        parser.error("Expected the six-axis SimLab-Arm model.")
    data = mujoco.MjData(model)
    data.qpos[:] = np.deg2rad(args.q_deg)
    mujoco.mj_forward(model, data)
    # The native viewer invokes callbacks on its own GUI thread. Queue only the reset
    # request here; apply it alongside qpos/forward under the viewer lock below.
    reset_requested = False

    def key_callback(key):
        nonlocal reset_requested
        if key == ord("R"):
            reset_requested = True

    print("Pose inspection only; simulation time stays at zero.")
    print("Press Space once to pause the native UI, then right panel > Joint: qpos in radians.")
    print("R: zero all joints. Close the window to exit. Space never starts dynamics in this tool.")
    print("CLI --q-deg accepts six initial joint angles in degrees.")
    with mujoco.viewer.launch_passive(model, data, key_callback=key_callback,
                                     show_left_ui=False, show_right_ui=True) as viewer:
        with viewer.lock():
            viewer.cam.lookat[:] = model.stat.center
            viewer.cam.distance = model.stat.extent * 1.45
            viewer.cam.azimuth = 315
            viewer.cam.elevation = -25
            viewer.opt.geomgroup[3] = 0  # Hidden collision geometry.
            viewer.opt.sitegroup[:] = 0
        while viewer.is_running():
            with viewer.lock():
                if reset_requested:
                    mujoco.mj_resetDataKeyframe(model, data, 0)
                    reset_requested = False
                # Also update poses after native Joint slider edits; never call mj_step.
                mujoco.mj_forward(model, data)
            viewer.sync()
            time.sleep(1 / 60)


if __name__ == "__main__":
    main()
