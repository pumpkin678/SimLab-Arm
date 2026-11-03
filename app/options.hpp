#ifndef SIMLAB_APP_OPTIONS_HPP
#define SIMLAB_APP_OPTIONS_HPP

#include "app/reference/reference.hpp"

#include <string>

namespace simlab {

struct Options {
    std::string model{"assets/description/scene.xml"};
    const ReferenceCase *reference_case{};
    bool headless{};
    bool help{};
};

Options parseOptions(int argc, char **argv);

} // namespace simlab

#endif
