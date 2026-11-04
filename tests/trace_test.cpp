#include "app/options.hpp"
#include "simulation/simulation.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using Row = std::map<std::string, std::string>;

const std::vector<std::string> kHeader{
    "trace_version", "reset_index", "step_index", "simulation_time_s", "cycle_timestep_s",
    "input_j1_position_rad", "input_j2_position_rad", "input_j3_position_rad",
    "input_j4_position_rad", "input_j5_position_rad", "input_j6_position_rad",
    "feedback_j1_position_rad", "feedback_j2_position_rad", "feedback_j3_position_rad",
    "feedback_j4_position_rad", "feedback_j5_position_rad", "feedback_j6_position_rad",
    "feedback_j1_velocity_radps", "feedback_j2_velocity_radps", "feedback_j3_velocity_radps",
    "feedback_j4_velocity_radps", "feedback_j5_velocity_radps", "feedback_j6_velocity_radps",
    "request_j1_torque_nm", "request_j2_torque_nm", "request_j3_torque_nm",
    "request_j4_torque_nm", "request_j5_torque_nm", "request_j6_torque_nm",
    "applied_j1_torque_nm", "applied_j2_torque_nm", "applied_j3_torque_nm",
    "applied_j4_torque_nm", "applied_j5_torque_nm", "applied_j6_torque_nm"};

struct ArmCall {
    simlab::JointFeedback feedback;
    simlab::JointCommand command;
    simlab::ArmContext context;
};

enum class Fault { None, TimeReset, NumericalWarning };

ArmCall last_call;
simlab::CycleResult next_result;
std::size_t updates{};
Fault fault{};
mjData *fault_data{};

void require(bool condition, const std::string &message) {
    if (!condition) throw std::runtime_error(message);
}

template <class Operation>
void requireThrows(Operation operation) {
    bool caught = false;
    try {
        operation();
    } catch (const std::exception &) {
        caught = true;
    }
    require(caught, "Expected operation to fail");
}

std::vector<std::string> split(const std::string &line) {
    std::istringstream input(line);
    std::vector<std::string> values;
    std::string value;
    while (std::getline(input, value, ',')) values.push_back(value);
    return values;
}

std::vector<Row> readCsv(const std::filesystem::path &path) {
    std::ifstream input(path, std::ios::binary);
    require(static_cast<bool>(input), "Missing trace file");
    std::string line;
    require(static_cast<bool>(std::getline(input, line)), "Missing trace header");
    const auto header = split(line);
    require(header == kHeader, "Trace v1 header/order differs from the 35-column contract");
    std::vector<Row> rows;
    while (std::getline(input, line)) {
        const auto values = split(line);
        require(values.size() == header.size(), "Incomplete trace row");
        Row row;
        for (std::size_t index = 0; index < header.size(); ++index) {
            require(std::isfinite(std::stod(values[index])), "Non-finite CSV field");
            row.emplace(header[index], values[index]);
        }
        rows.push_back(std::move(row));
    }
    return rows;
}

void equalFloat(const Row &row, const std::string &name, float expected) {
    const float actual = std::stof(row.at(name));
    require(actual == expected && std::signbit(actual) == std::signbit(expected), "Float round-trip: " + name);
}

simlab::Options parse(std::vector<std::string> arguments) {
    std::vector<char *> pointers;
    for (auto &argument : arguments) pointers.push_back(argument.data());
    return simlab::parseOptions(static_cast<int>(pointers.size()), pointers.data());
}

void checkOptions() {
    require(!parse({"simlab"}).trace, "Trace must default off");
    const auto options = parse({"simlab", "model with spaces.xml", "--force", "--trace", "trace with spaces.csv"});
    require(options.model == "model with spaces.xml", "Model option changed");
    require(options.trace && options.trace->path == "trace with spaces.csv" && options.trace->force,
        "Trace/force parsing");
    for (const auto &arguments : std::vector<std::vector<std::string>>{
        {"simlab", "--trace"}, {"simlab", "--trace", ""}, {"simlab", "--trace", "--force"},
        {"simlab", "--trace", "a.csv", "--trace", "b.csv"}, {"simlab", "--force"},
        {"simlab", "--trace", "a.csv", "--force", "--force"}, {"simlab", "--headless"},
        {"simlab", "--case", "missing"}, {"simlab", "--unknown"}}) {
        requireThrows([&] { parse(arguments); });
    }
}

