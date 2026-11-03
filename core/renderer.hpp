#ifndef SIMLAB_CORE_RENDERER_HPP
#define SIMLAB_CORE_RENDERER_HPP

#include "window.hpp"

#include <mujoco/mujoco.h>

namespace simlab {

class Renderer {
public:
    explicit Renderer(const mjModel &model);
    ~Renderer();
    Renderer(const Renderer &) = delete;
    Renderer &operator=(const Renderer &) = delete;
    void draw(mjData &data, WindowSize size, bool show_frames);
    void moveCamera(const InputEvent &event, int logical_height);
    void resetCamera();

private:
    void frameModel(const mjData &data, WindowSize size);

    const mjModel &model_;
    mjvCamera camera_{};
    mjvOption option_{};
    mjvScene scene_{};
    mjrContext context_{};
    bool frame_pending_{};
};

} // namespace simlab

#endif
