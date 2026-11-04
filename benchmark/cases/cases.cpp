#include "case.hpp"
#include "joint_sine/case.hpp"

#include <stdexcept>

namespace simlab::benchmark {

std::vector<CaseSpec> selectCases(std::string_view selection) {
    const std::vector<CaseSpec> definitions{joint_sine::definition()};
    std::vector<CaseSpec> selected;
    for (const auto &definition : definitions) {
        if (selection == "all" || selection == definition.id) selected.push_back(definition);
    }
    if (selected.empty()) throw std::invalid_argument("Unknown benchmark case: " + std::string(selection));
    return selected;
}

} // namespace simlab::benchmark
