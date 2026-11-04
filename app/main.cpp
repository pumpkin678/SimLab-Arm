#include "application.hpp"
#include "options.hpp"

#include <exception>
#include <iostream>

int main(int argc, char **argv) {
    try {
        const auto options = simlab::parseOptions(argc, argv);
        if (options.help) {
            std::cout << "Usage: simlab_arm [model.xml] [--trace <CSV path> [--force]]\n"
                         "Program references: tools/benchmark.ps1\n";
            return 0;
        }
        simlab::Application application(options.model, options.trace);
        application.run();
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "SimLab-Arm: " << error.what() << '\n';
        return 1;
    }
}
