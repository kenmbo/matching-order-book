#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace lob::benchmark {

inline constexpr std::array<std::string_view, 14> kCanonicalWorkloads{
    "mixed",          "cancel",   "unknown_cancel", "reduce",
    "increase",       "noop",     "unknown_amend",  "noncross_add",
    "fill1",          "fill4",    "fill16",         "fill64",
    "fill256",        "multi_level"};
inline constexpr std::array<std::uint64_t, 2> kCanonicalSeeds{0x5eed,
                                                              0xc0ffee};
inline constexpr std::size_t kCanonicalWarmup = 10'000;
inline constexpr std::size_t kCanonicalRepetitions = 5;

struct ExperimentConfiguration final {
  std::string_view mode{};
  std::string_view workload{};
  std::span<const std::uint64_t> seeds{};
  std::size_t samples_override{};
  std::size_t warmup{};
  std::size_t repetitions{};
  int cpu{-1};
};

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
[[nodiscard]] bool distinct_seeds(
    std::span<const std::uint64_t> seeds) noexcept;
[[nodiscard]] bool canonical_acceptance_configuration(
    const ExperimentConfiguration& configuration) noexcept;
[[nodiscard]] std::size_t canonical_sample_count(
    std::string_view workload) noexcept;

}  // namespace lob::benchmark
