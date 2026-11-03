#ifndef SIMLAB_APP_UI_HPP
#define SIMLAB_APP_UI_HPP

#include "core/window.hpp"
#include "simulation/arm/types.hpp"

struct ImGuiContext;

namespace simlab {

struct UiActions {
    bool toggle_pause{};
    bool reset{};
};

class Ui {
public:
    explicit Ui(Window &window);
    ~Ui();
    Ui(const Ui &) = delete;
    Ui &operator=(const Ui &) = delete;
    void beginFrame();
    UiActions buildControls(bool paused);
    bool buildReadouts(const JointState &state, const JointTorques &torques, bool paused);
    void draw();
    WindowSize sceneSize() const noexcept { return scene_size_; }
    bool captures(const InputEvent &event) const;
    bool showFrames() const noexcept { return show_frames_; }

private:
    Window &window_;
    ImGuiContext *context_{};
    WindowSize scene_size_{};
    WindowSize framebuffer_size_{};
    bool show_frames_{};
};

} // namespace simlab

#endif
