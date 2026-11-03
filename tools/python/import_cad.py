"""Explicit CAD import into a new candidate folder; never run during a build."""

from __future__ import annotations

import argparse
from pathlib import Path
import struct
import xml.etree.ElementTree as ET

import numpy as np

ROOT = Path(__file__).resolve().parents[2]
WORLD_ROTATION = np.diag([-1.0, -1.0, 1.0])
JOINT_AXES = np.array([[0, 0, 1], [0, 1, 0], [0, 1, 0], [1, 0, 0], [0, 1, 0], [1, 0, 0]])
JOINT_RANGES = np.pi * np.array([
    [-7 / 6, 1 / 6], [0, 5 / 6], [-5 / 6, 0], [-1, 1], [-1 / 2, 1 / 2], [-1, 1],
])
TORQUE_LIMITS = np.array([20, 80, 20, 10, 10, 2])
STL_FACE = np.dtype([("normal", "<f4", (3,)), ("vertex", "<f4", (3, 3)), ("attribute", "<u2")])


def pose(element):
    result = np.eye(4)
    if element is None:
        return result
    result[:3, 3] = np.fromstring(element.get("xyz", "0 0 0"), sep=" ")
    r, p, y = np.fromstring(element.get("rpy", "0 0 0"), sep=" ")
    cr, sr, cp, sp, cy, sy = np.cos(r), np.sin(r), np.cos(p), np.sin(p), np.cos(y), np.sin(y)
    result[:3, :3] = [[cy*cp, cy*sp*sr-sy*cr, cy*sp*cr+sy*sr],
                     [sy*cp, sy*sp*sr+cy*cr, sy*sp*cr-cy*sr], [-sp, cp*sr, cp*cr]]
    return result


def source_model(path):
    robot = ET.parse(path).getroot()
    links = {link.get("name"): link for link in robot.findall("link")}
    joints = robot.findall("joint")
    transforms = {"base_link": np.eye(4)}
    ordered = ["base_link"]
    remaining = list(joints)
    while remaining:
        ready = [j for j in remaining if j.find("parent").get("link") in transforms]
        if not ready:
            raise ValueError("URDF is not a connected arm tree rooted at base_link.")
        for joint in ready:
            parent, child = joint.find("parent").get("link"), joint.find("child").get("link")
            transforms[child] = transforms[parent] @ pose(joint.find("origin"))
            ordered.append(child)
            remaining.remove(joint)
    if set(ordered) != set(links):
        raise ValueError("Unreachable URDF links.")
    return links, joints, transforms, ordered


def read_stl(path):
    raw = Path(path).read_bytes()
    if len(raw) < 84:
        raise ValueError(f"Invalid binary STL: {path}")
    count = struct.unpack_from("<I", raw, 80)[0]
    if not 1 <= count <= 200000 or len(raw) != 84 + count * STL_FACE.itemsize:
        raise ValueError(f"Invalid or oversized MuJoCo STL: {path}")
    return raw[:84], np.frombuffer(raw, dtype=STL_FACE, offset=84, count=count).copy()


def mesh_vertices(item, source_dir):
    mesh = item.find("geometry/mesh")
    if mesh is None:
        raise ValueError("This CAD importer expects mesh geometry.")
    header, faces = read_stl(source_dir / mesh.get("filename"))
    scale = np.fromstring(mesh.get("scale", "1 1 1"), sep=" ")
    transform = pose(item.find("origin"))
    vertices = (faces["vertex"].astype(float) * scale) @ transform[:3, :3].T + transform[:3, 3]
    return header, faces, vertices


def source_inertia(link):
    inertial = link.find("inertial")
    frame = pose(inertial.find("origin"))
    values = {key: float(value) for key, value in inertial.find("inertia").attrib.items()}
    tensor = np.array([[values["ixx"], values["ixy"], values["ixz"]],
                       [values["ixy"], values["iyy"], values["iyz"]],
                       [values["ixz"], values["iyz"], values["izz"]]])
    return (float(inertial.find("mass").get("value")), frame[:3, 3],
            frame[:3, :3] @ tensor @ frame[:3, :3].T)