void checkFiles(const std::filesystem::path &directory) {
    const auto path = directory / "nested directory" / "trace with spaces.csv";
    {
        simlab::TraceWriter writer({path, false});
        writer.flush();
    }
    require(readCsv(path).empty(), "Zero-sample file must contain only its header");
    { std::ofstream sentinel(path, std::ios::app); sentinel << "preserve\n"; }
    const auto old_size = std::filesystem::file_size(path);
    requireThrows([&] { simlab::TraceWriter writer({path, false}); });
    require(std::filesystem::file_size(path) == old_size, "Existing file was changed without force");
    {
        simlab::TraceWriter writer({path, true});
        writer.flush();
    }
    require(readCsv(path).empty(), "Force did not replace file contents");
    requireThrows([] { simlab::TraceWriter writer({{}, false}); });
    requireThrows([&] { simlab::TraceWriter writer({directory, true}); });
    const auto blocker = directory / "parent-is-file";
    { std::ofstream file(blocker); file << "preserve"; }
    requireThrows([&] { simlab::TraceWriter writer({blocker / "trace.csv", false}); });
}

void checkRoundTrip(const std::filesystem::path &directory) {
    const auto path = directory / "round-trip.csv";
    const double time = std::nextafter(0.1, 1.0);
    const simlab::ArmContext context{0.1F, std::nextafter(0.001F, 1.0F)};
    const simlab::JointCommand command{{std::nextafter(0.1F, 1.0F), -0.0F, -0.3F, 0.4F, -0.5F, 0.6F}};
    const simlab::JointFeedback feedback{{1.1F, -1.2F, 1.3F, -1.4F, 1.5F, -1.6F},
        {-2.1F, 2.2F, -2.3F, 2.4F, -2.5F, 2.6F}};
    const simlab::CycleResult result{{3.1F, -3.2F, 3.3F, -3.4F, 3.5F, -3.6F}};
    const simlab::JointTorques applied{-4.1F, 4.2F, -4.3F, 4.4F, -4.5F, 4.6F};
    {
        simlab::TraceWriter writer({path, false});
        writer.write(9007199254740993ULL, time, context, command, feedback, result, applied);
        writer.flush();
    }
    const auto rows = readCsv(path);
    require(rows.size() == 1, "Wrong round-trip row count");
    const auto &row = rows.front();
    require(row.at("trace_version") == "1" && row.at("reset_index") == "0", "Trace metadata");
    require(row.at("step_index") == "9007199254740993", "Integer step lost precision");
    require(std::stod(row.at("simulation_time_s")) == time, "Double time lost precision");
    equalFloat(row, "cycle_timestep_s", context.dt);
    for (std::size_t index = 0; index < 6; ++index) {
        const auto joint = "_j" + std::to_string(index + 1);
        equalFloat(row, "input" + joint + "_position_rad", command.position[index]);
        equalFloat(row, "feedback" + joint + "_position_rad", feedback.position[index]);
        equalFloat(row, "feedback" + joint + "_velocity_radps", feedback.velocity[index]);
        equalFloat(row, "request" + joint + "_torque_nm", result.torques[index]);
        equalFloat(row, "applied" + joint + "_torque_nm", applied[index]);
    }
}

