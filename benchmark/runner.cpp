#include "runner.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>
#include <set>
#include <stdexcept>
#include <utility>

namespace simlab::benchmark {

std::string_view outcomeName(Outcome outcome) {
    switch (outcome) {
    case Outcome::NotStarted: return "not_started";
    case Outcome::Running: return "running";
    case Outcome::Completed: return "completed";
    case Outcome::Cancelled: return "cancelled";
    case Outcome::Failed: return "failed";
    }
    throw std::logic_error("Unknown benchmark outcome.");
}

Runner::Runner(Options options, std::vector<CaseSpec> cases)
    : options_(std::move(options)),
      cases_(std::move(cases)),
      reports_(cases_.size()) {
    if (cases_.empty()) throw std::invalid_argument("Benchmark selection is empty.");
    if (options_.headless && !options_.output) throw std::invalid_argument("Headless execution requires an output directory.");
    if (options_.output && (!std::filesystem::is_directory(*options_.output) ||
        std::filesystem::exists(*options_.output / "result.json"))) {
        throw std::invalid_argument("Benchmark needs an existing output directory without result.json.");
    }
    std::set<std::string> ids;
    for (const auto &definition : cases_) {
        if (definition.id.empty() || definition.id == "." || definition.id == ".." ||
            definition.id.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.-") != std::string::npos ||
            !ids.insert(definition.id).second || !definition.evaluate ||
            !std::isfinite(definition.duration) || definition.duration <= 0) {
            throw std::invalid_argument("Invalid or duplicate benchmark case definition.");
        }
        std::set<std::string> parameter_names;
        for (const auto &parameter : definition.parameters) {
            if (parameter.name.empty() || !parameter_names.insert(parameter.name).second || !std::isfinite(parameter.value)) {
                throw std::invalid_argument("Invalid or duplicate case parameter.");
            }
        }
        std::set<std::string> phase_ids;
        double end = 0;
        for (const auto &phase : definition.phases) {
            if (phase.id.empty() || !phase_ids.insert(phase.id).second ||
                !std::isfinite(phase.start) || !std::isfinite(phase.end) ||
                phase.start != end || phase.end <= phase.start || phase.end > definition.duration) {
                throw std::invalid_argument("Invalid benchmark phase timeline.");
            }
            phaseKindName(phase.kind);
            end = phase.end;
        }
        if (!definition.phases.empty() && end != definition.duration) {
            throw std::invalid_argument("Benchmark phases must cover the case duration.");
        }
        if (options_.output && std::filesystem::exists(*options_.output / (definition.id + ".csv"))) {
            throw std::invalid_argument("Refusing to overwrite benchmark trace: " + definition.id);
        }
    }
}

void Runner::startNext() {
    if (terminal()) return;
    if (caseComplete()) ++index_;
    else if (reports_[index_].status != Outcome::NotStarted) throw std::logic_error("Current case is unfinished.");
    simulation_.reset();
    last_command_ = {};
    last_sample_time_ = 0;
    auto &report = reports_[index_];
    report.status = Outcome::Running;
    try {
        std::optional<TraceOptions> trace;
        if (options_.output) trace = TraceOptions{*options_.output / (spec().id + ".csv"), false};
        simulation_ = std::make_unique<Simulation>(options_.model, trace);
        report.timestep = simulation_->model().opt.timestep;
        const double steps = std::ceil(spec().duration / report.timestep);
        if (!std::isfinite(steps) || steps >= static_cast<double>(std::numeric_limits<std::uint64_t>::max())) {
            throw std::runtime_error("Case requires too many physics steps.");
        }
        report.expected_steps = static_cast<std::uint64_t>(steps);
        std::cout << spec().id << ": start (" << report.expected_steps << " steps)\n";
    } catch (const std::exception &error) {
        fail(error.what());
    }
}

void Runner::syncSamples() {
    if (simulation_) reports_[index_].samples = simulation_->stepCount();
}

void Runner::advanceOneCycle() {
    if (terminal() || !simulation_ || reports_[index_].status != Outcome::Running) {
        throw std::logic_error("No running benchmark case.");
    }
    try {
        const double sample_time = simulation_->data().time;
        const auto command = spec().evaluate(sample_time);
        simulation_->step(command);
        last_command_ = command;
        last_sample_time_ = sample_time;
        syncSamples();
        auto &report = reports_[index_];
        if (report.samples >= report.expected_steps) {
            simulation_->flushTrace();
            report.status = Outcome::Completed;
            report.reason = "Physics step budget completed";
            std::cout << spec().id << ": completed " << report.samples << " steps\n";
            if (index_ + 1 == cases_.size()) {
                outcome_ = Outcome::Completed;
                reason_ = "All selected cases completed";
            }
        }
    } catch (const std::exception &error) {
        fail(spec().id + ": " + error.what());
    }
}

void Runner::cancel(std::string reason) {
    if (terminal()) return;
    try {
        if (simulation_) simulation_->flushTrace();
        syncSamples();
        auto &report = reports_[index_];
        if (report.status == Outcome::Running) {
            report.status = Outcome::Cancelled;
            report.reason = reason;
        }
        outcome_ = Outcome::Cancelled;
        reason_ = std::move(reason);
        std::cout << "Benchmark cancelled: " << reason_ << '\n';
    } catch (const std::exception &error) {
        fail(std::string("Cancellation flush failed: ") + error.what());
    }
}

void Runner::fail(std::string reason) {
    if (outcome_ == Outcome::Failed) return;
    outcome_ = Outcome::Failed;
    reason_ = std::move(reason);
    syncSamples();
    auto &report = reports_[index_];
    if (report.status == Outcome::Running) report.status = Outcome::Failed;
    try {
        if (simulation_) simulation_->flushTrace();
    } catch (const std::exception &error) {
        reason_ += std::string("; trace flush: ") + error.what();
    }
    report.reason = reason_;
    std::cerr << "Benchmark failed: " << reason_ << '\n';
}

bool Runner::canRender() const {
    if (!simulation_) return false;
    const auto &model = simulation_->model();
    const auto &data = simulation_->data();
    const auto valid_joint = [](mjtNum value) { return std::isfinite(value) && std::abs(value) <= std::numeric_limits<float>::max(); };
    const auto finite = [](mjtNum value) { return std::isfinite(value); };
    return std::isfinite(data.time) && std::all_of(data.qpos, data.qpos + model.nq, valid_joint) &&
        std::all_of(data.qvel, data.qvel + model.nv, valid_joint) &&
        std::all_of(data.xpos, data.xpos + 3 * model.nbody, finite) &&
        std::all_of(data.xmat, data.xmat + 9 * model.nbody, finite) &&
        std::all_of(data.geom_xpos, data.geom_xpos + 3 * model.ngeom, finite) &&
        std::all_of(data.geom_xmat, data.geom_xmat + 9 * model.ngeom, finite) &&
        std::all_of(data.site_xpos, data.site_xpos + 3 * model.nsite, finite) &&
        std::all_of(data.site_xmat, data.site_xmat + 9 * model.nsite, finite);
}

nlohmann::json Runner::result() const {
    nlohmann::json reports = nlohmann::json::array();
    for (std::size_t index = 0; index < cases_.size(); ++index) {
        const auto &definition = cases_[index];
        nlohmann::json parameters = nlohmann::json::object();
        for (const auto &parameter : definition.parameters) {
            parameters[parameter.name] = {{"value", parameter.value}, {"unit", parameter.unit}};
        }
        nlohmann::json entry = {{"id", definition.id}, {"duration_s", definition.duration},
            {"trace", definition.id + ".csv"}, {"parameters", std::move(parameters)}};
        entry["phases"] = nlohmann::json::array();
        for (const auto &phase : definition.phases) {
            std::vector<int> active;
            for (std::size_t joint = 0; joint < phase.active_joints.size(); ++joint) {
                if (phase.active_joints[joint]) active.push_back(static_cast<int>(joint + 1));
            }
            entry["phases"].push_back({{"id", phase.id}, {"start_s", phase.start}, {"end_s", phase.end},
                {"kind", phaseKindName(phase.kind)}, {"active_joints", active}});
        }
        const auto &report = reports_[index];
        entry["status"] = outcomeName(report.status);
        entry["reason"] = report.reason;
        entry["samples"] = report.samples;
        entry["last_step"] = report.samples ? nlohmann::json(report.samples - 1) : nlohmann::json(nullptr);
        entry["expected_steps"] = report.expected_steps ? nlohmann::json(report.expected_steps) : nlohmann::json(nullptr);
        entry["timestep_s"] = report.timestep > 0 ? nlohmann::json(report.timestep) : nlohmann::json(nullptr);
        reports.push_back(std::move(entry));
    }
    return {{"format_version", 1}, {"trace_version", 1}, {"selection", options_.selection},
        {"model", options_.model}, {"mode", options_.headless ? "headless" : "gui"},
        {"status", outcomeName(outcome_)}, {"reason", reason_},
        {"runtime", {{"mujoco", mj_versionString()}, {"configuration", SIMLAB_BENCHMARK_CONFIGURATION}}},
        {"cases", std::move(reports)}};
}

void Runner::writeResult() const {
    if (!terminal()) throw std::logic_error("Cannot finalize a running benchmark.");
    if (!options_.output) return;
    const auto path = *options_.output / "result.json";
    if (std::filesystem::exists(path)) throw std::runtime_error("Refusing to overwrite result.json.");
    std::ofstream output;
    output.exceptions(std::ios::failbit | std::ios::badbit);
    output.open(path);
    output << result().dump(2) << '\n';
    output.close();
}

} // namespace simlab::benchmark
