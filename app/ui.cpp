#include "ui.hpp"

#include <GLFW/glfw3.h>
#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl2.h>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>

namespace simlab {

namespace {

constexpr float kSidebarWidth = 560.0F;
constexpr float kMinimumSceneWidth = 320.0F;
constexpr double kRadiansToDegrees = 180.0 / std::numbers::pi;

} // namespace

Ui::Ui(Window &window)
    : window_(window) {
    IMGUI_CHECKVERSION();
    context_ = ImGui::CreateContext();
    if (!context_) throw std::runtime_error("Failed to create ImGui context.");
    ImGui::SetCurrentContext(context_);
    auto &io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigFlags &= ~ImGuiConfigFlags_NavEnableKeyboard;
    ImFontConfig font;
    font.SizePixels = 22.0F;
    io.Fonts->AddFontDefault(&font);
    ImGui::StyleColorsDark();
    auto &style = ImGui::GetStyle();
    style.ScaleAllSizes(1.15F);
    style.WindowMinSize = {0.0F, 0.0F};
    style.WindowRounding = 0.0F;
    style.WindowBorderSize = 0.0F;
    style.Colors[ImGuiCol_WindowBg].w = 1.0F;
    if (!ImGui_ImplGlfw_InitForOpenGL(window_.nativeHandle(), true)) {
        ImGui::DestroyContext(context_);
        throw std::runtime_error("Failed to initialize ImGui GLFW backend.");
    }
    if (!ImGui_ImplOpenGL2_Init()) {
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext(context_);
        throw std::runtime_error("Failed to initialize ImGui OpenGL backend.");
    }
}

Ui::~Ui() {
    ImGui::SetCurrentContext(context_);
    ImGui_ImplOpenGL2_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext(context_);
}

void Ui::beginFrame() {
    ImGui::SetCurrentContext(context_);
    ImGui_ImplOpenGL2_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
    framebuffer_size_ = window_.framebufferSize();
    const float window_width = ImGui::GetIO().DisplaySize.x;
    const float sidebar_width = std::clamp(window_width - kMinimumSceneWidth, 0.0F, kSidebarWidth);
    const int sidebar_pixels = static_cast<int>(std::lround(sidebar_width * framebuffer_size_.width / window_width));
    scene_size_ = {framebuffer_size_.width - sidebar_pixels, framebuffer_size_.height};
}

UiActions Ui::buildControls(bool paused) {
    UiActions actions;
    if (scene_size_.width == framebuffer_size_.width) return actions;
    const auto &io = ImGui::GetIO();
    const float scene_width = io.DisplaySize.x * scene_size_.width / framebuffer_size_.width;
    ImGui::SetNextWindowPos({scene_width, 0.0F});
    ImGui::SetNextWindowSize({io.DisplaySize.x - scene_width, io.DisplaySize.y});
    ImGui::Begin("Simulation", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);
    ImGui::SeparatorText("Run");
    actions.toggle_pause = ImGui::Button(paused ? "Run (Space)" : "Pause (Space)");
    ImGui::SameLine();
    actions.reset = ImGui::Button("Reset (R)");
    // Keep the panel open so readouts can be drawn after this frame's physics steps.
    return actions;
}

bool Ui::buildReadouts(const JointFeedback &feedback, const JointTorques &torques, float time, bool paused) {
    if (scene_size_.width == framebuffer_size_.width) return false;
    ImGui::Text("State: %s", paused ? "Paused" : "Running");
    ImGui::Text("Time: %.3f s", time);

    if (ImGui::CollapsingHeader("Joints", ImGuiTreeNodeFlags_DefaultOpen)) {
        constexpr auto kFlags = ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg |
            ImGuiTableFlags_SizingStretchProp;
        if (ImGui::BeginTable("joint state", 4, kFlags)) {
            ImGui::TableSetupColumn("Joint", ImGuiTableColumnFlags_WidthFixed, 52.0F);
            ImGui::TableSetupColumn("Angle [deg]");
            ImGui::TableSetupColumn("Rate [deg/s]");
            ImGui::TableSetupColumn("Torque [Nm]");
            ImGui::TableHeadersRow();
            for (std::size_t index = 0; index < feedback.position.size(); ++index) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::Text("J%d", static_cast<int>(index + 1));
                ImGui::TableSetColumnIndex(1);
                ImGui::Text("% .3f", feedback.position[index] * kRadiansToDegrees);
                ImGui::TableSetColumnIndex(2);
                ImGui::Text("% .3f", feedback.velocity[index] * kRadiansToDegrees);
                ImGui::TableSetColumnIndex(3);
                ImGui::Text("% .3f", torques[index]);
            }
            ImGui::EndTable();
        }
    }

    ImGui::SeparatorText("View");
    ImGui::Checkbox("Coordinate frames", &show_frames_);
    ImGui::SetItemTooltip("X: red, Y: green, Z: blue");
    const bool reset_camera = ImGui::Button("Reset view (F)");
    ImGui::End();
    return reset_camera;
}

bool Ui::captures(const InputEvent &event) const {
    if (event.type == EventType::TogglePause || event.type == EventType::ResetSimulation ||
        event.type == EventType::ResetView) return ImGui::GetIO().WantCaptureKeyboard;
    const double scene_width = ImGui::GetIO().DisplaySize.x * scene_size_.width / framebuffer_size_.width;
    return ImGui::GetIO().WantCaptureMouse || event.origin_x >= scene_width;
}

void Ui::draw() {
    ImGui::Render();

    // MuJoCo leaves texture unit 1 selected; ImGui's fixed-function backend draws on unit 0.
    using ActivateTexture = void (APIENTRY *)(GLenum);
    const auto activate_texture = reinterpret_cast<ActivateTexture>(glfwGetProcAddress("glActiveTexture"));
    const auto activate_client_texture = reinterpret_cast<ActivateTexture>(glfwGetProcAddress("glClientActiveTexture"));
    if (!activate_texture || !activate_client_texture) {
        throw std::runtime_error("OpenGL texture-unit functions are unavailable.");
    }
    constexpr GLenum kTextureUnitZero = 0x84C0;
    constexpr GLenum kActiveTexture = 0x84E0;
    constexpr GLenum kClientActiveTexture = 0x84E1;
    GLint previous_texture = 0;
    GLint previous_client_texture = 0;
    glGetIntegerv(kActiveTexture, &previous_texture);
    glGetIntegerv(kClientActiveTexture, &previous_client_texture);
    activate_texture(kTextureUnitZero);
    activate_client_texture(kTextureUnitZero);
    ImGui_ImplOpenGL2_RenderDrawData(ImGui::GetDrawData());
    activate_client_texture(static_cast<GLenum>(previous_client_texture));
    activate_texture(static_cast<GLenum>(previous_texture));
}

} // namespace simlab
