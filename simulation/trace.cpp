#include "trace.hpp"

#include <array>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <string_view>

namespace simlab {

TraceWriter::TraceWriter(const TraceOptions &options) {
    if (options.path.empty()) throw std::invalid_argument("Trace path must not be empty.");
    if (!options.force && std::filesystem::exists(options.path)) {
        throw std::invalid_argument("Trace file already exists; use --force to overwrite: " + options.path.string());
    }
    const auto parent = options.path.parent_path();
    if (!parent.empty()) std::filesystem::create_directories(parent);
    output_.imbue(std::locale::classic());
    output_.exceptions(std::ios::failbit | std::ios::badbit);
    output_.open(options.path, std::ios::out | std::ios::trunc | std::ios::binary);
    output_ << "trace_version,reset_index,step_index,simulation_time_s,cycle_timestep_s";
    constexpr std::array<std::string_view, 5> prefixes{
        "input", "feedback", "feedback", "request", "applied"};
    constexpr std::array<std::string_view, 5> suffixes{
        "position_rad", "position_rad", "velocity_radps", "torque_nm", "torque_nm"};
    for (std::size_t group = 0; group < prefixes.size(); ++group) {
        for (int joint = 1; joint <= 6; ++joint) {
            output_ << ',' << prefixes[group] << "_j" << joint << '_' << suffixes[group];
        }
    }
    output_ << '\n';
}

void TraceWriter::write(
    std::uint64_t step_index, double time, const ArmContext &context,
    const JointCommand &command, const JointFeedback &feedback,
    const CycleResult &result, const JointTorques &applied) {
    std::ostringstream row;
    row.imbue(std::locale::classic());
    row.exceptions(std::ios::failbit | std::ios::badbit);
    row << 1U << ',' << reset_index_ << ',' << step_index << ','
        << std::setprecision(std::numeric_limits<double>::max_digits10) << time << ','
        << std::setprecision(std::numeric_limits<float>::max_digits10) << context.dt;
    for (const auto *values : {&command.position, &feedback.position, &feedback.velocity, &result.torques, &applied}) {
        for (const float value : *values) row << ',' << value;
    }
    row << '\n';
    output_ << row.str();
    if (++rows_ % 100U == 0U) flush();
}

void TraceWriter::reset() {
    flush();
    ++reset_index_;
}

void TraceWriter::flush() {
    output_.flush();
}

} // namespace simlab
