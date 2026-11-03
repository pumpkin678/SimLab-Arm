"""Check the maintained MJCF against CAD, without a window or runtime changes."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import xml.etree.ElementTree as ET

import mujoco
import numpy as np

from .import_cad import (
    JOINT_AXES, JOINT_RANGES, TORQUE_LIMITS, ROOT, WORLD_ROTATION, mesh_vertices, pose, read_stl,
    source_inertia, source_model,
)

SIGNS = np.array([1, 1, 1, -1, -1, 1])


def require(condition, message):
    if not condition:
        raise AssertionError(message)


def close(actual, expected, label, tolerance=1e-10):
    error = float(np.max(np.abs(np.asarray(actual) - np.asarray(expected))))
    require(error <= tolerance, f"{label}: max error {error:g} > {tolerance:g}")
    return error


def rotation(axis, angle):
    axis = np.asarray(axis, dtype=float)
    axis /= np.linalg.norm(axis)
    x, y, z = axis
    cross = np.array([[0, -z, y], [z, 0, -x], [-y, x, 0]])
    return np.eye(3) + np.sin(angle) * cross + (1 - np.cos(angle)) * cross @ cross


def source_fk(joints, q):
    transforms = {"base_link": np.eye(4)}
    for joint in joints:
        transform = pose(joint.find("origin"))
        if joint.get("type") == "revolute":
            index = int(joint.get("name").removeprefix("joint")) - 1
            axis = np.fromstring(joint.find("axis").get("xyz"), sep=" ")
            transform[:3, :3] = transform[:3, :3] @ rotation(axis, q[index])
        transforms[joint.find("child").get("link")] = (
            transforms[joint.find("parent").get("link")] @ transform)
    return transforms


def contact_records(model, data):
    return [{"geoms": [model.geom(c.geom1).name, model.geom(c.geom2).name],
             "distance_m": float(c.dist), "position_m": c.pos.tolist(),
             "normal_geom1_to_geom2": c.frame[:3].tolist()}
            for c in data.contact]


def check(scene, source):
    require(mujoco.__version__ == "3.9.0", "Use the pinned MuJoCo 3.9.0 environment.")
    model = mujoco.MjModel.from_xml_path(str(scene))
    arm = mujoco.MjModel.from_xml_path(str(scene.with_name("arm.xml")))
    for item in (model, arm):
        require((item.nq, item.nv, item.nu, item.nbody) == (6, 6, 6, 8),
                "Expected seven physical links, six hinge DOFs and six motors.")
    data = mujoco.MjData(model)
    mujoco.mj_forward(model, data)
    links, joints, zero, ordered = source_model(source)
    xml_model = ET.parse(scene.with_name("arm.xml")).getroot()
    xml_bodies = {body.get("name"): body for body in xml_model.iter("body")}
    excluded_pairs = {frozenset(("base_link", "link1")), frozenset(("base_link", "link2"))}
    require({frozenset((item.get("body1"), item.get("body2")))
             for item in xml_model.findall("contact/exclude")} == excluded_pairs,
            "Unexpected collision exclusions")
    physical = [name for name in ordered if name != "tool0"]
    close(model.opt.timestep, .001, "timestep")
    require(model.opt.integrator == mujoco.mjtIntegrator.mjINT_IMPLICITFAST, "integrator")
    close(model.opt.gravity, [0, 0, -9.81], "gravity")
    close(model.dof_damping, 0, "damping")
    close(model.dof_frictionloss, 0, "friction")
    close(model.dof_armature, 0, "armature")
    require(model.jnt_limited.all(), "All six joint limits must be enabled")
    close(model.jnt_range, JOINT_RANGES, "joint ranges")
    require(not model.jnt_actfrclimited.any(), "joint torque limits")
    require(not model.actuator_ctrllimited.any() and model.actuator_forcelimited.all(),
            "actuator force limits")
    close(model.actuator_forcerange, np.column_stack([-TORQUE_LIMITS, TORQUE_LIMITS]), "torque ranges", 0)
    close(model.body_quat[1:], np.tile([1, 0, 0, 0], (7, 1)), "zero body frames")

    masses, inertias, hashes, geometry = {}, {}, {}, {}
    vertex_error = normal_error = inertia_error = 0.0
    samples = {}
    mesh_root = scene.parent.parent / "meshes" / "mjcf"
    for name in physical:
        body = model.body(name).id
        align = WORLD_ROTATION @ zero[name][:3, :3]
        mass, com, tensor = source_inertia(links[name])
        close(model.body_mass[body], mass, f"{name} mass")
        close(model.body_ipos[body], align @ com, f"{name} COM")
        inertial_rotation = np.zeros(9)
        mujoco.mju_quat2Mat(inertial_rotation, model.body_iquat[body])
        inertial_rotation = inertial_rotation.reshape(3, 3)
        compiled = inertial_rotation @ np.diag(model.body_inertia[body]) @ inertial_rotation.T
        expected = align @ tensor @ align.T
        xml_inertia = np.fromstring(xml_bodies[name].find("inertial").get("fullinertia"), sep=" ")
        close(xml_inertia, expected[[0, 1, 2, 0, 0, 1], [0, 1, 2, 1, 2, 2]], "XML full tensor", 1e-13)
        # MuJoCo's fullinertia eigensolver has a finite stopping tolerance. Verify the
        # source tensor exactly above and its compiled reconstruction to 1 ppm here.
        inertia_error = max(inertia_error, close(compiled, expected, f"{name} compiled inertia",
                                                 1e-6 * np.linalg.norm(expected)))
        eigenvalues = np.linalg.eigvalsh(expected)
        require(eigenvalues.min() > 0 and eigenvalues[2] <= eigenvalues[:2].sum(),
                f"Invalid physical inertia: {name}")
        masses[name] = mass
        inertias[name] = {"com_m": (align @ com).tolist(), "tensor_kg_m2": expected.tolist(),
                          "principal_moments_kg_m2": eigenvalues.tolist()}
        for kind in ("visual", "collision"):
            item = links[name].find(kind)
            raw_path = source.parent / item.find("geometry/mesh").get("filename")
            _, original, vertices = mesh_vertices(item, source.parent)
            _, baked = read_stl(mesh_root / kind / f"{name}.stl")
            require(len(baked) == len(original), f"{name} {kind}: changed triangle count")
            close(baked["attribute"], original["attribute"], "STL attributes", 0)
            expected_vertices = vertices @ align.T
            vertex_error = max(vertex_error, close(baked["vertex"], expected_vertices,
                                                   f"{name} {kind} vertices", 1e-6))
            edges = baked["vertex"].astype(float)
            normals = np.cross(edges[:, 1] - edges[:, 0], edges[:, 2] - edges[:, 0])
            length = np.linalg.norm(normals, axis=1)
            valid = length > 0
            normals[valid] /= length[valid, None]
            normal_error = max(normal_error, close(baked["normal"], normals, "STL normals", 1e-6))
            hashes[str(raw_path.relative_to(ROOT))] = hashlib.sha256(raw_path.read_bytes()).hexdigest()
            key = f"{kind}_{name}"
            geometry[key] = {"triangles": len(baked)}
            # Distributed samples include every original triangle in the zero-pose comparison above.
            indices = np.linspace(0, len(baked) * 3 - 1, 51, dtype=int)
            samples[key] = (name, vertices.reshape(-1, 3)[indices],
                            baked["vertex"].reshape(-1, 3)[indices].astype(float))
            geom = model.geom(key).id
            require(model.geom_bodyid[geom] == body and model.geom_group[geom] == (1 if kind == "visual" else 3),
                    f"{key}: wrong link/group")
            close(model.geom_contype[geom], 0 if kind == "visual" else 2, f"{key} contype", 0)
            close(model.geom_conaffinity[geom], 0 if kind == "visual" else 3, f"{key} affinity", 0)
            # Compiled meshes are recentered/reoriented internally by MuJoCo. Check their world
            # geometry too, rather than treating the compiler's mesh frame as the body frame.
            mesh = int(model.geom_dataid[geom])
            start, count = model.mesh_vertadr[mesh], model.mesh_vertnum[mesh]
            compiled_vertices = model.mesh_vert[start:start + count]
            world_vertices = compiled_vertices @ data.geom_xmat[geom].reshape(3, 3).T + data.geom_xpos[geom]
            expected_world = expected_vertices.reshape(-1, 3) + WORLD_ROTATION @ zero[name][:3, 3]
            close(world_vertices.min(axis=0), expected_world.min(axis=0), f"{key} compiled min", 1e-6)
            close(world_vertices.max(axis=0), expected_world.max(axis=0), f"{key} compiled max", 1e-6)

    for i in range(6):
        joint = model.joint(f"joint{i + 1}").id
        motor = model.actuator(f"motor{i + 1}").id
        require(joint == motor == i and model.jnt_qposadr[joint] == i and model.jnt_dofadr[joint] == i,
                "Unexpected joint/motor ordering")
        require(model.jnt_type[joint] == mujoco.mjtJoint.mjJNT_HINGE, "Expected hinges")
        close(model.jnt_axis[joint], JOINT_AXES[i], f"J{i + 1} axis", 0)
        require(model.actuator_trnid[motor, 0] == joint, "Motor transmission")
        require(model.actuator_trntype[motor] == mujoco.mjtTrn.mjTRN_JOINT, "Joint transmission")
        close(model.actuator_gear[motor], [1, 0, 0, 0, 0, 0], "motor gear", 0)
        close(model.actuator_gainprm[motor], [1] + [0] * 9, "motor gain", 0)
        close(model.actuator_biasprm[motor], 0, "motor bias", 0)

    rng = np.random.default_rng(20261102)
    poses = [np.zeros(6), *(.5 * np.pi * np.eye(6)),
             np.deg2rad([20, -35, 60, 40, -25, 75]),
             np.deg2rad([-45, 30, -50, -80, 45, -120]), *rng.uniform(-1.4, 1.4, (20, 6)), np.zeros(6)]
    position_error = rotation_error = world_error = 0.0
    for q in poses:
        data.qpos[:] = q
        mujoco.mj_forward(model, data)
        reference = source_fk(joints, q * SIGNS)
        for name in physical:
            body = model.body(name).id
            position = WORLD_ROTATION @ reference[name][:3, 3]
            orientation = (WORLD_ROTATION @ reference[name][:3, :3]
                           @ zero[name][:3, :3].T @ WORLD_ROTATION.T)
            position_error = max(position_error, close(data.xpos[body], position, "FK position"))
            rotation_error = max(rotation_error, close(data.xmat[body].reshape(3, 3), orientation, "FK rotation"))
        for name, original, baked in samples.values():
            body = model.body(name).id
            expected = (original @ reference[name][:3, :3].T + reference[name][:3, 3]) @ WORLD_ROTATION.T
            actual = baked @ data.xmat[body].reshape(3, 3).T + data.xpos[body]
            world_error = max(world_error, close(actual, expected, "moving geometry", 1e-6))
        close(data.site_xpos[model.site("tool0").id], data.xpos[model.body("link6").id], "tool0")
    mujoco.mj_resetDataKeyframe(model, data, 0)
    mujoco.mj_forward(model, data)
    close(data.qpos, 0, "reset", 0)
    require(data.ncon == 0, "Formal zero pose must have no contacts")
    ground = model.geom("ground").id
    close([model.geom_contype[ground], model.geom_conaffinity[ground]], [1, 2], "ground mask", 0)
    floor_z = float(model.geom_pos[ground, 2])
    # Full collision meshes, not just sampled vertices, establish the actual clearance.
    clearance = min(float((read_stl(mesh_root / "collision" / f"{name}.stl")[1]["vertex"]
                           + data.xpos[model.body(name).id])[:, :, 2].min()) - floor_z for name in physical)
    require(clearance > 0, "Zero-pose collision mesh penetrates the ground")

    # Isolate ideal torque transmission from gravity, contact and joint stops.
    isolated = mujoco.MjModel.from_xml_path(str(scene))
    isolated.opt.gravity[:] = 0
    isolated.opt.disableflags |= int(mujoco.mjtDisableBit.mjDSBL_CONTACT) | int(mujoco.mjtDisableBit.mjDSBL_LIMIT)
    isolated_data = mujoco.MjData(isolated)
    responses = []
    for i in range(6):
        mujoco.mj_resetData(isolated, isolated_data)
        isolated_data.ctrl[i] = 1
        mujoco.mj_forward(isolated, isolated_data)
        close(isolated_data.qfrc_actuator, np.eye(6)[i], f"motor{i + 1} torque", 1e-12)
        require(isolated_data.qacc[i] > 0, "Positive torque must yield positive own acceleration")
        responses.append({"motor": f"motor{i + 1}", "input_Nm": 1,
                          "qacc_rad_s2": isolated_data.qacc.tolist()})
        for _ in range(10):
            mujoco.mj_step(isolated, isolated_data)
        require(np.isfinite(isolated_data.qpos).all() and isolated_data.qvel[i] > 0,
                "Non-finite or wrong-sign short torque response")

    saturation = []
    for scale in (-2, -.5, .5, 2):
        mujoco.mj_resetData(isolated, isolated_data)
        isolated_data.ctrl[:] = scale * TORQUE_LIMITS
        mujoco.mj_forward(isolated, isolated_data)
        expected = np.clip(isolated_data.ctrl, -TORQUE_LIMITS, TORQUE_LIMITS)
        close(isolated_data.actuator_force, expected, "motor force saturation", 1e-12)
        close(isolated_data.qfrc_actuator, expected, "joint torque saturation", 1e-12)
        saturation.append({"input_Nm": isolated_data.ctrl.tolist(), "output_Nm": expected.tolist()})

    # Exercise ground and self contacts within the joint ranges, including exclusions.
    ground_contacts = []
    self_contacts = []
    for q in rng.uniform(model.jnt_range[:, 0], model.jnt_range[:, 1], (100, 6)):
        data.qpos[:] = q
        mujoco.mj_forward(model, data)
        for contact in data.contact:
            bodies = frozenset(model.body(int(model.geom_bodyid[geom])).name
                               for geom in (contact.geom1, contact.geom2))
            require(bodies not in excluded_pairs, "Excluded bodies produced a contact")
        records = contact_records(model, data)
        if not ground_contacts:
            ground_contacts = [item for item in records if "ground" in item["geoms"]]
            if ground_contacts:
                ground_pose = q.tolist()
        if not self_contacts:
            self_contacts = [item for item in records if "ground" not in item["geoms"]]
            if self_contacts:
                self_pose = q.tolist()
        if ground_contacts and self_contacts:
            break
    require(bool(ground_contacts), "No robot-ground contacts found in diagnostic poses")
    require(bool(self_contacts), "No self contacts found in diagnostic poses")
    hashes[str(source.relative_to(ROOT))] = hashlib.sha256(source.read_bytes()).hexdigest()
    return {"status": "passed", "mujoco_version": mujoco.__version__,
            "scene": str(scene.relative_to(ROOT)), "source_sha256": hashes,
            "nq_nv_nu": [model.nq, model.nv, model.nu], "total_mass_kg": sum(masses.values()),
            "joint_ranges_rad": model.jnt_range.tolist(),
            "torque_limits_Nm": TORQUE_LIMITS.tolist(), "torque_saturation": saturation,
            "link_mass_kg": masses, "inertias": inertias, "meshes": geometry,
            "pose_cases": len(poses), "max_stl_vertex_error_m": vertex_error,
            "max_stl_normal_error": normal_error, "max_inertia_error_kg_m2": inertia_error,
            "max_fk_position_error_m": position_error, "max_fk_rotation_error": rotation_error,
            "max_moving_geometry_error_m": world_error,
            "ground_z_m": floor_z, "zero_collision_ground_clearance_m": clearance,
            "zero_formal_contacts": 0,
            "torque_responses": responses,
            "ground_contact_example": {"q_rad": ground_pose, "contacts": ground_contacts},
            "self_contact_example": {"q_rad": self_pose, "contacts": self_contacts}}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--scene", type=Path, default=ROOT / "assets/description/scene.xml")
    parser.add_argument("--source", type=Path, default=ROOT / "assets/description/robot.urdf")
    parser.add_argument("--output", type=Path, default=ROOT / "temp/model-check/checks.json")
    args = parser.parse_args()
    report = check(args.scene.resolve(), args.source.resolve())
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    print(f"PASS: nq=nv=nu=6; mass={report['total_mass_kg']:.12g} kg; {report['pose_cases']} poses")
    print(f"STL error={report['max_stl_vertex_error_m']:.3g} m; inertia error={report['max_inertia_error_kg_m2']:.3g} kg m^2")
    print(f"Zero ground clearance={report['zero_collision_ground_clearance_m'] * 1000:.6g} mm; formal contacts=0")
    print(f"Report: {args.output.resolve()}")


if __name__ == "__main__":
    main()
