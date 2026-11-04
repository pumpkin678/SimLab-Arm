#include "ui/gui.hpp"

#include <iostream>
#include <stdexcept>

int main(int argc, char **argv) {
    namespace benchmark = simlab::benchmark;
    try {
        const auto options = benchmark::parseOptions(argc, argv);
        if (options.help) {
            std::cout << "Usage: simlab_benchmark [model.xml] [--case <id|all>] [--output <directory>] [--headless]\n"
                         "Create recorded runs with tools/benchmark.ps1.\n";
            return 0;
        }
        auto cases = benchmark::selectCases(options.selection);
        benchmark::Runner runner(options, std::move(cases));
        runner.startNext();
        try {
            if (options.headless) {
                while (!runner.terminal()) {
                    if (runner.caseComplete()) runner.startNext();
                    else runner.advanceOneCycle();
                }
            } else benchmark::runGui(runner);
        } catch (const std::exception &error) {
            runner.fail(error.what());
        }
        runner.writeResult();
        return runner.outcome() == benchmark::Outcome::Failed ? 1 : 0;
    } catch (const std::invalid_argument &error) {
        std::cerr << error.what() << '\n';
        return 2;
    } catch (const std::exception &error) {
        std::cerr << "Benchmark could not finalize: " << error.what() << '\n';
        return 1;
    }
}
