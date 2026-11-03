#include "application.hpp"

namespace simlab {

Application::Application(const std::string &model_path, const ReferenceCase *reference_case)
    : simulation_(model_path),
      reference_(simulation_, reference_case),
      renderer_(simulation_.model()),
      ui_(window_),
      paused_(reference_case == nullptr) {}

void Application::run() {
    previous_ = Clock::now();
    while (!window_.shouldClose()) {
        const auto events = window_.pollEvents();
        if (window_.shouldClose()) break;
        if (window_.minimized()) {
            accumulated_ = 0;
            previous_ = Clock::now();
            was_suspended_ = true;
            window_.waitEvents();
            continue;
        }

        ui_.beginFrame();
        auto actions = ui_.buildControls(paused_, reference_, simulation_.positionLimits());
        for (const auto &event : events) {
            if (ui_.captures(event)) continue;
            switch (event.type) {
            case EventType::TogglePause:
                actions.toggle_pause = !actions.toggle_pause;
                break;
            case EventType::ResetSimulation:
                actions.reset = true;
                break;
            case EventType::ResetView:
                renderer_.resetCamera();
                break;
            default:
                renderer_.moveCamera(event, window_.logicalSize().height);
                break;
            }
        }
        if (actions.reset) {
            reference_.reset();
            paused_ = true;
        } else if (actions.toggle_pause && !reference_.completed()) {
            paused_ = !paused_;
        }
        advanceSimulation(actions.reset || actions.toggle_pause);

        if (ui_.buildReadouts(
            simulation_.readFeedback(), simulation_.lastTorques(), simulation_.time(), paused_, reference_)) {
            renderer_.resetCamera();
        }
        renderer_.draw(simulation_.data(), ui_.sceneSize(), ui_.showFrames());
        ui_.draw();
        window_.swapBuffers();
    }
}

void Application::advanceSimulation(bool reset_timing) {
    const auto now = Clock::now();
    const double elapsed = std::chrono::duration<double>(now - previous_).count();
    previous_ = now;
    if (paused_ || was_suspended_ || reset_timing) accumulated_ = 0;
    else accumulated_ += elapsed;
    was_suspended_ = false;

    const double timestep = simulation_.model().opt.timestep;
    while (accumulated_ >= timestep) {
        reference_.step();
        accumulated_ -= timestep;
        if (reference_.completed()) {
            paused_ = true;
            accumulated_ = 0;
            break;
        }
    }
}

} // namespace simlab
