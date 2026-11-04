#include "window.hpp"

#include <GLFW/glfw3.h>

#include <iostream>
#include <stdexcept>
#include <utility>

namespace simlab {

Window::Window(const char *title) {
    glfwSetErrorCallback([](int code, const char *message) {
        std::cerr << "GLFW " << code << ": " << message << '\n';
    });
    if (!glfwInit()) throw std::runtime_error("Failed to initialize GLFW.");
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
    glfwWindowHint(GLFW_SAMPLES, 4);
    window_ = glfwCreateWindow(1600, 900, title, nullptr, nullptr);
    if (!window_) {
        glfwTerminate();
        throw std::runtime_error("Failed to create the OpenGL window.");
    }
    glfwMakeContextCurrent(window_);
    glfwSwapInterval(1);
    glfwSetWindowUserPointer(window_, this);
    glfwSetKeyCallback(window_, keyCallback);
    glfwSetMouseButtonCallback(window_, buttonCallback);
    glfwSetCursorPosCallback(window_, cursorCallback);
    glfwSetScrollCallback(window_, scrollCallback);
    glfwGetCursorPos(window_, &cursor_x_, &cursor_y_);
}

Window::~Window() {
    glfwDestroyWindow(window_);
    glfwTerminate();
}

std::vector<InputEvent> Window::pollEvents() {
    glfwPollEvents();
    return std::exchange(events_, {});
}

bool Window::shouldClose() const {
    return glfwWindowShouldClose(window_) != 0;
}

bool Window::minimized() const {
    const auto size = framebufferSize();
    return size.width <= 0 || size.height <= 0 || glfwGetWindowAttrib(window_, GLFW_ICONIFIED) != 0;
}

WindowSize Window::framebufferSize() const {
    WindowSize size;
    glfwGetFramebufferSize(window_, &size.width, &size.height);
    return size;
}

WindowSize Window::logicalSize() const {
    WindowSize size;
    glfwGetWindowSize(window_, &size.width, &size.height);
    return size;
}

void Window::swapBuffers() {
    glfwSwapBuffers(window_);
}

void Window::waitEvents() {
    glfwWaitEventsTimeout(0.1);
}

Window &Window::self(GLFWwindow *window) {
    return *static_cast<Window *>(glfwGetWindowUserPointer(window));
}

void Window::keyCallback(GLFWwindow *window, int key, int, int action, int) {
    if (action != GLFW_PRESS) return;
    if (key == GLFW_KEY_ESCAPE) glfwSetWindowShouldClose(window, GLFW_TRUE);
    if (key == GLFW_KEY_SPACE) self(window).events_.push_back({EventType::TogglePause});
    if (key == GLFW_KEY_R) self(window).events_.push_back({EventType::ResetSimulation});
    if (key == GLFW_KEY_F) self(window).events_.push_back({EventType::ResetView});
    if (key == GLFW_KEY_BACKSPACE) self(window).events_.push_back({EventType::Cancel});
}

void Window::buttonCallback(GLFWwindow *window, int button, int action, int) {
    if (action != GLFW_PRESS) return;
    auto &state = self(window);
    glfwGetCursorPos(window, &state.cursor_x_, &state.cursor_y_);
    if (button == GLFW_MOUSE_BUTTON_LEFT) {
        state.left_origin_x_ = state.cursor_x_;
        state.left_origin_y_ = state.cursor_y_;
    }
    if (button == GLFW_MOUSE_BUTTON_RIGHT) {
        state.right_origin_x_ = state.cursor_x_;
        state.right_origin_y_ = state.cursor_y_;
    }
}

void Window::cursorCallback(GLFWwindow *window, double x, double y) {
    auto &state = self(window);
    const double dx = x - state.cursor_x_;
    const double dy = y - state.cursor_y_;
    state.cursor_x_ = x;
    state.cursor_y_ = y;
    if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS) {
        state.events_.push_back({EventType::Pan, dx, dy, state.right_origin_x_, state.right_origin_y_});
    } else if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS) {
        state.events_.push_back({EventType::Rotate, dx, dy, state.left_origin_x_, state.left_origin_y_});
    }
}

void Window::scrollCallback(GLFWwindow *window, double, double y) {
    auto &state = self(window);
    double x = 0.0;
    double cursor_y = 0.0;
    glfwGetCursorPos(window, &x, &cursor_y);
    state.events_.push_back({EventType::Zoom, 0, y, x, cursor_y});
}

} // namespace simlab
