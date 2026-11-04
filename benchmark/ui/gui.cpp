#include "benchmark/ui/gui.hpp"
#include "app/ui/ui.hpp"
#include "core/renderer.hpp"

#include <GLFW/glfw3.h>
#include <imgui.h>

#include <chrono>
#include <numbers>
#include <optional>

namespace simlab::benchmark {

namespace {

class BenchmarkGui {
public:
    explicit BenchmarkGui(Runner &runner)
        : runner_(runner),
          window_("SimLab-Arm - Benchmark"),
          ui_(window_) {
        if (runner_.simulation()) renderer_.emplace(runner_.simulation()->model());
    }

    void run() {
        previous_ = Clock::now();
        while (!window_.shouldClose()) {
            const auto events = window_.pollEvents();
            if (window_.shouldClose()) break;
            if (window_.minimized()) {
                previous_ = Clock::now();
                accumulated_ = 0;
                was_suspended_ = true;
                window_.waitEvents();
                continue;
            }
            ui_.beginFrame();
            const bool panel = ui_.beginPanel();
            if (panel) {
                ImGui::SeparatorText("Benchmark");
                ImGui::BeginDisabled(runner_.terminal());
                if (ImGui::Button(paused_ ? "Resume (Space)" : "Pause (Space)")) togglePause();
                ImGui::SameLine();
                if (ImGui::Button("Cancel (Backspace)")) runner_.cancel("Cancelled by user");
                ImGui::EndDisabled();
            }
            for (const auto &event : events) {
                if (ui_.captures(event)) continue;
                switch (event.type) {
                case EventType::TogglePause: togglePause(); break;
                case EventType::Cancel: runner_.cancel("Cancelled by user"); break;
                case EventType::ResetSimulation: break;
                case EventType::ResetView:
                    if (renderer_) renderer_->resetCamera();
                    break;
                default:
                    if (renderer_) renderer_->moveCamera(event, window_.logicalSize().height);
                    break;
                }
            }
            advance();
            const bool usable = runner_.canRender();
            if (panel) {
                const auto &report = runner_.reports()[runner_.caseIndex()];
                ImGui::Text("Case %d / %d: %s", static_cast<int>(runner_.caseIndex() + 1),
                    static_cast<int>(runner_.reports().size()), runner_.spec().id.c_str());
                ImGui::TextUnformatted(runner_.recording() ? "Recording" : "Preview - no files saved");
                ImGui::Text("Status: %.*s", static_cast<int>(outcomeName(runner_.outcome()).size()), outcomeName(runner_.outcome()).data());
                ImGui::Text("Steps: %llu / %llu", static_cast<unsigned long long>(report.samples),
                    static_cast<unsigned long long>(report.expected_steps));
                ImGui::Text("Duration: %.3f s", runner_.spec().duration);
                if (const auto *phase = runner_.lastPhase()) {
                    ImGui::Text("Phase: %s (%.*s)", phase->id.c_str(),
                        static_cast<int>(phaseKindName(phase->kind).size()), phaseKindName(phase->kind).data());
                    ImGui::Text("Phase time: %.3f / %.3f s", runner_.lastSampleTime() - phase->start, phase->end - phase->start);
                    ImGui::TextUnformatted("Active joints:");
                    bool any = false;
                    for (std::size_t joint = 0; joint < phase->active_joints.size(); ++joint) {
                        if (!phase->active_joints[joint]) continue;
                        ImGui::SameLine();
                        ImGui::Text("J%d", static_cast<int>(joint + 1));
                        any = true;
                    }
                    if (!any) {
                        ImGui::SameLine();
                        ImGui::TextUnformatted("None (initialize / hold)");
                    }
                } else ImGui::TextUnformatted("Phase: no input sample yet");
                for (const auto &parameter : runner_.spec().parameters) {
                    ImGui::Text("%s: %.6g %s", parameter.name.c_str(), parameter.value, parameter.unit.c_str());
                }
                if (!runner_.reason().empty()) ImGui::TextWrapped("%s", runner_.reason().c_str());
                if (runner_.terminal()) ImGui::TextWrapped("Close to finish. Start a new process to run again.");
                ImGui::SeparatorText("Latest reference [deg]");
                if (report.samples) {
                    const auto &target = runner_.lastCommand();
                    for (std::size_t index = 0; index < target.position.size(); ++index) {
                        ImGui::Text("J%d: % .3f", static_cast<int>(index + 1),
                            target.position[index] * 180.0 / std::numbers::pi);
                    }
                } else ImGui::TextUnformatted("No input sample yet");
                if (usable) {
                    const auto &simulation = *runner_.simulation();
                    if (ui_.buildReadouts(simulation.readFeedback(), simulation.result(), simulation.lastTorques(),
                        simulation.time(), paused_ || runner_.terminal()) && renderer_) renderer_->resetCamera();
                } else ImGui::TextWrapped("No valid physical state is available for display.");
            }
            ui_.endPanel();
            const auto size = window_.framebufferSize();
            glViewport(0, 0, size.width, size.height);
            glClearColor(0.12F, 0.12F, 0.12F, 1.0F);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            if (usable && renderer_) renderer_->draw(runner_.simulation()->data(), ui_.sceneSize(), ui_.showFrames());
            ui_.draw();
            window_.swapBuffers();
        }
        runner_.cancel("Window closed by user");
    }

private:
    using Clock = std::chrono::steady_clock;

    void togglePause() {
        if (runner_.terminal()) return;
        paused_ = !paused_;
        reset_timing_ = true;
    }

    void advance() {
        const auto now = Clock::now();
        const double elapsed = std::chrono::duration<double>(now - previous_).count();
        previous_ = now;
        if (paused_ || runner_.terminal() || reset_timing_ || was_suspended_) accumulated_ = 0;
        else accumulated_ += elapsed;
        reset_timing_ = false;
        was_suspended_ = false;
        if (paused_ || runner_.terminal()) return;
        if (runner_.caseComplete()) {
            renderer_.reset();
            runner_.startNext();
            if (runner_.simulation() && runner_.canRender()) renderer_.emplace(runner_.simulation()->model());
            accumulated_ = 0;
            previous_ = Clock::now();
            return;
        }
        const double timestep = runner_.simulation()->model().opt.timestep;
        while (accumulated_ >= timestep) {
            runner_.advanceOneCycle();
            accumulated_ -= timestep;
            if (runner_.terminal() || runner_.caseComplete()) {
                accumulated_ = 0;
                break;
            }
        }
    }

    Runner &runner_;
    Window window_;
    Ui ui_;
    std::optional<Renderer> renderer_;
    Clock::time_point previous_{};
    double accumulated_{};
    bool paused_{};
    bool was_suspended_{};
    bool reset_timing_{};
};

} // namespace

void runGui(Runner &runner) {
    BenchmarkGui gui(runner);
    gui.run();
}

} // namespace simlab::benchmark
