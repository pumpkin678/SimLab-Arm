#include "options.hpp"

#include <stdexcept>
#include <string_view>

namespace simlab::benchmark {

Options parseOptions(int argc, const char *const *argv) {
    Options options;
    bool model_set = false;
    bool case_set = false;
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument = argv[index];
        if (argument == "--case" || argument == "--output") {
            if (index + 1 >= argc) throw std::invalid_argument("Missing value after " + std::string(argument));
            const std::string_view value = argv[++index];
            if (value.empty() || value.starts_with('-')) throw std::invalid_argument("Invalid value after " + std::string(argument));
            if (argument == "--case") {
                if (case_set) throw std::invalid_argument("Duplicate --case option.");
                options.selection = value;
                case_set = true;
            } else {
                if (options.output) throw std::invalid_argument("Duplicate --output option.");
                options.output = std::string(value);
            }
        } else if (argument == "--headless") {
            if (options.headless) throw std::invalid_argument("Duplicate --headless option.");
            options.headless = true;
        } else if (argument == "--help") {
            options.help = true;
        } else if (argument.empty() || argument.starts_with('-') || model_set) {
            throw std::invalid_argument("Unknown or duplicate benchmark argument: " + std::string(argument));
        } else {
            options.model = argument;
            model_set = true;
        }
    }
    if (!options.help && options.headless && !options.output) {
        throw std::invalid_argument("Headless execution requires --output; use tools/benchmark.ps1 to create a run.");
    }
    return options;
}

} // namespace simlab::benchmark
