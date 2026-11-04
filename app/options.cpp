#include "options.hpp"

#include <stdexcept>
#include <string_view>

namespace simlab {

Options parseOptions(int argc, char **argv) {
    Options options;
    bool model_supplied = false;
    bool force = false;
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument = argv[index];
        if (argument == "--case" || argument == "--headless") {
            throw std::runtime_error("Case execution moved to tools/benchmark.ps1 (simlab_benchmark).");
        } else if (argument == "--trace") {
            if (options.trace || index + 1 >= argc) {
                throw std::runtime_error("Provide exactly one path after --trace.");
            }
            const std::string_view path = argv[++index];
            if (path.empty() || path.starts_with('-')) {
                throw std::runtime_error("Provide a non-empty path after --trace.");
            }
            options.trace = TraceOptions{std::string(path), false};
        } else if (argument == "--force") {
            if (force) throw std::runtime_error("Duplicate --force option.");
            force = true;
        } else if (argument == "--help") {
            options.help = true;
        } else if (argument.starts_with('-')) {
            throw std::runtime_error("Unknown option: " + std::string(argument));
        } else {
            if (model_supplied) throw std::runtime_error("Provide at most one model path.");
            options.model = argument;
            model_supplied = true;
        }
    }
    if (force && !options.trace) throw std::runtime_error("--force requires --trace <path>.");
    if (options.trace) options.trace->force = force;
    return options;
}

} // namespace simlab
