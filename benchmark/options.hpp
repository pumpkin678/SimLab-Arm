#ifndef SIMLAB_BENCHMARK_OPTIONS_HPP
#define SIMLAB_BENCHMARK_OPTIONS_HPP

#include <filesystem>
#include <optional>
#include <string>

namespace simlab::benchmark {

struct Options {
    std::string model{"assets/description/scene.xml"};
    std::string selection{"all"};
    std::optional<std::filesystem::path> output;
    bool headless{};
    bool help{};
};

Options parseOptions(int argc, const char *const *argv);

} // namespace simlab::benchmark

#endif