def numbers(values):
    return " ".join(format(0.0 if abs(float(x)) < 1e-14 else float(x), ".17g") for x in values)


def write_xml(path, root):
    ET.indent(root, space="  ")
    ET.ElementTree(root).write(path, encoding="utf-8", xml_declaration=True)


def import_model(source, output):
    links, joints, zero, ordered = source_model(source)
    robot_path = output / "description" / "arm.xml"
    if robot_path.exists():
        raise FileExistsError("Choose a new --output directory; an existing MJCF will not be overwritten.")
    robot_path.parent.mkdir(parents=True, exist_ok=True)
    mesh_root = output / "meshes" / "mjcf"
    for kind in ("visual", "collision"):
        (mesh_root / kind).mkdir(parents=True, exist_ok=True)

    model = ET.Element("mujoco", model="SimLab-Arm")
    model.append(ET.Comment(" Maintained native MJCF. CAD import is explicit, not a build dependency. "))
    ET.SubElement(model, "compiler", angle="radian", meshdir="../meshes/mjcf", inertiafromgeom="false", fusestatic="false", autolimits="false")
    ET.SubElement(model, "option", timestep="0.001", integrator="implicitfast", gravity="0 0 -9.81")
    defaults = ET.SubElement(model, "default")
    ET.SubElement(defaults, "joint", type="hinge", limited="true", actuatorfrclimited="false", damping="0", frictionloss="0", armature="0")
    ET.SubElement(defaults, "geom", density="0", contype="0", conaffinity="0")
    ET.SubElement(defaults, "site", size="0.003", group="3", rgba="0.9 0.9 0.9 1")
    assets = ET.SubElement(model, "asset")
    world = ET.SubElement(model, "worldbody")
    contact = ET.SubElement(model, "contact")
    ET.SubElement(contact, "exclude", body1="base_link", body2="link1")
    ET.SubElement(contact, "exclude", body1="base_link", body2="link2")
    actuator = ET.SubElement(model, "actuator")
    actuator.append(ET.Comment(" Ideal joint torque [N m]. "))
    bodies = {}
    by_child = {j.find("child").get("link"): j for j in joints}
    ground_height = None

    for name in ordered:
        if name == "tool0":
            if not np.allclose(zero[name], zero["link6"]):
                raise ValueError("tool0 has changed; explicitly define its new frame before importing.")
            ET.SubElement(bodies["link6"], "site", name="tool0")
            continue
        joint = by_child.get(name)
        parent_name = joint.find("parent").get("link") if joint is not None else None
        parent_position = zero[parent_name][:3, 3] if parent_name else np.zeros(3)
        body = ET.SubElement(bodies[parent_name] if parent_name else world, "body", name=name,
                             pos=numbers(WORLD_ROTATION @ (zero[name][:3, 3] - parent_position)))
        bodies[name] = body
        align = WORLD_ROTATION @ zero[name][:3, :3]
        if joint is not None:
            index = int(joint.get("name").removeprefix("joint")) - 1
            if joint.get("type") != "revolute" or not 0 <= index < 6:
                raise ValueError("Expected joint1 through joint6, all revolute.")
            source_axis = np.fromstring(joint.find("axis").get("xyz"), sep=" ")
            sign = -1 if index in (3, 4) else 1
            if not np.allclose(align @ source_axis, sign * JOINT_AXES[index], atol=1e-10):
                raise ValueError(f"{joint.get('name')} CAD axis changed; review the coordinate convention.")
            ET.SubElement(body, "joint", name=joint.get("name"), axis=numbers(JOINT_AXES[index]),
                          pos="0 0 0", ref="0", range=numbers(JOINT_RANGES[index]))
            ET.SubElement(body, "site", name=joint.get("name") + "_frame")
            limit = TORQUE_LIMITS[index]
            ET.SubElement(actuator, "motor", name=f"motor{index+1}", joint=joint.get("name"), gear="1",
                          ctrllimited="false", forcelimited="true", forcerange=numbers([-limit, limit]))
        else:
            ET.SubElement(body, "site", name="base_frame")
        mass, center, tensor = source_inertia(links[name])
        transformed = align @ tensor @ align.T
        ET.SubElement(body, "inertial", pos=numbers(align @ center), mass=numbers([mass]),
                      fullinertia=numbers([transformed[0, 0], transformed[1, 1], transformed[2, 2],
                                          transformed[0, 1], transformed[0, 2], transformed[1, 2]]))
        for kind in ("visual", "collision"):
            items = links[name].findall(kind)
            if len(items) != 1:
                raise ValueError(f"Expected one {kind} mesh for {name}.")
            item = items[0]
            header, faces, vertices = mesh_vertices(item, source.parent)
            baked = vertices @ align.T
            faces["vertex"] = baked
            # Recompute normals from the stored float32 vertices; preserve face order/count/attributes.
            stored = faces["vertex"].astype(float)
            normals = np.cross(stored[:, 1] - stored[:, 0], stored[:, 2] - stored[:, 0])
            length = np.linalg.norm(normals, axis=1)
            nonzero = length > 0
            normals[nonzero] /= length[nonzero, None]
            faces["normal"] = normals
            (mesh_root / kind / f"{name}.stl").write_bytes(header + faces.tobytes())
            mesh_name = f"{kind}_{name}"
            ET.SubElement(assets, "mesh", name=mesh_name, file=f"{kind}/{name}.stl")
            attrs = {"name": mesh_name, "type": "mesh", "mesh": mesh_name}
            if kind == "visual":
                color = item.find("material/color")
                attrs.update(group="1", rgba=color.get("rgba") if color is not None else "0.6 0.65 0.7 1")
                if name == "base_link":
                    ground_height = float(baked[:, :, 2].min()) - 0.001
            else:
                attrs.update(group="3", contype="2", conaffinity="3", rgba="0.8 0.2 0.2 0.3")
            ET.SubElement(body, "geom", **attrs)
    ET.SubElement(ET.SubElement(model, "keyframe"), "key", name="zero", qpos="0 0 0 0 0 0", ctrl="0 0 0 0 0 0")
    write_xml(robot_path, model)

    scene = ET.Element("mujoco", model="SimLab-Arm scene")
    ET.SubElement(scene, "include", file="arm.xml")
    visual = ET.SubElement(scene, "visual")
    ET.SubElement(visual, "global", offwidth="1600", offheight="1100", azimuth="315", elevation="-25")
    ET.SubElement(visual, "headlight", ambient="0.5 0.5 0.5", diffuse="0.6 0.6 0.6", specular="0.1 0.1 0.1")
    ET.SubElement(visual, "scale", framelength="0.08", framewidth="0.003")
    scene_assets = ET.SubElement(scene, "asset")
    ET.SubElement(scene_assets, "texture", name="background", type="skybox", builtin="gradient", rgb1="0.65 0.75 0.85", rgb2="0.95 0.97 0.99", width="256", height="1536")
    ET.SubElement(scene_assets, "texture", name="ground_grid", type="2d", builtin="flat", mark="edge", rgb1="0.82 0.84 0.86", markrgb="0.42 0.46 0.50", width="128", height="128")
    ET.SubElement(scene_assets, "material", name="ground_grid", texture="ground_grid", texrepeat="10 10", texuniform="true", reflectance="0")
    scene_world = ET.SubElement(scene, "worldbody")
    ET.SubElement(scene_world, "geom", name="ground", type="plane", pos=numbers([0, 0, ground_height]), size="3 3 0.05", material="ground_grid", rgba="1 1 1 1", contype="1", conaffinity="2", group="0")
    ET.SubElement(scene_world, "light", pos="0 1 2", dir="0 0 -1", directional="true", diffuse="0.35 0.35 0.35", castshadow="false")
    ET.SubElement(scene_world, "light", pos="0 -1 1", dir="0 1 -1", directional="true", diffuse="0.25 0.25 0.25", castshadow="false")
    write_xml(output / "description" / "scene.xml", scene)
    print(f"Candidate model and 14 baked STL files written to {output}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, default=ROOT / "assets/description/robot.urdf")
    parser.add_argument("--output", type=Path, default=ROOT / "temp" / "mjcf-import")
    args = parser.parse_args()
    import_model(args.source.resolve(), args.output.resolve())
