#include "benchmark/runner.hpp"

#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

namespace {

using namespace simlab::benchmark;

struct Call {
    simlab::JointFeedback feedback;
    simlab::JointCommand command;
    simlab::ArmContext context;
};

std::vector<Call> calls;
int evaluations{};
int resets{};

void require(bool condition, const char *message) {
    if (!condition) throw std::runtime_error(message);
}

template <class Operation>
void requireThrows(Operation operation) {
    bool caught = false;
    try { operation(); } catch (const std::exception &) { caught = true; }
    require(caught, "Expected operation to fail");
}

simlab::JointCommand reference(double time) {
    ++evaluations;
    return {{static_cast<float>(time), 0.25F, 0, 0, 0, 0}};
}

simlab::JointCommand invalid(double) {
    return {{std::numeric_limits<float>::quiet_NaN(), 0, 0, 0, 0, 0}};
}

CaseSpec definition(std::string id, double duration = 0.0025) {
    return {std::move(id), duration, {{"test_value", "rad", 0.25}}, reference};
}

Options options(const std::string &model, const std::filesystem::path &output) {
    Options value;
    value.model = model;
    if (!output.empty()) {
        std::filesystem::create_directories(output);
        value.output = output;
        value.headless = true;
    }
    return value;
}

std::size_t rowCount(const std::filesystem::path &path) {
    std::ifstream input(path);
    require(static_cast<bool>(input), "Missing recorded trace");
    std::string line;
    std::size_t lines = 0;
    while (std::getline(input, line)) ++lines;
    require(lines > 0, "Missing trace header");
    return lines - 1;
}

Options parse(std::vector<std::string> arguments) {
    std::vector<const char *> pointers;
    for (const auto &argument : arguments) pointers.push_back(argument.c_str());
    return parseOptions(static_cast<int>(pointers.size()), pointers.data());
}

void checkOptions() {
    require(selectCases("all").size() == 1 && selectCases("joint_sine")[0].id == "joint_sine", "Case selection");
    requireThrows([] { selectCases("missing"); });
    const auto preview = parse({"benchmark"});
    require(!preview.output && !preview.headless && preview.selection == "all", "Preview must not record");
    const auto headless = parse({"benchmark", "model with spaces.xml", "--case", "joint_sine", "--headless", "--output", "run with spaces"});
    require(headless.headless && headless.output == "run with spaces" && headless.model == "model with spaces.xml", "Headless options");
    for (const auto &arguments : std::vector<std::vector<std::string>>{
        {"benchmark", "--headless"}, {"benchmark", "--output"}, {"benchmark", "--case", ""},
        {"benchmark", "--output", "a", "--output", "b"}, {"benchmark", "--case", "a", "--case", "b"},
        {"benchmark", "--describe"}, {"benchmark", "--unknown"}}) {
        requireThrows([&] { parse(arguments); });
    }
    Runner runner(Options{}, selectCases("all"));
    const auto metadata = runner.result()["cases"][0];
    require(metadata["parameters"]["period"]["value"] == 4.0, "Case metadata does not use actual parameters");
}

std::vector<Call> runBatched(const std::string &model, const std::filesystem::path &directory, int batch) {
    const int before_resets = resets;
    Runner runner(options(model, directory), {definition("first"), definition("second", 0.002)});
    evaluations = 0;
    calls.clear();
    runner.startNext();
    while (!runner.terminal()) {
        if (runner.caseComplete()) {
            runner.startNext();
            continue;
        }
        runner.result();
        runner.lastCommand();
        require(evaluations == static_cast<int>(calls.size()), "Display/report reevaluates reference");
        for (int index = 0; index < batch && !runner.terminal() && !runner.caseComplete(); ++index) runner.advanceOneCycle();
    }
    require(evaluations == 5 && calls.size() == 5 && resets == before_resets + 2, "Single evaluation/update or per-case initialization");
    require(calls[0].context.time == 0 && calls[3].context.time == 0, "Case time did not restart");
    require(calls[0].feedback.position == calls[3].feedback.position && calls[0].feedback.velocity == calls[3].feedback.velocity,
        "Cases did not start from identical zero state");
    require(runner.outcome() == Outcome::Completed && runner.reports()[0].samples == 3 && runner.reports()[1].samples == 2,
        "Case budgets or outcome mismatch");
    require(rowCount(directory / "first.csv") == 3 && rowCount(directory / "second.csv") == 2, "Completion did not flush");
    runner.cancel("window closed");
    require(runner.outcome() == Outcome::Completed, "Close changed completed outcome");
    runner.writeResult();
    std::ifstream result_file(directory / "result.json");
    const auto result = nlohmann::json::parse(result_file);
    require(result["status"] == "completed" && result["cases"][0]["expected_steps"] == 3 &&
        result["cases"][1]["last_step"] == 1 && result["cases"][0]["parameters"]["test_value"]["unit"] == "rad",
        "Result/parameters do not match actual execution");
    requireThrows([&] { runner.writeResult(); });
    requireThrows([&] { Runner duplicate(options(model, directory), {definition("first")}); });
    return calls;
}

void checkCancellation(const std::string &model, const std::filesystem::path &directory) {
    for (int stage = 0; stage < 3; ++stage) {
        const auto output = directory / std::to_string(stage);
        Runner runner(options(model, output), {definition("first"), definition("second")});
        runner.startNext();
        const int count = stage == 0 ? 0 : (stage == 1 ? 1 : 3);
        for (int step = 0; step < count; ++step) runner.advanceOneCycle();
        runner.cancel("cancelled for test");
        runner.startNext();
        require(runner.outcome() == Outcome::Cancelled && runner.reports()[0].samples == static_cast<std::uint64_t>(count), "Cancel samples");
        require(runner.reports()[1].status == Outcome::NotStarted && !std::filesystem::exists(output / "second.csv"), "Cancellation started another case");
        require(runner.reports()[0].status == (stage == 2 ? Outcome::Completed : Outcome::Cancelled), "Cancellation changed finished case");
        require(rowCount(output / "first.csv") == static_cast<std::size_t>(count), "Cancellation did not flush partial data");
        runner.writeResult();
    }
}

void checkFailure(const std::string &model, const std::filesystem::path &directory) {
    auto bad = definition("bad");
    bad.evaluate = [](double time) { return time == 0 ? reference(time) : invalid(time); };
    Runner runner(options(model, directory), {definition("first", 0.001), bad, definition("never")});
    runner.startNext();
    runner.advanceOneCycle();
    runner.startNext();
    runner.advanceOneCycle();
    runner.advanceOneCycle();
    require(runner.outcome() == Outcome::Failed && runner.reports()[0].status == Outcome::Completed &&
        runner.reports()[1].status == Outcome::Failed && runner.reports()[1].samples == 1 &&
        runner.reports()[2].status == Outcome::NotStarted, "Failure statuses");
    require(rowCount(directory / "bad.csv") == 1 && !std::filesystem::exists(directory / "never.csv"), "Failure published a sample or started next case");
    runner.cancel("window closed");
    require(runner.outcome() == Outcome::Failed, "Close changed failure status");
    runner.writeResult();
    require(runner.result()["cases"][1]["last_step"] == 0, "Failure result lost the last successful cycle");

    Runner missing(options(model + ".missing", {}), {definition("missing")});
    missing.startNext();
    require(missing.terminal() && missing.outcome() == Outcome::Failed && !missing.canRender(), "Model-load failure was not captured");
    Runner preview(options(model, {}), {definition("preview", 0.001)});
    require(!preview.recording(), "Preview unexpectedly recording");
    requireThrows([&] { preview.writeResult(); });
    preview.startNext();
    preview.advanceOneCycle();
    preview.writeResult();
}

} // namespace

namespace simlab {

CycleResult Arm::update(const JointFeedback &feedback, const JointCommand &command, const ArmContext &context) {
    calls.push_back({feedback, command, context});
    return {};
}

void Arm::reset() { ++resets; }

} // namespace simlab

int main(int argc, char **argv) {
    try {
        require(argc == 3, "Provide model and test root");
        const auto directory = std::filesystem::path(argv[2]) /
            std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
        require(std::filesystem::create_directories(directory), "Output already exists");
        checkOptions();
        const auto single = runBatched(argv[1], directory / "single", 1);
        const auto batched = runBatched(argv[1], directory / "batched", 7);
        require(single.size() == batched.size(), "Batch count mismatch");
        for (std::size_t index = 0; index < single.size(); ++index) {
            require(single[index].command.position == batched[index].command.position &&
                single[index].feedback.position == batched[index].feedback.position &&
                single[index].context.time == batched[index].context.time, "Frame batch changes execution");
        }
        checkCancellation(argv[1], directory / "cancel");
        checkFailure(argv[1], directory / "failure");
        std::cout << "PASS benchmark contract: metadata, selection, fresh cases, batch equivalence, cancellation, failure and result lifecycle\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
