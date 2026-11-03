#include "application.hpp"

#include <exception>
#include <iostream>
#include <string>

int main(int argc, char **argv) {
    try {
        const std::string model = argc > 1 ? argv[1] : "assets/description/scene.xml";
        simlab::Application application(model);
        application.run();
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "SimLab-Arm: " << error.what() << '\n';
        return 1;
    }
}
