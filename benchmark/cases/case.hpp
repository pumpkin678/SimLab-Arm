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

struct CaseSpec {
    std::string id;
    double duration;
    std::vector<Parameter> parameters;
    JointCommand (*evaluate)(double time);
};

std::vector<CaseSpec> selectCases(std::string_view selection);

} // namespace simlab::benchmark

#endif