void checkSimulation(const std::string &model, const std::filesystem::path &directory) {
    const auto path = directory / "simulation.csv";
    next_result = {};
    simlab::Simulation simulation(model, simlab::TraceOptions{path, false});
    simulation.flushTrace();
    require(readCsv(path).empty() && simulation.stepCount() == 0, "Initial trace/step state");
    const auto calls = updates;
    simulation.readFeedback();
    simulation.result();
    simulation.lastTorques();
    require(updates == calls && simulation.stepCount() == 0, "Readout advances simulation");
    next_result.torques = {25.125F, -85.25F, 3.375F, -4.5F, 11.625F, -3.75F};
    next_result.torque_saturated[0] = true;
    const simlab::JointCommand command{{std::nextafter(0.1F, 1.0F), 0.2F, -0.3F, 0.4F, -0.5F, 0.6F}};
    simulation.step(command);
    simulation.flushTrace();
    const auto rows = readCsv(path);
    require(rows.size() == 1 && updates == calls + 1 && simulation.stepCount() == 1, "One cycle, one row, one Arm call");
    const auto &row = rows.front();
    require(std::stod(row.at("simulation_time_s")) == 0 && simulation.data().time > 0, "Feedback time must be pre-step");
    require(last_call.command.position == command.position, "Arm did not receive recorded target");
    equalFloat(row, "cycle_timestep_s", last_call.context.dt);
    for (std::size_t index = 0; index < 6; ++index) {
        const auto joint = "_j" + std::to_string(index + 1);
        equalFloat(row, "input" + joint + "_position_rad", last_call.command.position[index]);
        equalFloat(row, "feedback" + joint + "_position_rad", last_call.feedback.position[index]);
        equalFloat(row, "feedback" + joint + "_velocity_radps", last_call.feedback.velocity[index]);
        equalFloat(row, "request" + joint + "_torque_nm", next_result.torques[index]);
        equalFloat(row, "applied" + joint + "_torque_nm", simulation.lastTorques()[index]);
        const auto name = "motor" + std::to_string(index + 1);
        const int motor = mj_name2id(&simulation.model(), mjOBJ_ACTUATOR, name.c_str());
        const float limited = std::clamp(next_result.torques[index],
            static_cast<float>(simulation.model().actuator_forcerange[2 * motor]),
            static_cast<float>(simulation.model().actuator_forcerange[2 * motor + 1]));
        require(simulation.lastTorques()[index] == limited, "Applied torque not limited by model");
        require(simulation.result().torque_saturated[index] ==
            (next_result.torque_saturated[index] || std::abs(next_result.torques[index] - limited) > 1e-4F),
            "Business and model saturation flags were not merged");
    }
    next_result = {};
    simulation.step({});
    require(simulation.result().torque_saturated == std::array<bool, 6>{}, "Saturation flags were latched");
    next_result.torque_saturated.fill(true);
    simulation.step({});
    simulation.reset();
    simulation.reset();
    require(simulation.stepCount() == 0 && simulation.data().time == 0, "Reset cycle clock");
    require(simulation.result().torque_saturated == std::array<bool, 6>{}, "Reset retained saturation flags");
    next_result = {};
    simulation.step({});
    simulation.flushTrace();
    const auto reset_rows = readCsv(path);
    require(reset_rows.size() == 4 && reset_rows.back().at("reset_index") == "2" &&
        reset_rows.back().at("step_index") == "0", "Empty reset segments or first step changed");
}

void checkEquivalence(const std::string &model, const std::filesystem::path &directory) {
    const auto path = directory / "equivalent.csv";
    next_result = {};
    simlab::Simulation recorded(model, simlab::TraceOptions{path, false});
    simlab::Simulation plain(model);
    for (int cycle = 0; cycle < 100; ++cycle) {
        const simlab::JointCommand command{{0.001F * cycle, 0, 0, 0, 0, 0}};
        recorded.step(command);
        plain.step(command);
        for (int index = 0; index < 6; ++index) {
            require(recorded.data().qpos[index] == plain.data().qpos[index] &&
                recorded.data().qvel[index] == plain.data().qvel[index], "Recording changes physics");
        }
    }
    require(readCsv(path).size() == 100, "Periodic flush did not publish 100 rows");
    require(recorded.stepCount() == plain.stepCount(), "Recording changes successful step count");
}

simlab::JointCommand invalidReference(double) {
    return {{std::numeric_limits<float>::quiet_NaN(), 0, 0, 0, 0, 0}};
}

