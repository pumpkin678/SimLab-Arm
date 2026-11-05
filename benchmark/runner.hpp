#ifndef SIMLAB_BENCHMARK_RUNNER_HPP
#define SIMLAB_BENCHMARK_RUNNER_HPP

#include "cases/case.hpp"
#include "options.hpp"
#include "simulation/simulation.hpp"

#include <nlohmann/json.hpp>

namespace simlab::benchmark {

enum class Outcome { NotStarted, Running, Completed, Cancelled, Failed };
std::string_view outcomeName(Outcome outcome);

struct CaseReport {
    Outcome status{Outcome::NotStarted};
    std::uint64_t samples{};
    std::uint64_t expected_steps{};
    double timestep{};
    std::optional<JointTorqueLimits> torque_limits;
    std::string reason;
};

class Runner {
public:
    Runner(Options options, std::vector<CaseSpec> cases);
    void startNext(); // Release the borrowed Renderer before replacing the Simulation.
    void advanceOneCycle();
    void cancel(std::string reason);
    void fail(std::string reason);
    nlohmann::json result() const;
    void writeResult() const;

    bool terminal() const noexcept { return outcome_ != Outcome::Running; }
    bool caseComplete() const { return reports_[index_].status == Outcome::Completed; }
    bool recording() const noexcept { return options_.output.has_value(); }
    bool canRender() const;
    Outcome outcome() const noexcept { return outcome_; }
    const std::string &reason() const noexcept { return reason_; }
    const CaseSpec &spec() const { return cases_[index_]; }
    const std::vector<CaseReport> &reports() const noexcept { return reports_; }
    std::size_t caseIndex() const noexcept { return index_; }
    const JointCommand &lastCommand() const noexcept { return last_command_; }
    const PhaseSpec *lastPhase() const { return reports_[index_].samples ? phaseAt(spec(), last_sample_time_) : nullptr; }
    double lastSampleTime() const noexcept { return last_sample_time_; }
    Simulation *simulation() const noexcept { return simulation_.get(); }

private:
    void syncSamples();

    Options options_;
    std::vector<CaseSpec> cases_;
    std::vector<CaseReport> reports_;
    std::size_t index_{};
    Outcome outcome_{Outcome::Running};
    std::string reason_;
    std::unique_ptr<Simulation> simulation_;
    JointCommand last_command_{};
    double last_sample_time_{};
};

} // namespace simlab::benchmark

#endif
