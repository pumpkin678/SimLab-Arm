#include "renderer.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numbers>
#include <vector>

namespace simlab {

Renderer::Renderer(const mjModel &model)
    : model_(model) {
    mjv_defaultOption(&option_);
    option_.geomgroup[3] = 0;
    mjv_defaultScene(&scene_);
    mjr_defaultContext(&context_);
    mjv_makeScene(&model_, &scene_, 1000);
    mjr_makeContext(&model_, &context_, mjFONTSCALE_150);
    resetCamera();
}

Renderer::~Renderer() {
    mjr_freeContext(&context_);
    mjv_freeScene(&scene_);
}

void Renderer::draw(mjData &data, WindowSize size, bool show_frames) {
    if (frame_pending_) frameModel(data, size);
    option_.sitegroup[3] = show_frames;
    option_.frame = show_frames ? mjFRAME_SITE : mjFRAME_NONE;
    mjv_updateScene(&model_, &data, &option_, nullptr, &camera_, mjCAT_ALL, &scene_);
    mjr_render({0, 0, size.width, size.height}, &scene_, &context_);
}

void Renderer::moveCamera(const InputEvent &event, int logical_height) {
    if (logical_height <= 0) return;
    if (event.type == EventType::Zoom) {
        mjv_moveCamera(&model_, mjMOUSE_ZOOM, 0, -0.05 * event.dy, &scene_, &camera_);
    } else {
        const auto action = event.type == EventType::Pan ? mjMOUSE_MOVE_V : mjMOUSE_ROTATE_V;
        mjv_moveCamera(&model_, action, event.dx / logical_height, event.dy / logical_height, &scene_, &camera_);
    }
}

void Renderer::resetCamera() {
    mjv_defaultFreeCamera(&model_, &camera_);
    camera_.orthographic = 0;
    std::copy_n(model_.stat.center, 3, camera_.lookat);
    camera_.distance = std::max(0.1, 2.0 * model_.stat.extent);
    camera_.azimuth = model_.vis.global.azimuth;
    camera_.elevation = model_.vis.global.elevation;
    frame_pending_ = true;
}

void Renderer::frameModel(const mjData &data, WindowSize size) {
    std::array<double, 3> lower;
    std::array<double, 3> upper;
    lower.fill(std::numeric_limits<double>::max());
    upper.fill(std::numeric_limits<double>::lowest());
    std::vector<std::array<double, 3>> corners;
    for (mjtSize geom = 0; geom < model_.ngeom; ++geom) {
        if (model_.geom_type[geom] == mjGEOM_PLANE || model_.geom_rgba[4 * geom + 3] <= 0) continue;
        if (!option_.geomgroup[model_.geom_group[geom]]) continue;
        const auto *box = model_.geom_aabb + 6 * geom;
        const auto *position = data.geom_xpos + 3 * geom;
        const auto *rotation = data.geom_xmat + 9 * geom;
        for (int corner = 0; corner < 8; ++corner) {
            std::array<double, 3> local;
            for (int axis = 0; axis < 3; ++axis) {
                local[axis] = box[axis] + ((corner & (1 << axis)) ? box[axis + 3] : -box[axis + 3]);
            }
            std::array<double, 3> world;
            for (int axis = 0; axis < 3; ++axis) {
                world[axis] = position[axis] + rotation[3 * axis] * local[0] +
                    rotation[3 * axis + 1] * local[1] + rotation[3 * axis + 2] * local[2];
                lower[axis] = std::min(lower[axis], world[axis]);
                upper[axis] = std::max(upper[axis], world[axis]);
            }
            corners.push_back(world);
        }
    }
    if (!corners.empty()) {
        for (int axis = 0; axis < 3; ++axis) {
            camera_.lookat[axis] = 0.5 * (lower[axis] + upper[axis]);
        }
        mjv_updateCamera(&model_, &data, &camera_, &scene_);
        mjtNum forward[3];
        mjtNum up[3];
        mjtNum right[3];
        std::copy_n(scene_.camera[0].forward, 3, forward);
        std::copy_n(scene_.camera[0].up, 3, up);
        mju_cross(right, forward, up);
        mju_normalize3(right);
        const double aspect = static_cast<double>(size.width) / std::max(1, size.height);
        const double vertical_half_fov = model_.vis.global.fovy * std::numbers::pi / 360.0;
        const double horizontal_half_fov = std::atan(std::tan(vertical_half_fov) * aspect);
        double distance = 0.01;
        for (const auto &corner : corners) {
            mjtNum relative[3];
            mju_sub3(relative, corner.data(), camera_.lookat);
            const double depth = mju_dot3(relative, forward);
            distance = std::max(distance, std::abs(mju_dot3(relative, right)) / std::tan(horizontal_half_fov) - depth);
            distance = std::max(distance, std::abs(mju_dot3(relative, up)) / std::tan(vertical_half_fov) - depth);
        }
        camera_.distance = 1.08 * distance;
    }
    frame_pending_ = false;
}

} // namespace simlab