void requireZeroControl(const simlab::Simulation &simulation) {
    for (int index = 0; index < simulation.model().nu; ++index) {
        require(simulation.data().ctrl[index] == 0, "Failed cycle did not clear control");
    }
}

void checkFailures(const std::string &model, const std::filesystem::path &directory) {
    const auto path = directory / "failed-cycles.csv";
    next_result = {};
    simlab::Simulation simulation(model, simlab::TraceOptions{path, false});
    next_result.torques[5] = std::numeric_limits<float>::infinity();
    requireThrows([&] { simulation.step({}); });
    requireZeroControl(simulation);
    require(simulation.stepCount() == 0 && simulation.data().time == 0, "Failed output advanced cycle");
    next_result = {};
    simulation.data().qvel[1] = std::numeric_limits<double>::quiet_NaN();
    const auto calls = updates;
    requireThrows([&] { simulation.step({}); });
    require(updates == calls, "Invalid feedback reached Arm");
    requireZeroControl(simulation);
    simulation.reset();
    simulation.data().time = std::numeric_limits<double>::infinity();
    requireThrows([&] { simulation.step({}); });
    simulation.reset();
    simulation.flushTrace();
    require(readCsv(path).empty(), "Failed cycle generated a trace row");
    requireThrows([&] { simulation.step(invalidReference(0)); });
    require(simulation.stepCount() == 0, "Invalid reference advanced cycle");

    for (const auto failure : {Fault::TimeReset, Fault::NumericalWarning}) {
        simulation.reset();
        fault = Fault::None;
        next_result.torque_saturated.fill(true);
        simulation.step({});
        const auto successful = simulation.stepCount();
        const auto result = simulation.result();
        next_result.torque_saturated.fill(false);
        fault = failure;
        fault_data = &simulation.data();
        requireThrows([&] { simulation.step({}); });
        fault = Fault::None;
        fault_data = nullptr;
        requireZeroControl(simulation);
        require(simulation.stepCount() == successful && simulation.result().torques == result.torques &&
            simulation.result().torque_saturated == result.torque_saturated,
            "Failed engine cycle was published");
    }
    simulation.flushTrace();
    require(readCsv(path).size() == 2, "Engine failures generated extra rows");
}

void checkCleanup(const std::string &model, const std::filesystem::path &directory) {
    next_result = {};
    const auto unwind_path = directory / "unwind.csv";
    try {
        simlab::Simulation unwinding(model, simlab::TraceOptions{unwind_path, false});
        unwinding.step({});
        throw std::runtime_error("original error");
    } catch (const std::exception &error) {
        require(std::string(error.what()) == "original error", "Cleanup replaced original exception");
    }
    require(readCsv(unwind_path).size() == 1, "Cleanup lost the last completed row");
}

} // namespace

namespace simlab {

// Test-only link-time stand-in observes the real Simulation boundary and controls its result.
CycleResult Arm::update(const JointFeedback &feedback, const JointCommand &command, const ArmContext &context) {
    last_call = {feedback, command, context};
    ++updates;
    if (fault == Fault::TimeReset) fault_data->time = 0;
    if (fault == Fault::NumericalWarning) ++fault_data->warning[mjWARN_BADQACC].number;
    return next_result;
}

void Arm::reset() {}

} // namespace simlab

int main(int argc, char **argv) {
    try {
        require(argc == 3, "Expected model and test output root");
        const auto suffix = std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
        const auto directory = std::filesystem::path(argv[2]) / suffix;
        require(std::filesystem::create_directories(directory), "Test output directory already exists");
        checkOptions();
        checkFiles(directory);
        checkRoundTrip(directory);
        checkSimulation(argv[1], directory);
        checkEquivalence(argv[1], directory);
        checkFailures(argv[1], directory);
        checkCleanup(argv[1], directory);
        std::cout << "PASS trace contract: options, 35 columns, real cycle pairing, reset, cleanup, "
                     "recording equivalence, failure rejection and file lifecycle\nArtifacts: " << directory << '\n';
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
