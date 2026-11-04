#ifndef SIMLAB_BENCHMARK_CASES_CASE_HPP
#define SIMLAB_BENCHMARK_CASES_CASE_HPP

#include "simulation/arm/types.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace simlab::benchmark {

struct Parameter {
    std::string name;
    std::string unit;
    double value;
};

enum class PhaseKind { Initialization, Response, Hold, Tracking };
std::string_view phaseKindName(PhaseKind kind);

struct PhaseSpec {
    std::string id;
    double start;
    double end;
    PhaseKind kind;
    std::array<bool, 6> active_joints{};
};

struct CaseSpec {
    std::string id;
    double duration;
    std::vector<Parameter> parameters;
    JointCommand (*evaluate)(double time);
    std::vector<PhaseSpec> phases{};
};

const PhaseSpec *phaseAt(const CaseSpec &spec, double time);
std::vector<CaseSpec> selectCases(std::string_view selection);

} // namespace simlab::benchmark

#endif
