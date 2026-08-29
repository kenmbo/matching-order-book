#include "benchmark_support.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <random>
#include <sstream>

namespace lob::benchmark {

bool distinct_seeds(std::span<const std::uint64_t> seeds) noexcept {
  for (std::size_t left = 0; left < seeds.size(); ++left) {
    for (std::size_t right = left + 1; right < seeds.size(); ++right) {
      if (seeds[left] == seeds[right]) {
        return false;
      }
    }
  }
  return true;
}

bool canonical_acceptance_configuration(
    const ExperimentConfiguration& configuration) noexcept {
  return configuration.mode == "acceptance" &&
         configuration.workload == "all" &&
         configuration.seeds.size() == kCanonicalSeeds.size() &&
         std::equal(configuration.seeds.begin(), configuration.seeds.end(),
                    kCanonicalSeeds.begin()) &&
         distinct_seeds(configuration.seeds) &&
         configuration.samples_override == 0 &&
         configuration.warmup == kCanonicalWarmup &&
         configuration.repetitions == kCanonicalRepetitions &&
         configuration.cpu >= 0 && !configuration.sibling_occupancy.empty() &&
         configuration.sibling_occupancy != "not_observed";
}

std::size_t canonical_sample_count(std::string_view workload) noexcept {
  if (workload == "mixed" || workload == "unknown_cancel" ||
      workload == "noop" || workload == "unknown_amend") {
    return 1'000'000;
  }
  if (workload == "cancel" || workload == "reduce" ||
      workload == "increase" || workload == "noncross_add") {
    return 500'000;
  }
  if (workload == "fill1") {
    return 200'000;
  }
  if (workload == "fill4") {
    return 50'000;
  }
  if (workload == "fill16" || workload == "multi_level") {
    return 20'000;
  }
  if (workload == "fill64") {
    return 5'000;
  }
  if (workload == "fill256") {
    return 1'000;
  }
  return 0;
}

std::uint64_t MixedCounts::total() const noexcept {
  return cancel + reduce + increase + no_op_amend + non_crossing_add + cross;
}

std::uint64_t MixedCounts::cancel_and_amend() const noexcept {
  return cancel + reduce + increase + no_op_amend;
}

bool GateEvaluation::passed() const noexcept {
  return p50 && p99 && p999 && throughput;
}

std::vector<MixedOperation> deterministic_mixed_schedule(
    std::size_t sample_count, std::uint64_t seed) {
  std::vector<MixedOperation> schedule;
  schedule.reserve(sample_count);
  const auto count = static_cast<std::uint64_t>(sample_count);
  const auto cancel = count * 20 / 100;
  const auto reduce = count * 20 / 100;
  const auto increase = count * 20 / 100;
  const auto no_op = count * 10 / 100;
  const auto add = count * 20 / 100;
  const auto assigned = cancel + reduce + increase + no_op + add;
  const auto cross = count - assigned;
  const auto append = [&schedule](MixedOperation operation,
                                  std::uint64_t operation_count) {
    schedule.insert(schedule.end(), static_cast<std::size_t>(operation_count),
                    operation);
  };
  append(MixedOperation::Cancel, cancel);
  append(MixedOperation::Reduce, reduce);
  append(MixedOperation::Increase, increase);
  append(MixedOperation::NoOpAmend, no_op);
  append(MixedOperation::NonCrossingAdd, add);
  append(MixedOperation::Cross, cross);
  std::mt19937_64 generator(seed);
  std::shuffle(schedule.begin(), schedule.end(), generator);
  return schedule;
}

MixedCounts count_operations(
    const std::vector<MixedOperation>& schedule) noexcept {
  MixedCounts counts;
  for (const auto operation : schedule) {
    switch (operation) {
      case MixedOperation::Cancel:
        ++counts.cancel;
        break;
      case MixedOperation::Reduce:
        ++counts.reduce;
        break;
      case MixedOperation::Increase:
        ++counts.increase;
        break;
      case MixedOperation::NoOpAmend:
        ++counts.no_op_amend;
        break;
      case MixedOperation::NonCrossingAdd:
        ++counts.non_crossing_add;
        break;
      case MixedOperation::Cross:
        ++counts.cross;
        break;
    }
  }
  return counts;
}

bool valid_mixed_operation_counts(const MixedCounts& counts) noexcept {
  const auto total = counts.total();
  if (total == 0) {
    return false;
  }
  return counts.cancel_and_amend() * 100 == total * 70 &&
         counts.non_crossing_add * 100 == total * 20 &&
         counts.cross * 100 == total * 10;
}

double percentage(std::uint64_t numerator,
                  std::uint64_t denominator) noexcept {
  if (denominator == 0) {
    return 0.0;
  }
  return static_cast<double>(numerator) * 100.0 /
         static_cast<double>(denominator);
}

bool top_five_volume_valid(std::uint64_t top_five_volume,
                           std::uint64_t total_volume) noexcept {
  return total_volume != 0 && top_five_volume <= total_volume &&
         top_five_volume * 100 >= total_volume * 80;
}

std::uint64_t nearest_rank(const std::vector<std::uint64_t>& sorted_samples,
                           std::uint64_t percentile_numerator,
                           std::uint64_t percentile_denominator) noexcept {
  if (sorted_samples.empty() || percentile_denominator == 0 ||
      percentile_numerator == 0 ||
      percentile_numerator > percentile_denominator) {
    return 0;
  }
  const auto count = static_cast<std::uint64_t>(sorted_samples.size());
  const auto rank =
      (count * percentile_numerator + percentile_denominator - 1) /
      percentile_denominator;
  return sorted_samples[static_cast<std::size_t>(rank - 1)];
}

LatencyStatistics summarize_latencies(std::vector<std::uint64_t>& samples) {
  LatencyStatistics result;
  if (samples.empty()) {
    return result;
  }
  std::sort(samples.begin(), samples.end());
  result.sample_count = samples.size();
  result.p50_ns = nearest_rank(samples, 50, 100);
  result.p90_ns = nearest_rank(samples, 90, 100);
  result.p99_ns = nearest_rank(samples, 99, 100);
  result.p999_ns = nearest_rank(samples, 999, 1'000);
  result.p9999_ns = nearest_rank(samples, 9'999, 10'000);
  result.maximum_ns = samples.back();
  for (const auto sample : samples) {
    if (sample > std::numeric_limits<std::uint64_t>::max() -
                     result.total_timed_ns) {
      result.total_timed_ns = std::numeric_limits<std::uint64_t>::max();
      break;
    }
    result.total_timed_ns += sample;
  }
  if (result.total_timed_ns != 0) {
    result.throughput_per_second =
        static_cast<double>(result.sample_count) * 1'000'000'000.0 /
        static_cast<double>(result.total_timed_ns);
  }
  return result;
}

GateEvaluation evaluate_public_path_gate(
    const LatencyStatistics& statistics) noexcept {
  return {statistics.p50_ns <= 1'500, statistics.p99_ns <= 5'000,
          statistics.p999_ns <= 15'000,
          statistics.throughput_per_second >= 500'000.0};
}

GateEvaluation evaluate_matching_core_threshold(
    const LatencyStatistics& statistics) noexcept {
  return {statistics.p50_ns <= 500, statistics.p99_ns <= 2'000,
          statistics.p999_ns <= 6'000,
          statistics.throughput_per_second >= 1'000'000.0};
}

std::uint64_t multi_fill_p99_ceiling_ns(std::uint64_t fills,
                                        std::uint64_t emitted_events) noexcept {
  if (fills > (std::numeric_limits<std::uint64_t>::max() - 1'000) / 200) {
    return std::numeric_limits<std::uint64_t>::max();
  }
  const auto fill_component = fills * 200;
  if (emitted_events >
      (std::numeric_limits<std::uint64_t>::max() - 1'000 - fill_component) /
          100) {
    return std::numeric_limits<std::uint64_t>::max();
  }
  return 1'000 + fill_component + emitted_events * 100;
}

std::uint64_t checksum_mix(std::uint64_t checksum,
                           std::uint64_t value) noexcept {
  constexpr std::uint64_t prime = 1'099'511'628'211ULL;
  checksum ^= value;
  checksum *= prime;
  return checksum;
}

std::string statistics_json(const LatencyStatistics& statistics) {
  std::ostringstream output;
  output << std::fixed << std::setprecision(3)
         << "{\"sample_count\":" << statistics.sample_count
         << ",\"p50_ns\":" << statistics.p50_ns
         << ",\"p90_ns\":" << statistics.p90_ns
         << ",\"p99_ns\":" << statistics.p99_ns
         << ",\"p999_ns\":" << statistics.p999_ns
         << ",\"p9999_ns\":" << statistics.p9999_ns
         << ",\"maximum_ns\":" << statistics.maximum_ns
         << ",\"total_timed_ns\":" << statistics.total_timed_ns
         << ",\"throughput_per_second\":"
         << statistics.throughput_per_second << '}';
  return output.str();
}

}  // namespace lob::benchmark
