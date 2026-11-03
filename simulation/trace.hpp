#ifndef SIMLAB_SIMULATION_TRACE_HPP
#define SIMLAB_SIMULATION_TRACE_HPP

#include "arm/types.hpp"

#include <cstdint>
#include <filesystem>
#include <fstream>

namespace simlab {

struct TraceOptions {
    std::filesystem::path path;
    bool force{};
};

// Inputs are sampled before mj_step; applied torques are read after mj_forward.
class TraceWriter {
public:
    explicit TraceWriter(const TraceOptions &options);
    void write(
        std::uint64_t step_index, double time, const ArmContext &context,
        const JointCommand &command, const JointFeedback &feedback,
        const CycleResult &result, const JointTorques &applied);
    void reset();
    void flush();

private:
    std::ofstream output_;
    std::uint64_t reset_index_{};
    std::uint64_t rows_{};
};

} // namespace simlab

#endif
