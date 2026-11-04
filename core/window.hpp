#ifndef SIMLAB_CORE_WINDOW_HPP
#define SIMLAB_CORE_WINDOW_HPP

#include <vector>

struct GLFWwindow;

namespace simlab {

enum class EventType { TogglePause, ResetSimulation, ResetView, Cancel, Rotate, Pan, Zoom };

struct InputEvent {
    EventType type{};
    double dx{};
    double dy{};
    double origin_x{};
    double origin_y{};
};

struct WindowSize {
    int width{};
    int height{};
};

class Window {
public:
    explicit Window(const char *title = "SimLab-Arm");
    ~Window();
    Window(const Window &) = delete;
    Window &operator=(const Window &) = delete;
    std::vector<InputEvent> pollEvents();
    bool shouldClose() const;
    bool minimized() const;
    WindowSize framebufferSize() const;
    WindowSize logicalSize() const;
    GLFWwindow *nativeHandle() const noexcept { return window_; }
    void swapBuffers();
    void waitEvents();

private:
    static Window &self(GLFWwindow *window);
    static void keyCallback(GLFWwindow *, int, int, int, int);
    static void buttonCallback(GLFWwindow *, int, int, int);
    static void cursorCallback(GLFWwindow *, double, double);
    static void scrollCallback(GLFWwindow *, double, double);

    GLFWwindow *window_{};
    std::vector<InputEvent> events_;
    double cursor_x_{};
    double cursor_y_{};
    double left_origin_x_{};
    double left_origin_y_{};
    double right_origin_x_{};
    double right_origin_y_{};
};

} // namespace simlab

#endif
