#include "application.hpp"
#include "options.hpp"

#include <exception>
#include <iostream>

int main(int argc, char **argv) {
    try {
        const auto options = simlab::parseOptions(argc, argv);
        if (options.help) {
            std::cout << "Usage: simlab_arm [model.xml] [--case <name>] [--headless] [--trace <CSV path> [--force]]\n"
                         "Cases: joint_sine (8 s)\n";
            return 0;
        }
        if (options.headless) {
            simlab::Simulation simulation(options.model, options.trace);
            simlab::ReferenceRun reference(simulation, options.reference_case);
            while (reference.step()) {}
            simulation.flushTrace();
            std::cout << "Completed case " << options.reference_case->name << ": "
                      << reference.stepCount() << " physics steps, " << simulation.data().time << " s\n";
            return 0;
        }
        simlab::Application application(options.model, options.reference_case, options.trace);
        application.run();
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "SimLab-Arm: " << error.what() << '\n';
        return 1;
    }
}
