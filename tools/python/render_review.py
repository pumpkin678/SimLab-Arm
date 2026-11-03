"""Render the arm's zero pose and positive joint directions from native MJCF."""

from __future__ import annotations

import argparse
import math
from pathlib import Path

import mujoco
import numpy as np
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[2]

COLORS = ["#2878c9", "#d77924", "#27934e", "#078b9d", "#8850bb", "#cb3740"]
INK, MUTED, BG = "#1c3041", "#53697a", "#f4f7fa"
TYPES = ["yaw", "pitch", "pitch", "roll", "pitch", "roll"]


def font(size, bold=False):
    return ImageFont.truetype("C:/Windows/Fonts/msyhbd.ttc" if bold else
                              "C:/Windows/Fonts/msyh.ttc", size)


def arrow(draw, start, end, color, width=4, head=13):
    start, end = np.asarray(start, float), np.asarray(end, float)
    delta = end - start
    if np.linalg.norm(delta) < 1:
        return
    direction = delta / np.linalg.norm(delta)
    normal = np.array([-direction[1], direction[0]])
    draw.line([tuple(start), tuple(end)], fill="white", width=width + 5)
    draw.line([tuple(start), tuple(end)], fill=color, width=width)
    draw.polygon([tuple(p) for p in (end, end - head * direction + .45 * head * normal,
                                    end - head * direction - .45 * head * normal)], fill=color)


def bounds(model, data, geom_ids):
    points = []
    for geom in geom_ids:
        center, half = model.geom_aabb[geom, :3], model.geom_aabb[geom, 3:]
        for signs in np.ndindex(2, 2, 2):
            local = center + half * (2 * np.array(signs) - 1)
            points.append(data.geom_xpos[geom] + data.geom_xmat[geom].reshape(3, 3) @ local)
    return np.array(points)


class View:
    def __init__(self, model, data, width, height):
        self.width, self.height = width, height
        self.renderer = mujoco.Renderer(model, height=height, width=width)
        option = mujoco.MjvOption()
        option.sitegroup[:] = 0
        option.geomgroup[:] = 0
        option.geomgroup[0] = 1
        option.geomgroup[1] = 1
        geom_ids = [i for i in range(model.ngeom) if model.geom_group[i] == 1]
        points = bounds(model, data, geom_ids)
        self.camera = mujoco.MjvCamera()
        mujoco.mjv_defaultFreeCamera(model, self.camera)
        self.camera.lookat[:] = (points.min(axis=0) + points.max(axis=0)) / 2
        self.camera.azimuth, self.camera.elevation = 315, -25
        self.camera.distance = 2
        self.renderer.update_scene(data, camera=self.camera, scene_option=option)
        camera = self.renderer.scene.camera[0]
        forward, up = np.array(camera.forward, float), np.array(camera.up, float)
        right = np.cross(forward, up)
        right /= np.linalg.norm(right)
        self.ty = math.tan(math.radians(float(model.vis.global_.fovy)) / 2)
        self.tx = self.ty * width / height
        relative = points - self.camera.lookat
        fit = np.max(np.maximum(abs(relative @ right) / self.tx,
                                abs(relative @ up) / self.ty) - relative @ forward)
        self.camera.distance = float(fit * 1.35)
        self.renderer.update_scene(data, camera=self.camera, scene_option=option)
        self.image = Image.fromarray(self.renderer.render().copy())
        camera = self.renderer.scene.camera[0]
        self.eye, self.forward = np.array(camera.pos, float), np.array(camera.forward, float)
        self.up = np.array(camera.up, float)
        self.right = np.cross(self.forward, self.up)
        self.right /= np.linalg.norm(self.right)

    def project(self, point):
        relative = np.asarray(point) - self.eye
        depth = float(relative @ self.forward)
        return np.array([self.width / 2 * (1 + relative @ self.right / depth / self.tx),
                         self.height / 2 * (1 - relative @ self.up / depth / self.ty)])

    def close(self):
        self.renderer.close()


