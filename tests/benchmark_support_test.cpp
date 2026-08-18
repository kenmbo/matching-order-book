#include "benchmark_support.hpp"

#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

namespace {

class Checks final {
 public:
  void require(bool condition, const char* message) {
    if (!condition) {
      std::cerr << "benchmark support failure: " << message << '\n';
      failed_ = true;
    }
  }

  [[nodiscard]] bool passed() const noexcept { return !failed_; }

 private:
  bool failed_{};
};

void test_mix(Checks& checks) {
  const auto first =
      lob::benchmark::deterministic_mixed_schedule(10'000, 0x5eed);
  const auto second =
      lob::benchmark::deterministic_mixed_schedule(10'000, 0x5eed);
  const auto other =
      lob::benchmark::deterministic_mixed_schedule(10'000, 0xc0ffee);
  checks.require(first == second, "fixed seed must reproduce schedule");
  checks.require(first != other, "different seeds should vary schedule");
  const auto counts = lob::benchmark::count_operations(first);
  checks.require(lob::benchmark::valid_mixed_operation_counts(counts),
                 "70/20/10 schedule validity");
  checks.require(counts.cancel_and_amend() == 7'000 &&
                     counts.non_crossing_add == 2'000 && counts.cross == 1'000,
                 "exact mixed operation counts");
  checks.require(!lob::benchmark::valid_mixed_operation_counts({}),
                 "empty mix is invalid");
}

void test_volume_and_percentiles(Checks& checks) {
  checks.require(lob::benchmark::top_five_volume_valid(80, 100),
                 "exact top-five boundary");
  checks.require(!lob::benchmark::top_five_volume_valid(79, 100),
                 "below top-five boundary");
  checks.require(!lob::benchmark::top_five_volume_valid(0, 0),
                 "empty volume invalid");

  std::vector<std::uint64_t> samples;
  for (std::uint64_t value = 1; value <= 10'000; ++value) {
    samples.push_back(value);
  }
  const auto statistics = lob::benchmark::summarize_latencies(samples);
  checks.require(statistics.p50_ns == 5'000 && statistics.p90_ns == 9'000 &&
                     statistics.p99_ns == 9'900 &&
                     statistics.p999_ns == 9'990 &&
                     statistics.p9999_ns == 9'999 &&
                     statistics.maximum_ns == 10'000,
                 "nearest-rank percentile convention");
}

void test_gates_and_schema(Checks& checks) {
  lob::benchmark::LatencyStatistics exact;
  exact.sample_count = 1;
  exact.p50_ns = 1'500;
  exact.p90_ns = 2'000;
  exact.p99_ns = 5'000;
  exact.p999_ns = 15'000;
  exact.p9999_ns = 20'000;
  exact.maximum_ns = 20'000;
  exact.total_timed_ns = 2'000;
  exact.throughput_per_second = 500'000.0;
  checks.require(lob::benchmark::evaluate_public_path_gate(exact).passed(),
                 "public gate exact boundary");
  ++exact.p99_ns;
  checks.require(!lob::benchmark::evaluate_public_path_gate(exact).passed(),
                 "public gate immediately above boundary");
  lob::benchmark::LatencyStatistics matching_core;
  matching_core.p50_ns = 500;
  matching_core.p99_ns = 2'000;
  matching_core.p999_ns = 6'000;
  matching_core.throughput_per_second = 1'000'000.0;
  checks.require(
      lob::benchmark::evaluate_matching_core_threshold(matching_core).passed(),
      "matching-core informational threshold exact boundary");
  ++matching_core.p999_ns;
  checks.require(
      !lob::benchmark::evaluate_matching_core_threshold(matching_core).passed(),
      "matching-core informational threshold immediately above boundary");
  checks.require(lob::benchmark::multi_fill_p99_ceiling_ns(1, 1) == 1'300 &&
                     lob::benchmark::multi_fill_p99_ceiling_ns(4, 4) == 2'200 &&
                     lob::benchmark::multi_fill_p99_ceiling_ns(16, 16) ==
                         5'800 &&
                     lob::benchmark::multi_fill_p99_ceiling_ns(64, 64) ==
                         20'200 &&
                     lob::benchmark::multi_fill_p99_ceiling_ns(256, 256) ==
                         77'800,
                 "multi-fill ceilings");

  const auto json = lob::benchmark::statistics_json(exact);
  checks.require(json.find("\"sample_count\":1") != std::string::npos &&
                     json.find("\"p9999_ns\":20000") != std::string::npos &&
                     json.find("\"throughput_per_second\":500000.000") !=
                         std::string::npos,
                 "statistics JSON schema");
  checks.require(lob::benchmark::checksum_mix(123, 456) ==
                     lob::benchmark::checksum_mix(123, 456),
                 "checksum stability");
}

}  // namespace

int main() {
  Checks checks;
  test_mix(checks);
  test_volume_and_percentiles(checks);
  test_gates_and_schema(checks);
  return checks.passed() ? 0 : 1;
}
