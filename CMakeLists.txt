cmake_minimum_required(VERSION 3.24)
project(simlab_arm LANGUAGES C CXX)

if(NOT WIN32 OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8 OR NOT MSVC)
    message(FATAL_ERROR "SimLab-Arm requires Windows x64 and MSVC. Use tools/build.ps1.")
endif()
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin" CACHE PATH "Executable output directory")

foreach(dependency IN ITEMS glfw imgui)
    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/${dependency}/.git")
        message(FATAL_ERROR "Missing ${dependency} submodule. Run git submodule update --init --recursive.")
    endif()
endforeach()
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/glfw)
add_subdirectory(third_party/mujoco)
find_package(OpenGL REQUIRED)

add_library(simlab_simulation STATIC
    simulation/simulation.cpp simulation/adapter.cpp simulation/trace.cpp simulation/arm/arm.cpp)
target_include_directories(simlab_simulation PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(simlab_simulation PUBLIC mujoco::mujoco)

add_library(simlab_viewer STATIC
    core/window.cpp core/renderer.cpp app/ui/ui.cpp
    third_party/imgui/imgui.cpp third_party/imgui/imgui_draw.cpp
    third_party/imgui/imgui_tables.cpp third_party/imgui/imgui_widgets.cpp
    third_party/imgui/backends/imgui_impl_glfw.cpp
    third_party/imgui/backends/imgui_impl_opengl2.cpp)
target_include_directories(simlab_viewer PUBLIC third_party/imgui)
target_link_libraries(simlab_viewer PUBLIC simlab_simulation glfw OpenGL::GL)

add_executable(simlab_arm app/main.cpp app/application.cpp app/options.cpp app/reference/reference.cpp)
target_link_libraries(simlab_arm PRIVATE simlab_viewer)
foreach(target IN ITEMS simlab_simulation simlab_viewer simlab_arm)
    target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8)
endforeach()
add_custom_command(TARGET simlab_arm POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "$<TARGET_FILE:mujoco::mujoco>" "$<TARGET_FILE_DIR:simlab_arm>"
    VERBATIM)

option(BUILD_TESTING "Build the trace contract check" ON)
if(BUILD_TESTING)
    enable_testing()
    add_executable(simlab_trace_test
        tests/trace_test.cpp app/options.cpp app/reference/reference.cpp)
    target_link_libraries(simlab_trace_test PRIVATE simlab_simulation)
    target_compile_options(simlab_trace_test PRIVATE /W4 /permissive- /utf-8)
    add_custom_command(TARGET simlab_trace_test POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "$<TARGET_FILE:mujoco::mujoco>" "$<TARGET_FILE_DIR:simlab_trace_test>"
        VERBATIM)
    add_test(NAME trace COMMAND simlab_trace_test
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/description/scene.xml"
        "${CMAKE_CURRENT_BINARY_DIR}/test-output/trace")
endif()
