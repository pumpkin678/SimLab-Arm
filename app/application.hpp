#ifndef SIMLAB_APP_APPLICATION_HPP
#define SIMLAB_APP_APPLICATION_HPP

#include "core/window.hpp"
#include "core/renderer.hpp"
#include "simulation/simulation.hpp"
#include "app/reference/reference.hpp"
#include "app/ui/ui.hpp"

#include <chrono>
#include <string>

namespace simlab {

class Application {
public:
    explicit Application(
        const std::string &model_path, const ReferenceCase *reference_case = nullptr,
        const std::optional<TraceOptions> &trace = std::nullopt);
    void run();

private:
    using Clock = std::chrono::steady_clock;

    void advanceSimulation(bool reset_timing);

    // Keep the model and OpenGL context alive until UI and renderer cleanup finishes.
    Simulation simulation_;
    ReferenceRun reference_;
    Window window_;
    Renderer renderer_;
    Ui ui_;
    Clock::time_point previous_{};
    double accumulated_{};
    bool paused_{true};
    bool was_suspended_{};
};

} // namespace simlab

#endif
