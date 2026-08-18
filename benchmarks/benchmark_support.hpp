#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace lob::benchmark {

enum class MixedOperation : std::uint8_t {
  Cancel,
  Reduce,
  Increase,
  NoOpAmend,
  NonCrossingAdd,
  Cross,
};

struct MixedCounts final {
  std::uint64_t cancel{};
  std::uint64_t reduce{};
  std::uint64_t increase{};
  std::uint64_t no_op_amend{};
  std::uint64_t non_crossing_add{};
  std::uint64_t cross{};

  [[nodiscard]] std::uint64_t total() const noexcept;
  [[nodiscard]] std::uint64_t cancel_and_amend() const noexcept;
};

struct LatencyStatistics final {
  std::uint64_t sample_count{};
  std::uint64_t p50_ns{};
  std::uint64_t p90_ns{};
  std::uint64_t p99_ns{};
  std::uint64_t p999_ns{};
  std::uint64_t p9999_ns{};
  std::uint64_t maximum_ns{};
  std::uint64_t total_timed_ns{};
  double throughput_per_second{};
};

struct GateEvaluation final {
  bool p50{};
  bool p99{};
  bool p999{};
  bool throughput{};

  [[nodiscard]] bool passed() const noexcept;
};

[[nodiscard]] std::vector<MixedOperation> deterministic_mixed_schedule(
    std::size_t sample_count, std::uint64_t seed);
[[nodiscard]] MixedCounts count_operations(
    const std::vector<MixedOperation>& schedule) noexcept;
[[nodiscard]] bool valid_mixed_operation_counts(
    const MixedCounts& counts) noexcept;
[[nodiscard]] double percentage(std::uint64_t numerator,
                                std::uint64_t denominator) noexcept;
[[nodiscard]] bool top_five_volume_valid(std::uint64_t top_five_volume,
                                         std::uint64_t total_volume) noexcept;
[[nodiscard]] std::uint64_t nearest_rank(
    const std::vector<std::uint64_t>& sorted_samples,
    std::uint64_t percentile_numerator,
    std::uint64_t percentile_denominator) noexcept;
[[nodiscard]] LatencyStatistics summarize_latencies(
    std::vector<std::uint64_t>& samples);
[[nodiscard]] GateEvaluation evaluate_public_path_gate(
    const LatencyStatistics& statistics) noexcept;
[[nodiscard]] GateEvaluation evaluate_matching_core_threshold(
    const LatencyStatistics& statistics) noexcept;
[[nodiscard]] std::uint64_t multi_fill_p99_ceiling_ns(
    std::uint64_t fills, std::uint64_t emitted_events) noexcept;
[[nodiscard]] std::uint64_t checksum_mix(std::uint64_t checksum,
                                         std::uint64_t value) noexcept;
[[nodiscard]] std::string statistics_json(
    const LatencyStatistics& statistics);

}  // namespace lob::benchmark
