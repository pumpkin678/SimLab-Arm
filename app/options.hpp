#ifndef SIMLAB_APP_OPTIONS_HPP
#define SIMLAB_APP_OPTIONS_HPP

#include "simulation/trace.hpp"

#include <optional>
#include <string>

namespace simlab {

struct Options {
    std::string model{"assets/description/scene.xml"};
    std::optional<TraceOptions> trace;
    bool help{};
};

Options parseOptions(int argc, char **argv);

} // namespace simlab

#endif
