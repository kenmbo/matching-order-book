#include "benchmark_support.hpp"
#include "benchmark_provenance.hpp"

#include <array>
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

void test_canonical_configuration(Checks& checks) {
  const std::array<std::uint64_t, 2> seeds{24301, 12648430};
  lob::benchmark::ExperimentConfiguration configuration{
      "acceptance", "all", seeds, 0, 10'000, 5, 1, "sibling_idle"};
  checks.require(
      lob::benchmark::canonical_acceptance_configuration(configuration),
      "exact canonical acceptance configuration");

  const std::array<std::uint64_t, 2> duplicate{24301, 24301};
  checks.require(!lob::benchmark::distinct_seeds(duplicate),
                 "duplicate seeds rejected");
  configuration.seeds = duplicate;
  checks.require(
      !lob::benchmark::canonical_acceptance_configuration(configuration),
      "duplicate canonical seeds rejected");
  configuration.seeds = seeds;
  ++configuration.warmup;
  checks.require(
      !lob::benchmark::canonical_acceptance_configuration(configuration),
      "noncanonical warm-up rejected");
  --configuration.warmup;
  ++configuration.repetitions;
  checks.require(
      !lob::benchmark::canonical_acceptance_configuration(configuration),
      "noncanonical repetition count rejected");
  --configuration.repetitions;
  configuration.samples_override = 1;
  checks.require(
      !lob::benchmark::canonical_acceptance_configuration(configuration),
      "sample override rejected");
  configuration.samples_override = 0;
  configuration.mode = "exploratory";
  checks.require(
      !lob::benchmark::canonical_acceptance_configuration(configuration),
      "exploratory mode is never canonical");
  configuration.mode = "acceptance";
  configuration.sibling_occupancy = {};
  checks.require(
      !lob::benchmark::canonical_acceptance_configuration(configuration),
      "empty sibling policy rejected");
  configuration.sibling_occupancy = "not_observed";
  checks.require(
      !lob::benchmark::canonical_acceptance_configuration(configuration),
      "unobserved sibling policy rejected");

  constexpr std::array<std::size_t, 14> expected_samples{
      1'000'000, 500'000, 1'000'000, 500'000, 500'000, 1'000'000, 1'000'000,
      500'000,   200'000, 50'000,     20'000,  5'000,   1'000,     20'000};
  constexpr std::array<std::string_view, 14> expected_workloads{
      "mixed",          "cancel",   "unknown_cancel", "reduce",
      "increase",       "noop",     "unknown_amend",  "noncross_add",
      "fill1",          "fill4",    "fill16",         "fill64",
      "fill256",        "multi_level"};
  checks.require(lob::benchmark::kCanonicalWorkloads == expected_workloads,
                 "canonical workload names and order");
  bool sample_counts_match = true;
  for (std::size_t index = 0;
       index < lob::benchmark::kCanonicalWorkloads.size(); ++index) {
    sample_counts_match =
        sample_counts_match &&
        lob::benchmark::canonical_sample_count(
            lob::benchmark::kCanonicalWorkloads[index]) ==
            expected_samples[index];
  }
  checks.require(sample_counts_match &&
                     lob::benchmark::canonical_sample_count("invalid") == 0,
                 "all canonical per-workload sample counts");
}

void test_provenance_policy(Checks& checks) {
  lob::benchmark::BuildProvenance build;
  build.source_commit = std::string(40, '1');
  build.source_dirty_at_build = false;
  build.build_type = "Release";
  build.compiler_id = "GNU";
  build.compiler_version = "12.2.0";
  build.compiler_banner = "g++ 12.2.0";
  build.latency_sha256 = std::string(64, 'a');
  build.allocation_sha256 = std::string(64, 'b');
  build.latency_compile_flags =
      "-O3 -DNDEBUG -Wall -Wextra -Werror -march=native -ffast-math "
      "-std=c++20";
  build.latency_link_flags = "-O3 -DNDEBUG";
  build.allocation_compile_flags =
      build.latency_compile_flags + " -DLOB_ENABLE_ALLOCATION_AUDIT=1";
  build.allocation_link_flags = build.latency_link_flags;
  lob::benchmark::ProvenanceVerificationInputs execution;
  execution.execution_commit = build.source_commit;
  execution.execution_tree_dirty = false;
  execution.actual_latency_sha256 = build.latency_sha256;
  execution.actual_allocation_sha256 = build.allocation_sha256;
  execution.executing_expected_binary = true;
  auto verified = lob::benchmark::verify_build_provenance(build, execution);
  checks.require(verified.canonical_eligible,
                 "clean matching Release provenance is eligible");

  execution.execution_tree_dirty = true;
  verified = lob::benchmark::verify_build_provenance(build, execution);
  checks.require(!verified.canonical_eligible,
                 "dirty execution provenance rejected");
  execution.execution_tree_dirty = false;
  execution.actual_latency_sha256 = std::string(64, 'c');
  verified = lob::benchmark::verify_build_provenance(build, execution);
  checks.require(!verified.canonical_eligible,
                 "binary hash mismatch rejected");
  execution.actual_latency_sha256 = build.latency_sha256;
  execution.actual_allocation_sha256 = std::string(64, 'c');
  verified = lob::benchmark::verify_build_provenance(build, execution);
  checks.require(!verified.canonical_eligible,
                 "stale allocation-audit hash rejected");
  execution.actual_allocation_sha256 = build.allocation_sha256;
  execution.executing_expected_binary = false;
  verified = lob::benchmark::verify_build_provenance(build, execution);
  checks.require(!verified.canonical_eligible,
                 "wrong executable identity rejected");
  execution.executing_expected_binary = true;
  execution.execution_commit = std::string(40, '2');
  verified = lob::benchmark::verify_build_provenance(build, execution);
  checks.require(!verified.canonical_eligible,
                 "build and execution commit mismatch rejected");
  execution.execution_commit = build.source_commit;
  build.allocation_compile_flags.clear();
  verified = lob::benchmark::verify_build_provenance(build, execution);
  checks.require(!verified.canonical_eligible,
                 "allocation-audit Release flags required");
  build.allocation_compile_flags =
      build.latency_compile_flags + " -DLOB_ENABLE_ALLOCATION_AUDIT=1";
  build.source_dirty_at_build = true;
  verified = lob::benchmark::verify_build_provenance(build, execution);
  checks.require(!verified.canonical_eligible,
                 "dirty build provenance rejected");
}

}  // namespace

int main() {
  Checks checks;
  test_mix(checks);
  test_volume_and_percentiles(checks);
  test_gates_and_schema(checks);
  test_canonical_configuration(checks);
  test_provenance_policy(checks);
  return checks.passed() ? 0 : 1;
}