def triad(image, view, location):
    draw = ImageDraw.Draw(image)
    origin = np.array(location)
    projected = view.project(view.camera.lookat)
    for axis, name, color in zip(np.eye(3), "XYZ", ["#d14545", "#26915a", "#2a6fd2"]):
        delta = view.project(view.camera.lookat + .06 * axis) - projected
        delta *= 55 / np.linalg.norm(delta)
        arrow(draw, origin, origin + delta, color)
        draw.text(tuple(origin + 1.2 * delta - [8, 12]), name, font=font(23, True), fill=color)
    draw.text(tuple(origin + [-72, 65]), "世界 / 基座 XYZ", font=font(20), fill=MUTED)


def zero_pose(scene, output):
    model = mujoco.MjModel.from_xml_path(str(scene))
    data = mujoco.MjData(model)
    mujoco.mj_forward(model, data)
    canvas = Image.new("RGB", (1600, 1160), BG)
    draw = ImageDraw.Draw(canvas)
    draw.text((50, 25), "机械臂零位与关节正方向", font=font(39, True), fill=INK)
    draw.text((52, 88), "全部关节角为 0°；世界坐标系 Z 轴向上", font=font(25), fill=MUTED)
    view = View(model, data, 1500, 900)
    image = view.image.copy()
    positions = [(20, 710), (20, 465), (1230, 715), (1230, 340), (280, 20), (900, 20)]
    draw = ImageDraw.Draw(image)
    for i in range(6):
        joint = model.joint(f"joint{i + 1}").id
        point, axis = data.xanchor[joint], data.xaxis[joint]
        uv = view.project(point)
        color = COLORS[i]
        arrow(draw, uv, view.project(point + .095 * axis), color, width=5)
        u = view.right - (view.right @ axis) * axis
        u /= np.linalg.norm(u)
        v = np.cross(axis, u)
        arc = [view.project(point + .021 * (math.cos(t) * u + math.sin(t) * v))
               for t in np.linspace(.3, .3 + 1.5 * math.pi, 70)]
        draw.line([tuple(p) for p in arc], fill="white", width=9)
        draw.line([tuple(p) for p in arc], fill=color, width=4)
        arrow(draw, arc[-4], arc[-1], color, width=3, head=10)
        x, y = positions[i]
        anchor = np.clip(uv, [x + 8, y + 8], [x + 242, y + 82])
        draw.line([tuple(uv), tuple(anchor)], fill="white", width=6)
        draw.line([tuple(uv), tuple(anchor)], fill=color, width=2)
        draw.ellipse((*tuple(uv - 5), *tuple(uv + 5)), fill=color, outline="white", width=2)
        draw.rounded_rectangle((x, y, x + 250, y + 90), radius=10, fill="white", outline=color, width=2)
        draw.text((x + 12, y + 8), f"J{i + 1} · {TYPES[i]} · link{i + 1}", font=font(24, True), fill=color)
        draw.text((x + 12, y + 50), "局部正轴 +" + ("Z", "Y", "Y", "X", "Y", "X")[i], font=font(23), fill=INK)
    triad(image, view, (430, 775))
    canvas.paste(image, (50, 145))
    draw = ImageDraw.Draw(canvas)
    draw.text((52, 1070), "实线箭头：关节正轴；圆弧箭头：关节角增大时的旋转方向（右手定则）。", font=font(25), fill=INK)
    draw.text((52, 1115), "零位时，各连杆坐标系的 XYZ 方向与世界坐标系一致。", font=font(24), fill=MUTED)
    canvas.save(output)
    view.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--scene", type=Path, default=ROOT / "assets/description/scene.xml")
    parser.add_argument("--output", type=Path, default=ROOT / "assets/images/zero_pose.png")
    args = parser.parse_args()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    zero_pose(args.scene.resolve(), args.output)
    print(f"Rendered: {args.output.resolve()}")


if __name__ == "__main__":
    main()
