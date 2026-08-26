#include "benchmark_support.hpp"
#include "benchmark_provenance.hpp"

#include "lob/matching/matching_engine.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <new>
#include <optional>
#include <random>
#include <sched.h>
#include <sstream>
#include <span>
#include <string>
#include <string_view>
#include <sys/utsname.h>
#include <time.h>
#include <unistd.h>
#include <utility>
#include <vector>

namespace allocation_audit {

enum class Phase : std::uint8_t {
  Inactive,
  Construction,
  InitialPopulation,
  TraceGeneration,
  Warmup,
  TimedProcess,
  TimedCollection,
  PostStatistics,
  Destruction,
  Count,
};

struct Counters final {
  std::uint64_t allocations{};
  std::uint64_t allocated_bytes{};
  std::uint64_t deallocations{};

  constexpr Counters operator-(const Counters& other) const noexcept {
    return {allocations - other.allocations,
            allocated_bytes - other.allocated_bytes,
            deallocations - other.deallocations};
  }
};

#ifdef LOB_ENABLE_ALLOCATION_AUDIT
thread_local Phase current_phase = Phase::Inactive;
std::array<Counters, static_cast<std::size_t>(Phase::Count)> counters{};

void record_allocation(std::size_t bytes) noexcept {
  auto& value = counters[static_cast<std::size_t>(current_phase)];
  ++value.allocations;
  value.allocated_bytes += static_cast<std::uint64_t>(bytes);
}

void record_deallocation() noexcept {
  ++counters[static_cast<std::size_t>(current_phase)].deallocations;
}
#else
constexpr Phase current_phase = Phase::Inactive;
constexpr std::array<Counters, static_cast<std::size_t>(Phase::Count)>
    counters{};
#endif

class Guard final {
 public:
  explicit Guard(Phase phase) noexcept
#ifdef LOB_ENABLE_ALLOCATION_AUDIT
      : previous_(current_phase) {
    current_phase = phase;
  }
#else
  {
    static_cast<void>(phase);
  }
#endif

  Guard(const Guard&) = delete;
  Guard& operator=(const Guard&) = delete;

  ~Guard() {
#ifdef LOB_ENABLE_ALLOCATION_AUDIT
    current_phase = previous_;
#endif
  }

 private:
#ifdef LOB_ENABLE_ALLOCATION_AUDIT
  Phase previous_{};
#endif
};

[[nodiscard]] Counters snapshot(Phase phase) noexcept {
  return counters[static_cast<std::size_t>(phase)];
}

}  // namespace allocation_audit

#ifdef LOB_ENABLE_ALLOCATION_AUDIT
void* operator new(std::size_t bytes) {
  const auto allocation_size = bytes == 0 ? std::size_t{1} : bytes;
  if (void* memory = std::malloc(allocation_size)) {
    allocation_audit::record_allocation(allocation_size);
    return memory;
  }
  throw std::bad_alloc{};
}

void* operator new[](std::size_t bytes) {
  return ::operator new(bytes);
}

void operator delete(void* memory) noexcept {
  if (memory != nullptr) {
    allocation_audit::record_deallocation();
  }
  std::free(memory);
}

void operator delete[](void* memory) noexcept {
  if (memory != nullptr) {
    allocation_audit::record_deallocation();
  }
  std::free(memory);
}

void operator delete(void* memory, std::size_t) noexcept {
  if (memory != nullptr) {
    allocation_audit::record_deallocation();
  }
  std::free(memory);
}

void operator delete[](void* memory, std::size_t) noexcept {
  if (memory != nullptr) {
    allocation_audit::record_deallocation();
  }
  std::free(memory);
}

void* operator new(std::size_t bytes, const std::nothrow_t&) noexcept {
  try {
    return ::operator new(bytes);
  } catch (...) {
    return nullptr;
  }
}

void* operator new[](std::size_t bytes, const std::nothrow_t&) noexcept {
  return ::operator new(bytes, std::nothrow);
}

void operator delete(void* memory, const std::nothrow_t&) noexcept {
  if (memory != nullptr) {
    allocation_audit::record_deallocation();
  }
  std::free(memory);
}

void operator delete[](void* memory, const std::nothrow_t&) noexcept {
  if (memory != nullptr) {
    allocation_audit::record_deallocation();
  }
  std::free(memory);
}

void* operator new(std::size_t bytes, std::align_val_t alignment) {
  void* memory = nullptr;
  const auto alignment_value = static_cast<std::size_t>(alignment);
  const auto allocation_size = bytes == 0 ? std::size_t{1} : bytes;
  if (posix_memalign(&memory, alignment_value, allocation_size) != 0) {
    throw std::bad_alloc{};
  }
  allocation_audit::record_allocation(allocation_size);
  return memory;
}

void* operator new[](std::size_t bytes, std::align_val_t alignment) {
  return ::operator new(bytes, alignment);
}

void operator delete(void* memory, std::align_val_t) noexcept {
  if (memory != nullptr) {
    allocation_audit::record_deallocation();
  }
  std::free(memory);
}

void operator delete[](void* memory, std::align_val_t) noexcept {
  if (memory != nullptr) {
    allocation_audit::record_deallocation();
  }
  std::free(memory);
}

void operator delete(void* memory, std::size_t,
                     std::align_val_t) noexcept {
  if (memory != nullptr) {
    allocation_audit::record_deallocation();
  }
  std::free(memory);
}

void operator delete[](void* memory, std::size_t,
                       std::align_val_t) noexcept {
  if (memory != nullptr) {
    allocation_audit::record_deallocation();
  }
  std::free(memory);
}

void* operator new(std::size_t bytes, std::align_val_t alignment,
                   const std::nothrow_t&) noexcept {
  try {
    return ::operator new(bytes, alignment);
  } catch (...) {
    return nullptr;
  }
}

void* operator new[](std::size_t bytes, std::align_val_t alignment,
                     const std::nothrow_t&) noexcept {
  return ::operator new(bytes, alignment, std::nothrow);
}

void operator delete(void* memory, std::align_val_t,
                     const std::nothrow_t&) noexcept {
  if (memory != nullptr) {
    allocation_audit::record_deallocation();
  }
  std::free(memory);
}

void operator delete[](void* memory, std::align_val_t,
                       const std::nothrow_t&) noexcept {
  if (memory != nullptr) {
    allocation_audit::record_deallocation();
  }
  std::free(memory);
}
#endif

namespace {

using Clock = std::chrono::steady_clock;

enum class CommandType : std::uint8_t { New, Cancel, Amend };

struct Command final {
  CommandType type{CommandType::New};
  lob::OrderId order_id{};
  lob::Side side{lob::Side::Invalid};
  lob::PriceTicks price{};
  lob::Quantity quantity{};
  lob::OrderBookResult expected_result{lob::OrderBookResult::Accepted};
  std::uint16_t expected_reports{};
};

struct Block final {
  std::size_t first_command{};
  std::size_t command_count{};
  std::vector<lob::NewOrder> initial_orders{};
  std::optional<Command> repair_command{};
  bool repopulate_after_command{};
};

struct Trace final {
  std::string workload{};
  std::vector<Command> commands{};
  std::vector<Block> blocks{};
  lob::benchmark::MixedCounts mixed_counts{};
  std::array<std::uint64_t, 65> command_quantity_distribution{};
  std::uint64_t command_quantity_volume{};
  std::uint64_t top_five_volume{};
  std::uint64_t priced_volume{};
  std::size_t expected_min_active{};
  std::size_t expected_max_active{};
  std::size_t target_precondition_orders{};
  std::size_t target_precondition_levels{};
  std::uint16_t nominal_fills{};
  bool command_to_outbox_observed{};
  std::uint64_t trace_checksum{};
  allocation_audit::Counters trace_allocations{};
};

struct Options final {
  std::string mode{};
  std::string workload{"all"};
  std::vector<std::uint64_t> seeds{};
  std::size_t samples_override{};
  std::size_t warmup{};
  std::size_t repetitions{};
  int cpu{-1};
  std::string sibling_occupancy{"not_observed"};
  std::string json_path{};
  std::string report_path{};
  std::string allocation_input{};
  std::string allocation_output{};
  std::string provenance_path{};
};

struct PhaseDeltas final {
  allocation_audit::Counters trace_generation{};
  allocation_audit::Counters construction{};
  allocation_audit::Counters initial_population{};
  allocation_audit::Counters warmup{};
  allocation_audit::Counters timed_process{};
  allocation_audit::Counters timed_collection{};
  allocation_audit::Counters post_statistics{};
  allocation_audit::Counters destruction{};
};

struct RepetitionResult final {
  lob::benchmark::LatencyStatistics statistics{};
  lob::benchmark::GateEvaluation public_gate{};
  lob::benchmark::GateEvaluation matching_core_threshold{};
  bool multi_fill_threshold{};
  std::uint64_t checksum{};
  std::uint64_t accepted{};
  std::uint64_t rejected{};
  std::array<std::uint64_t, lob::kMaximumFillsPerCommand + 1>
      fill_distribution{};
  std::array<std::uint64_t, lob::kMaximumFillsPerCommand + 1>
      event_distribution{};
  std::size_t minimum_active{};
  std::size_t maximum_active{};
  std::size_t minimum_levels{};
  std::size_t maximum_levels{};
  std::uint64_t unintended_outbox_failures{};
  std::uint64_t lifecycle_transitions{};
  int starting_cpu{-1};
  int ending_cpu{-1};
  PhaseDeltas allocations{};
  lob::StorageDiagnostics storage{};
};

struct AllocationKey final {
  std::string workload{};
  std::uint64_t seed{};
  std::size_t repetition{};
  std::size_t samples{};
  std::size_t warmup{};
  std::uint64_t trace_checksum{};
  PhaseDeltas phases{};
};

struct WorkloadRun final {
  std::string workload{};
  std::uint64_t seed{};
  std::size_t samples{};
  std::size_t warmup{};
  Trace trace{};
  std::vector<RepetitionResult> repetitions{};
  lob::benchmark::LatencyStatistics median{};
  std::size_t public_gate_passes{};
  bool public_gate_applicable{};
  bool workload_valid{};
  bool allocation_policy_compliant{};
  bool performance_gate_compliant{};
};

struct Environment final {
  std::string cpu_model{};
  std::string microcode{};
  std::string kernel{};
  std::string compiler{};
  std::string affinity{};
  std::string sibling{};
  std::string sibling_occupancy{};
  std::string governor{};
  std::string frequency_khz{};
  std::string scaling_driver{};
  std::string scaling_min_khz{};
  std::string scaling_max_khz{};
  std::string energy_performance_preference{};
  std::string numa_node{};
  std::uint64_t clock_resolution_ns{};
  lob::benchmark::LatencyStatistics clock_overhead{};
};

template <typename Domain, typename Source>
[[nodiscard]] Domain domain(Source value) {
  const auto converted = lob::checked_domain_cast<Domain>(value);
  if (!converted.has_value()) {
    std::abort();
  }
  return converted.value;
}

[[nodiscard]] lob::OrderId oid(std::uint64_t value) {
  return domain<lob::OrderId>(value);
}

[[nodiscard]] lob::InstrumentId instrument() {
  return domain<lob::InstrumentId>(std::uint32_t{1});
}

[[nodiscard]] lob::PriceTicks ticks(std::int64_t value) {
  return domain<lob::PriceTicks>(value);
}

[[nodiscard]] lob::Quantity qty(std::uint64_t value) {
  return domain<lob::Quantity>(value);
}

[[nodiscard]] Command make_new(std::uint64_t id, lob::Side side,
                               std::int64_t price, std::uint64_t quantity,
                               std::uint16_t reports = 0) {
  return {CommandType::New, oid(id), side, ticks(price), qty(quantity),
          lob::OrderBookResult::Accepted, reports};
}

[[nodiscard]] Command make_cancel(
    std::uint64_t id,
    lob::OrderBookResult expected = lob::OrderBookResult::Accepted) {
  return {CommandType::Cancel, oid(id), lob::Side::Invalid, {}, {}, expected,
          0};
}

[[nodiscard]] Command make_amend(
    std::uint64_t id, std::int64_t price, std::uint64_t quantity,
    lob::OrderBookResult expected = lob::OrderBookResult::Accepted) {
  return {CommandType::Amend, oid(id), lob::Side::Invalid, ticks(price),
          qty(quantity), expected, 0};
}

[[nodiscard]] std::optional<std::uint64_t> parse_unsigned(
    std::string_view value) noexcept {
  std::uint64_t result = 0;
  const auto parsed =
      std::from_chars(value.data(), value.data() + value.size(), result);
  if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size()) {
    return std::nullopt;
  }
  return result;
}

[[nodiscard]] bool parse_seeds(std::string_view value,
                               std::vector<std::uint64_t>& seeds) {
  std::vector<std::uint64_t> parsed_seeds;
  std::size_t start = 0;
  while (start < value.size()) {
    const auto comma = value.find(',', start);
    const auto part = value.substr(
        start, comma == std::string_view::npos ? value.size() - start
                                               : comma - start);
    const auto seed = parse_unsigned(part);
    if (!seed) {
      return false;
    }
    parsed_seeds.push_back(*seed);
    if (comma == std::string_view::npos) {
      break;
    }
    start = comma + 1;
  }
  if (parsed_seeds.empty() || !lob::benchmark::distinct_seeds(parsed_seeds)) {
    return false;
  }
  seeds = std::move(parsed_seeds);
  return true;
}

[[nodiscard]] std::optional<Options> parse_options(int argc, char** argv) {
  Options options;
  std::vector<std::string_view> seen_arguments;
  for (int index = 1; index < argc; ++index) {
    const std::string_view argument{argv[index]};
    if (index + 1 >= argc ||
        std::find(seen_arguments.begin(), seen_arguments.end(), argument) !=
            seen_arguments.end()) {
      return std::nullopt;
    }
    seen_arguments.push_back(argument);
    const std::string_view value{argv[++index]};
    if (argument == "--mode") {
      options.mode = value;
    } else if (argument == "--workload") {
      options.workload = value;
    } else if (argument == "--seeds") {
      options.seeds.clear();
      if (!parse_seeds(value, options.seeds)) {
        return std::nullopt;
      }
    } else if (argument == "--samples") {
      const auto parsed = parse_unsigned(value);
      if (!parsed || *parsed == 0 ||
          *parsed > std::numeric_limits<std::size_t>::max()) {
        return std::nullopt;
      }
      options.samples_override = static_cast<std::size_t>(*parsed);
    } else if (argument == "--warmup") {
      const auto parsed = parse_unsigned(value);
      if (!parsed || *parsed == 0 ||
          *parsed > std::numeric_limits<std::size_t>::max()) {
        return std::nullopt;
      }
      options.warmup = static_cast<std::size_t>(*parsed);
    } else if (argument == "--repetitions") {
      const auto parsed = parse_unsigned(value);
      if (!parsed || *parsed == 0 ||
          *parsed > std::numeric_limits<std::size_t>::max()) {
        return std::nullopt;
      }
      options.repetitions = static_cast<std::size_t>(*parsed);
    } else if (argument == "--cpu") {
      const auto parsed = parse_unsigned(value);
      if (!parsed || *parsed > static_cast<std::uint64_t>(CPU_SETSIZE - 1)) {
        return std::nullopt;
      }
      options.cpu = static_cast<int>(*parsed);
    } else if (argument == "--json") {
      options.json_path = value;
    } else if (argument == "--sibling-occupancy") {
      options.sibling_occupancy = value;
    } else if (argument == "--report") {
      options.report_path = value;
    } else if (argument == "--allocation-input") {
      options.allocation_input = value;
    } else if (argument == "--allocation-output") {
      options.allocation_output = value;
    } else if (argument == "--provenance") {
      options.provenance_path = value;
    } else {
      return std::nullopt;
    }
  }

  if (options.mode != "smoke" && options.mode != "exploratory" &&
      options.mode != "acceptance") {
    return std::nullopt;
  }
  if (options.seeds.empty()) {
    options.seeds = options.mode == "smoke"
                        ? std::vector<std::uint64_t>{0x5eed}
                        : std::vector<std::uint64_t>{
                              lob::benchmark::kCanonicalSeeds.begin(),
                              lob::benchmark::kCanonicalSeeds.end()};
  }
  if (options.repetitions == 0) {
    options.repetitions = options.mode == "smoke"
                              ? 1
                              : lob::benchmark::kCanonicalRepetitions;
  }
  if (options.warmup == 0) {
    options.warmup = options.mode == "smoke"
                         ? 100
                         : lob::benchmark::kCanonicalWarmup;
  }
  const lob::benchmark::ExperimentConfiguration configuration{
      options.mode,          options.workload, options.seeds,
      options.samples_override, options.warmup,   options.repetitions,
      options.cpu};
  if (options.mode == "acceptance" &&
      (!lob::benchmark::canonical_acceptance_configuration(configuration) ||
       options.sibling_occupancy == "not_observed")) {
    return std::nullopt;
  }
#ifdef LOB_ENABLE_ALLOCATION_AUDIT
  if (options.allocation_output.empty()) {
    return std::nullopt;
  }
#else
  if (options.mode == "acceptance" &&
      (options.allocation_input.empty() || options.json_path.empty() ||
       options.report_path.empty())) {
    return std::nullopt;
  }
#endif
  return options;
}

[[nodiscard]] std::vector<std::string> selected_workloads(
    const std::string& selection) {
  static const std::vector<std::string> all{
      lob::benchmark::kCanonicalWorkloads.begin(),
      lob::benchmark::kCanonicalWorkloads.end()};
  if (selection == "all") {
    return all;
  }
  if (std::find(all.begin(), all.end(), selection) == all.end()) {
    return {};
  }
  return {selection};
}

[[nodiscard]] std::size_t default_samples(const std::string& workload,
                                          const Options& options) {
  if (options.samples_override != 0) {
    return options.samples_override;
  }
  if (options.mode == "smoke") {
    return 1'000;
  }
  return lob::benchmark::canonical_sample_count(workload);
}

[[nodiscard]] std::int64_t top_price(lob::Side side,
                                     std::mt19937_64& generator) {
  const auto offset = static_cast<std::int64_t>(generator() % 5);
  return side == lob::Side::Buy ? 999 - offset : 1001 + offset;
}

[[nodiscard]] std::int64_t outside_price(lob::Side side,
                                         std::mt19937_64& generator,
                                         std::normal_distribution<double>&
                                             distribution) {
  const auto sampled = static_cast<std::int64_t>(std::llround(distribution(generator)));
  if (side == lob::Side::Buy) {
    return std::clamp(sampled, std::int64_t{950}, std::int64_t{994});
  }
  return std::clamp(std::int64_t{2000} - sampled, std::int64_t{1006},
                    std::int64_t{1050});
}

struct LiveOrder final {
  std::uint64_t id{};
  lob::Side side{lob::Side::Invalid};
  std::int64_t price{};
  std::uint64_t leaves{};
};

[[nodiscard]] Trace make_mixed_trace(std::size_t sample_count,
                                     std::size_t warmup_count,
                                     std::uint64_t seed) {
  Trace trace;
  trace.workload = "mixed";
  trace.command_to_outbox_observed = true;
  trace.commands.reserve(sample_count);
  trace.blocks.resize(1);
  auto& block = trace.blocks.front();
  block.first_command = 0;
  block.command_count = sample_count;
  block.initial_orders.reserve(6'000);

  const auto schedule =
      lob::benchmark::deterministic_mixed_schedule(sample_count, seed);
  trace.mixed_counts = lob::benchmark::count_operations(schedule);
  const auto total_crosses = trace.mixed_counts.cross + warmup_count + 100;
  block.initial_orders.push_back(
      {oid(1), instrument(), lob::Side::Buy, ticks(999), qty(total_crosses)});
  block.initial_orders.push_back(
      {oid(2), instrument(), lob::Side::Sell, ticks(1001), qty(total_crosses)});
  trace.top_five_volume += total_crosses * 2;
  trace.priced_volume += total_crosses * 2;

  std::mt19937_64 generator(seed ^ 0xa5a5a5a5ULL);
  std::normal_distribution<double> distribution(970.0, 10.0);
  std::vector<LiveOrder> live;
  live.reserve(sample_count / 5 + 6'000);
  std::uint64_t next_id = 3;
  for (std::size_t index = 0; index < 5'998; ++index) {
    const auto side = index % 2 == 0 ? lob::Side::Buy : lob::Side::Sell;
    const bool near_top = index % 10 < 8;
    const auto order_price = near_top ? top_price(side, generator)
                                      : outside_price(side, generator,
                                                      distribution);
    block.initial_orders.push_back(
        {oid(next_id), instrument(), side, ticks(order_price), qty(10)});
    live.push_back({next_id, side, order_price, 10});
    trace.priced_volume += 10;
    if (near_top) {
      trace.top_five_volume += 10;
    }
    ++next_id;
  }

  std::size_t minimum_active = 6'000;
  std::size_t maximum_active = 6'000;
  std::size_t active_count = 6'000;
  std::uint64_t placement_index = 0;
  std::uint64_t cross_index = 0;
  for (const auto operation : schedule) {
    const auto selected = static_cast<std::size_t>(generator() % live.size());
    if (operation == lob::benchmark::MixedOperation::Cancel) {
      trace.commands.push_back(make_cancel(live[selected].id));
      live[selected] = live.back();
      live.pop_back();
      --active_count;
    } else if (operation == lob::benchmark::MixedOperation::NonCrossingAdd) {
      const auto side = generator() % 2 == 0 ? lob::Side::Buy : lob::Side::Sell;
      const bool near_top = placement_index % 10 < 8;
      ++placement_index;
      const auto order_price = near_top ? top_price(side, generator)
                                        : outside_price(side, generator,
                                                        distribution);
      trace.commands.push_back(make_new(next_id, side, order_price, 10));
      live.push_back({next_id, side, order_price, 10});
      trace.priced_volume += 10;
      if (near_top) {
        trace.top_five_volume += 10;
      }
      ++next_id;
      ++active_count;
    } else if (operation == lob::benchmark::MixedOperation::Cross) {
      const auto side = cross_index++ % 2 == 0 ? lob::Side::Buy
                                               : lob::Side::Sell;
      trace.commands.push_back(
          make_new(next_id++, side, side == lob::Side::Buy ? 1001 : 999, 1,
                   1));
      ++trace.top_five_volume;
      ++trace.priced_volume;
    } else {
      auto& target = live[selected];
      std::uint64_t new_leaves = target.leaves;
      if (operation == lob::benchmark::MixedOperation::Reduce) {
        new_leaves = target.leaves > 1 ? target.leaves - 1 : target.leaves;
      } else if (operation == lob::benchmark::MixedOperation::Increase) {
        new_leaves = target.leaves + 1;
      }
      trace.commands.push_back(
          make_amend(target.id, target.price, new_leaves));
      target.leaves = new_leaves;
      trace.priced_volume += new_leaves;
      if ((target.side == lob::Side::Buy && target.price >= 995) ||
          (target.side == lob::Side::Sell && target.price <= 1005)) {
        trace.top_five_volume += new_leaves;
      }
    }
    minimum_active = std::min(minimum_active, active_count);
    maximum_active = std::max(maximum_active, active_count);
  }
  trace.expected_min_active = minimum_active;
  trace.expected_max_active = maximum_active;
  trace.target_precondition_orders = 6'000;
  std::array<bool, 101> populated_prices{};
  for (const auto& order : block.initial_orders) {
    const auto price = order.limit_price.value();
    if (price >= 950 && price <= 1050) {
      populated_prices[static_cast<std::size_t>(price - 950)] = true;
    }
  }
  trace.target_precondition_levels = static_cast<std::size_t>(
      std::count(populated_prices.begin(), populated_prices.end(), true));
  trace.nominal_fills = 0;
  return trace;
}

[[nodiscard]] std::uint16_t fills_for_workload(const std::string& workload) {
  if (workload == "fill1") {
    return 1;
  }
  if (workload == "fill4") {
    return 4;
  }
  if (workload == "fill16" || workload == "multi_level") {
    return 16;
  }
  if (workload == "fill64") {
    return 64;
  }
  if (workload == "fill256") {
    return 256;
  }
  return 0;
}

[[nodiscard]] bool distributions_valid(
    const Trace& trace, const RepetitionResult& repetition,
    std::size_t sample_count) noexcept {
  if (trace.workload == "mixed") {
    const auto crossing = trace.mixed_counts.cross;
    const auto non_crossing =
        static_cast<std::uint64_t>(sample_count) - crossing;
    return repetition.fill_distribution[0] == non_crossing &&
           repetition.fill_distribution[1] == crossing &&
           repetition.event_distribution[0] == non_crossing &&
           repetition.event_distribution[1] == crossing;
  }
  return repetition.fill_distribution[trace.nominal_fills] == sample_count &&
         repetition.event_distribution[trace.nominal_fills] == sample_count;
}

[[nodiscard]] Trace make_isolated_trace(const std::string& workload,
                                        std::size_t sample_count,
                                        std::uint64_t seed) {
  Trace trace;
  trace.workload = workload;
  trace.commands.reserve(sample_count);
  const auto fills = fills_for_workload(workload);
  trace.nominal_fills = fills;
  trace.command_to_outbox_observed = fills != 0;
  Block block;
  block.first_command = 0;
  block.command_count = sample_count;
  const auto base_id =
      std::uint64_t{1'000} + (seed % std::uint64_t{1'000'000}) * 300;

  Command target;
  if (workload == "cancel") {
    block.initial_orders.push_back({oid(base_id), instrument(), lob::Side::Buy,
                                    ticks(990), qty(10)});
    target = make_cancel(base_id);
    block.repopulate_after_command = true;
  } else if (workload == "unknown_cancel") {
    target = make_cancel(base_id, lob::OrderBookResult::OrderNotFound);
  } else if (workload == "reduce" || workload == "increase" ||
             workload == "noop") {
    const auto initial = workload == "increase" ? 10U : 11U;
    const auto amended = workload == "reduce" ? 10U
                         : workload == "increase" ? 11U
                                                  : 11U;
    block.initial_orders.push_back({oid(base_id), instrument(), lob::Side::Buy,
                                    ticks(990), qty(initial)});
    target = make_amend(base_id, 990, amended);
    if (workload != "noop") {
      block.repair_command = make_amend(base_id, 990, initial);
    }
  } else if (workload == "unknown_amend") {
    target = make_amend(base_id, 990, 10,
                        lob::OrderBookResult::OrderNotFound);
  } else if (workload == "noncross_add") {
    const auto side = seed % 2 == 0 ? lob::Side::Buy : lob::Side::Sell;
    target = make_new(base_id, side,
                      side == lob::Side::Buy ? 990 : 1010, 10);
    block.repair_command = make_cancel(base_id);
  } else {
    const auto aggressive_id = base_id;
    block.initial_orders.reserve(fills);
    for (std::uint16_t fill = 0; fill < fills; ++fill) {
      const auto level_offset = workload == "multi_level"
                                    ? static_cast<std::int64_t>(fill / 4)
                                    : std::int64_t{0};
      block.initial_orders.push_back(
          {oid(base_id + 1 + fill), instrument(), lob::Side::Sell,
           ticks(1000 + level_offset), qty(1)});
    }
    target = make_new(aggressive_id, lob::Side::Buy,
                      workload == "multi_level" ? 1003 : 1000, fills, fills);
    block.repopulate_after_command = true;
  }
  trace.commands.assign(sample_count, target);
  trace.blocks.push_back(std::move(block));
  trace.expected_min_active = 0;
  trace.expected_max_active = fills == 0 ? std::size_t{1} : fills;
  trace.target_precondition_orders = fills == 0 ? std::size_t{1} : fills;
  if (workload == "unknown_cancel" || workload == "unknown_amend" ||
      workload == "noncross_add") {
    trace.target_precondition_orders = 0;
  }
  trace.target_precondition_levels =
      trace.target_precondition_orders == 0
          ? 0
          : (workload == "multi_level" ? std::size_t{4} : std::size_t{1});
  return trace;
}

[[nodiscard]] Trace make_trace(const std::string& workload,
                               std::size_t sample_count,
                               std::size_t warmup_count,
                               std::uint64_t seed) {
  const auto before = allocation_audit::snapshot(
      allocation_audit::Phase::TraceGeneration);
  Trace trace;
  {
    allocation_audit::Guard phase(
        allocation_audit::Phase::TraceGeneration);
    trace = workload == "mixed"
                ? make_mixed_trace(sample_count, warmup_count, seed)
                : make_isolated_trace(workload, sample_count, seed);
    for (const auto& command : trace.commands) {
      trace.trace_checksum = lob::benchmark::checksum_mix(
          trace.trace_checksum, static_cast<std::uint64_t>(command.type));
      trace.trace_checksum = lob::benchmark::checksum_mix(
          trace.trace_checksum, command.order_id.value());
      trace.trace_checksum = lob::benchmark::checksum_mix(
          trace.trace_checksum, static_cast<std::uint64_t>(command.side));
      trace.trace_checksum = lob::benchmark::checksum_mix(
          trace.trace_checksum,
          static_cast<std::uint64_t>(command.price.value()));
      trace.trace_checksum = lob::benchmark::checksum_mix(
          trace.trace_checksum, command.quantity.value());
      trace.trace_checksum = lob::benchmark::checksum_mix(
          trace.trace_checksum,
          static_cast<std::uint64_t>(command.expected_result));
      trace.trace_checksum = lob::benchmark::checksum_mix(
          trace.trace_checksum, command.expected_reports);
      if (command.type != CommandType::Cancel) {
        const auto quantity = command.quantity.value();
        const auto bucket = static_cast<std::size_t>(
            std::min<std::uint64_t>(quantity, 64));
        ++trace.command_quantity_distribution[bucket];
        trace.command_quantity_volume += quantity;
      }
    }
    for (const auto& block : trace.blocks) {
      trace.trace_checksum = lob::benchmark::checksum_mix(
          trace.trace_checksum, block.repopulate_after_command ? 1 : 0);
      if (block.repair_command.has_value()) {
        trace.trace_checksum = lob::benchmark::checksum_mix(
            trace.trace_checksum,
            static_cast<std::uint64_t>(block.repair_command->type));
        trace.trace_checksum = lob::benchmark::checksum_mix(
            trace.trace_checksum, block.repair_command->order_id.value());
        trace.trace_checksum = lob::benchmark::checksum_mix(
            trace.trace_checksum,
            static_cast<std::uint64_t>(block.repair_command->price.value()));
        trace.trace_checksum = lob::benchmark::checksum_mix(
            trace.trace_checksum, block.repair_command->quantity.value());
      }
      for (const auto& order : block.initial_orders) {
        trace.trace_checksum = lob::benchmark::checksum_mix(
            trace.trace_checksum, order.order_id.value());
        trace.trace_checksum = lob::benchmark::checksum_mix(
            trace.trace_checksum, static_cast<std::uint64_t>(order.side));
        trace.trace_checksum = lob::benchmark::checksum_mix(
            trace.trace_checksum,
            static_cast<std::uint64_t>(order.limit_price.value()));
        trace.trace_checksum = lob::benchmark::checksum_mix(
            trace.trace_checksum, order.quantity.value());
      }
    }
  }
  trace.trace_allocations =
      allocation_audit::snapshot(allocation_audit::Phase::TraceGeneration) -
      before;
  return trace;
}

[[nodiscard]] lob::NewOrderResult apply_command(lob::MatchingEngine& engine,
                                                const Command& command) {
  if (command.type == CommandType::New) {
    return engine.process(lob::NewOrder{command.order_id, instrument(),
                                        command.side, command.price,
                                        command.quantity});
  }
  if (command.type == CommandType::Cancel) {
    return engine.process(lob::CancelOrder{command.order_id, instrument()});
  }
  return engine.process(lob::AmendOrder{command.order_id, instrument(),
                                        command.price, command.quantity});
}

void checksum_result(std::uint64_t& checksum,
                     const lob::NewOrderResult& result) noexcept {
  checksum = lob::benchmark::checksum_mix(
      checksum, static_cast<std::uint64_t>(result.result));
  checksum = lob::benchmark::checksum_mix(
      checksum, result.command_sequence.value());
  checksum = lob::benchmark::checksum_mix(
      checksum, result.execution_report_count);
  for (const auto& report : result.reports()) {
    checksum = lob::benchmark::checksum_mix(checksum, report.match_id.value());
    checksum = lob::benchmark::checksum_mix(
        checksum, report.resting_order_id.value());
    checksum = lob::benchmark::checksum_mix(
        checksum, report.aggressive_order_id.value());
    checksum = lob::benchmark::checksum_mix(
        checksum, static_cast<std::uint64_t>(report.match_price.value()));
    checksum = lob::benchmark::checksum_mix(
        checksum, report.match_quantity.value());
    checksum = lob::benchmark::checksum_mix(
        checksum, report.engine_sequence.value());
  }
}

[[nodiscard]] bool populate(lob::MatchingEngine& engine,
                            std::span<const lob::NewOrder> orders) {
  for (const auto& order : orders) {
    const auto result = engine.process(order);
    if (result.result != lob::OrderBookResult::Accepted ||
        !result.reports().empty()) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] std::span<const lob::NewOrder> initial_population(
    const Block& block) noexcept {
  return block.initial_orders;
}

[[nodiscard]] bool restore_precondition(lob::MatchingEngine& engine,
                                        const Block& block) {
  if (block.repair_command.has_value()) {
    const auto result = apply_command(engine, *block.repair_command);
    if (result.result != lob::OrderBookResult::Accepted ||
        !result.reports().empty() ||
        engine.pending_execution_report_count() != 0) {
      return false;
    }
  }
  return !block.repopulate_after_command ||
         populate(engine, initial_population(block));
}

[[nodiscard]] bool consume_reports(lob::MatchingEngine& engine,
                                   std::span<const lob::ExecutionReport> expected,
                                   std::uint64_t& checksum) noexcept {
  std::size_t observed = 0;
  lob::ExecutionReport report;
  while (engine.try_consume_execution_report(report)) {
    if (observed >= expected.size()) {
      return false;
    }
    const auto& wanted = expected[observed];
    if (report.match_id != wanted.match_id ||
        report.instrument_id != wanted.instrument_id ||
        report.aggressive_order_id != wanted.aggressive_order_id ||
        report.resting_order_id != wanted.resting_order_id ||
        report.match_price != wanted.match_price ||
        report.match_quantity != wanted.match_quantity ||
        report.engine_sequence != wanted.engine_sequence) {
      return false;
    }
    checksum = lob::benchmark::checksum_mix(checksum, report.match_id.value());
    checksum = lob::benchmark::checksum_mix(
        checksum, report.engine_sequence.value());
    ++observed;
  }
  return observed == expected.size();
}

[[nodiscard]] bool reports_match_workload(
    const Trace& trace, const Command& command,
    const lob::NewOrderResult& result) noexcept {
  for (std::size_t index = 0; index < result.reports().size(); ++index) {
    const auto& report = result.reports()[index];
    const bool price_valid =
        trace.workload == "mixed"
            ? (report.match_price == ticks(999) ||
               report.match_price == ticks(1001))
            : report.match_price ==
                  ticks(trace.workload == "multi_level"
                            ? std::int64_t{1000} +
                                  static_cast<std::int64_t>(index / 4)
                            : std::int64_t{1000});
    if (report.aggressive_order_id != command.order_id ||
        report.match_quantity != qty(1) ||
        !price_valid) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] bool warm_trace(const Trace& trace, std::size_t warmup_count) {
  std::size_t remaining = warmup_count;
  while (remaining != 0) {
    for (const auto& block : trace.blocks) {
      if (remaining == 0) {
        break;
      }
      std::unique_ptr<lob::MatchingEngine> engine;
      {
        allocation_audit::Guard phase(allocation_audit::Phase::Construction);
        engine = std::make_unique<lob::MatchingEngine>(instrument());
      }
      {
        allocation_audit::Guard phase(
            allocation_audit::Phase::InitialPopulation);
        if (!populate(*engine, initial_population(block))) {
          return false;
        }
      }
      const auto count = std::min(remaining, block.command_count);
      {
        allocation_audit::Guard phase(allocation_audit::Phase::Warmup);
        std::uint64_t checksum = 0;
        for (std::size_t offset = 0; offset < count; ++offset) {
          const auto& command = trace.commands[block.first_command + offset];
          const auto result = apply_command(*engine, command);
          if (result.result != command.expected_result ||
              result.reports().size() != command.expected_reports ||
              !reports_match_workload(trace, command, result) ||
              !consume_reports(*engine, result.reports(), checksum)) {
            return false;
          }
          if (offset + 1 < count &&
              !restore_precondition(*engine, block)) {
            return false;
          }
        }
      }
      {
        allocation_audit::Guard phase(
            allocation_audit::Phase::Destruction);
        engine.reset();
      }
      remaining -= count;
    }
  }
  return true;
}

[[nodiscard]] RepetitionResult run_repetition(const Trace& trace,
                                              std::size_t warmup_count,
                                              bool collect_latency) {
  RepetitionResult result;
  result.minimum_active = std::numeric_limits<std::size_t>::max();
  result.starting_cpu = sched_getcpu();
  const auto construction_before = allocation_audit::snapshot(
      allocation_audit::Phase::Construction);
  const auto population_before = allocation_audit::snapshot(
      allocation_audit::Phase::InitialPopulation);
  const auto warmup_before =
      allocation_audit::snapshot(allocation_audit::Phase::Warmup);
  const auto process_before = allocation_audit::snapshot(
      allocation_audit::Phase::TimedProcess);
  const auto collection_before = allocation_audit::snapshot(
      allocation_audit::Phase::TimedCollection);
  const auto destruction_before = allocation_audit::snapshot(
      allocation_audit::Phase::Destruction);
  std::vector<std::uint64_t> samples;
  {
    allocation_audit::Guard phase(allocation_audit::Phase::Construction);
    samples.resize(trace.commands.size());
  }

  if (!warm_trace(trace, warmup_count)) {
    result.lifecycle_transitions = 1;
    return result;
  }

  std::size_t sample_index = 0;
  for (const auto& block : trace.blocks) {
    std::unique_ptr<lob::MatchingEngine> engine;
    {
      allocation_audit::Guard phase(allocation_audit::Phase::Construction);
      engine = std::make_unique<lob::MatchingEngine>(instrument());
    }
    {
      allocation_audit::Guard phase(
          allocation_audit::Phase::InitialPopulation);
      if (!populate(*engine, initial_population(block))) {
        result.lifecycle_transitions = 1;
        return result;
      }
    }

    for (std::size_t offset = 0; offset < block.command_count; ++offset) {
      const auto& command = trace.commands[block.first_command + offset];
      lob::NewOrderResult command_result;
      std::uint64_t elapsed = 0;
      {
        allocation_audit::Guard phase(
            allocation_audit::Phase::TimedProcess);
        if (collect_latency) {
          asm volatile("" ::: "memory");
          const auto start = Clock::now();
          command_result = apply_command(*engine, command);
          const auto finish = Clock::now();
          asm volatile("" ::: "memory");
          elapsed = static_cast<std::uint64_t>(
              std::chrono::duration_cast<std::chrono::nanoseconds>(finish -
                                                                    start)
                  .count());
        } else {
          command_result = apply_command(*engine, command);
        }
      }
      {
        allocation_audit::Guard phase(
            allocation_audit::Phase::TimedCollection);
        samples[sample_index++] = elapsed;
        if (command_result.result == lob::OrderBookResult::Accepted) {
          ++result.accepted;
        } else {
          ++result.rejected;
        }
        if (command_result.result == lob::OrderBookResult::LosslessOutboxFull ||
            command_result.result == lob::OrderBookResult::StatusOutboxFull) {
          ++result.unintended_outbox_failures;
        }
        if (command_result.result != command.expected_result ||
            command_result.reports().size() != command.expected_reports ||
            !reports_match_workload(trace, command, command_result)) {
          ++result.lifecycle_transitions;
        }
        ++result.fill_distribution[command_result.reports().size()];
        ++result.event_distribution[command_result.reports().size()];
        checksum_result(result.checksum, command_result);
        if (!consume_reports(*engine, command_result.reports(),
                             result.checksum)) {
          ++result.unintended_outbox_failures;
        }
        const auto active = engine->active_order_count();
        const auto levels = engine->price_level_count(lob::Side::Buy) +
                            engine->price_level_count(lob::Side::Sell);
        result.minimum_active = std::min(result.minimum_active, active);
        result.maximum_active = std::max(result.maximum_active, active);
        if (sample_index == 1) {
          result.minimum_levels = levels;
        } else {
          result.minimum_levels = std::min(result.minimum_levels, levels);
        }
        result.maximum_levels = std::max(result.maximum_levels, levels);
        if (engine->instrument_state() != lob::InstrumentState::Active) {
          ++result.lifecycle_transitions;
        }
      }
      if (offset + 1 < block.command_count &&
          (block.repair_command.has_value() ||
           block.repopulate_after_command)) {
        allocation_audit::Guard phase(
            allocation_audit::Phase::InitialPopulation);
        if (!restore_precondition(*engine, block)) {
          ++result.lifecycle_transitions;
        }
      }
    }
    {
      allocation_audit::Guard phase(
          allocation_audit::Phase::TimedCollection);
      if (!engine->validate_invariants() ||
          engine->pending_execution_report_count() != 0 ||
          engine->pending_status_event_count() != 0) {
        ++result.lifecycle_transitions;
      }
    }
    const auto diagnostics = engine->storage_diagnostics();
    result.storage = diagnostics;
    {
      allocation_audit::Guard phase(allocation_audit::Phase::Destruction);
      engine.reset();
    }
  }

  const auto process_after = allocation_audit::snapshot(
      allocation_audit::Phase::TimedProcess);
  const auto collection_after = allocation_audit::snapshot(
      allocation_audit::Phase::TimedCollection);
  const auto statistics_before = allocation_audit::snapshot(
      allocation_audit::Phase::PostStatistics);
  {
    allocation_audit::Guard phase(
        allocation_audit::Phase::PostStatistics);
    result.statistics = lob::benchmark::summarize_latencies(samples);
  }
  result.public_gate =
      lob::benchmark::evaluate_public_path_gate(result.statistics);
  result.matching_core_threshold =
      lob::benchmark::evaluate_matching_core_threshold(result.statistics);
  result.multi_fill_threshold =
      trace.nominal_fills == 0 ||
      result.statistics.p99_ns <= lob::benchmark::multi_fill_p99_ceiling_ns(
                                      trace.nominal_fills,
                                      trace.nominal_fills);
  result.ending_cpu = sched_getcpu();
  result.allocations.construction =
      allocation_audit::snapshot(allocation_audit::Phase::Construction) -
      construction_before;
  result.allocations.initial_population =
      allocation_audit::snapshot(
          allocation_audit::Phase::InitialPopulation) -
      population_before;
  result.allocations.warmup =
      allocation_audit::snapshot(allocation_audit::Phase::Warmup) -
      warmup_before;
  result.allocations.timed_process =
      process_after - process_before;
  result.allocations.timed_collection =
      collection_after - collection_before;
  result.allocations.post_statistics =
      allocation_audit::snapshot(allocation_audit::Phase::PostStatistics) -
      statistics_before;
  result.allocations.destruction =
      allocation_audit::snapshot(allocation_audit::Phase::Destruction) -
      destruction_before;
  if (result.minimum_active == std::numeric_limits<std::size_t>::max()) {
    result.minimum_active = 0;
  }
  return result;
}

[[nodiscard]] lob::benchmark::LatencyStatistics median_statistics(
    const std::vector<RepetitionResult>& repetitions) {
  lob::benchmark::LatencyStatistics result;
  if (repetitions.empty()) {
    return result;
  }
  const auto median_u64 = [&repetitions](auto member) {
    std::vector<std::uint64_t> values;
    values.reserve(repetitions.size());
    for (const auto& repetition : repetitions) {
      values.push_back(repetition.statistics.*member);
    }
    std::sort(values.begin(), values.end());
    return values[values.size() / 2];
  };
  const auto median_double = [&repetitions](auto member) {
    std::vector<double> values;
    values.reserve(repetitions.size());
    for (const auto& repetition : repetitions) {
      values.push_back(repetition.statistics.*member);
    }
    std::sort(values.begin(), values.end());
    return values[values.size() / 2];
  };
  result.sample_count = median_u64(&lob::benchmark::LatencyStatistics::sample_count);
  result.p50_ns = median_u64(&lob::benchmark::LatencyStatistics::p50_ns);
  result.p90_ns = median_u64(&lob::benchmark::LatencyStatistics::p90_ns);
  result.p99_ns = median_u64(&lob::benchmark::LatencyStatistics::p99_ns);
  result.p999_ns = median_u64(&lob::benchmark::LatencyStatistics::p999_ns);
  result.p9999_ns = median_u64(&lob::benchmark::LatencyStatistics::p9999_ns);
  result.maximum_ns = median_u64(&lob::benchmark::LatencyStatistics::maximum_ns);
  result.total_timed_ns =
      median_u64(&lob::benchmark::LatencyStatistics::total_timed_ns);
  result.throughput_per_second =
      median_double(&lob::benchmark::LatencyStatistics::throughput_per_second);
  return result;
}

[[nodiscard]] bool pin_cpu(int cpu) noexcept {
  if (cpu < 0) {
    return true;
  }
  cpu_set_t mask;
  CPU_ZERO(&mask);
  CPU_SET(cpu, &mask);
  return sched_setaffinity(0, sizeof(mask), &mask) == 0;
}

#ifndef LOB_ENABLE_ALLOCATION_AUDIT
[[nodiscard]] std::string read_first_matching_line(const std::string& path,
                                                   std::string_view prefix) {
  std::ifstream input(path);
  std::string line;
  while (std::getline(input, line)) {
    if (line.starts_with(prefix)) {
      const auto colon = line.find(':');
      return colon == std::string::npos ? line : line.substr(colon + 1);
    }
  }
  return "unavailable";
}

[[nodiscard]] std::string read_small_file(const std::string& path) {
  std::ifstream input(path);
  if (!input) {
    return "unavailable";
  }
  std::string value;
  std::getline(input, value);
  return value;
}

[[nodiscard]] std::string affinity_string() {
  cpu_set_t mask;
  CPU_ZERO(&mask);
  if (sched_getaffinity(0, sizeof(mask), &mask) != 0) {
    return "unavailable";
  }
  std::ostringstream output;
  bool first = true;
  for (int cpu = 0; cpu < CPU_SETSIZE; ++cpu) {
    if (CPU_ISSET(cpu, &mask)) {
      if (!first) {
        output << ',';
      }
      output << cpu;
      first = false;
    }
  }
  return output.str();
}

[[nodiscard]] Environment collect_environment(int cpu,
                                              std::string sibling_occupancy) {
  Environment result;
  result.cpu_model = read_first_matching_line("/proc/cpuinfo", "model name");
  result.microcode = read_first_matching_line("/proc/cpuinfo", "microcode");
  utsname kernel{};
  if (uname(&kernel) == 0) {
    result.kernel = std::string{kernel.sysname} + ' ' + kernel.release + ' ' +
                    kernel.machine;
  } else {
    result.kernel = "unavailable";
  }
  result.compiler = __VERSION__;
  result.affinity = affinity_string();
  const auto effective_cpu = cpu >= 0 ? cpu : sched_getcpu();
  const auto cpu_path =
      "/sys/devices/system/cpu/cpu" + std::to_string(effective_cpu);
  result.sibling =
      read_small_file(cpu_path + "/topology/thread_siblings_list");
  result.sibling_occupancy = std::move(sibling_occupancy);
  result.governor =
      read_small_file(cpu_path + "/cpufreq/scaling_governor");
  result.frequency_khz =
      read_small_file(cpu_path + "/cpufreq/scaling_cur_freq");
  result.scaling_driver =
      read_small_file(cpu_path + "/cpufreq/scaling_driver");
  result.scaling_min_khz =
      read_small_file(cpu_path + "/cpufreq/scaling_min_freq");
  result.scaling_max_khz =
      read_small_file(cpu_path + "/cpufreq/scaling_max_freq");
  result.energy_performance_preference =
      read_small_file(cpu_path + "/cpufreq/energy_performance_preference");
  result.numa_node = "unavailable";
  for (int node = 0; node < 64; ++node) {
    const auto candidate = cpu_path + "/node" + std::to_string(node);
    if (std::filesystem::exists(candidate)) {
      result.numa_node = std::to_string(node);
      break;
    }
  }
  timespec resolution{};
  if (clock_getres(CLOCK_MONOTONIC, &resolution) == 0) {
    result.clock_resolution_ns =
        static_cast<std::uint64_t>(resolution.tv_sec) * 1'000'000'000ULL +
        static_cast<std::uint64_t>(resolution.tv_nsec);
  }
  std::vector<std::uint64_t> overhead(100'000);
  for (auto& sample : overhead) {
    const auto start = Clock::now();
    const auto finish = Clock::now();
    sample = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(finish - start)
            .count());
  }
  result.clock_overhead = lob::benchmark::summarize_latencies(overhead);
  return result;
}

[[nodiscard]] std::string json_escape(std::string_view value) {
  std::string result;
  result.reserve(value.size());
  for (const char character : value) {
    if (character == '"' || character == '\\') {
      result.push_back('\\');
    }
    result.push_back(character);
  }
  return result;
}

void write_counters_json(std::ostream& output,
                         const allocation_audit::Counters& counters) {
  output << "{\"allocations\":" << counters.allocations
         << ",\"allocated_bytes\":" << counters.allocated_bytes
         << ",\"deallocations\":" << counters.deallocations << '}';
}

template <std::size_t Size>
void write_distribution_json(
    std::ostream& output,
    const std::array<std::uint64_t, Size>& distribution) {
  output << '{';
  bool first = true;
  for (std::size_t index = 0; index < distribution.size(); ++index) {
    if (distribution[index] == 0) {
      continue;
    }
    if (!first) {
      output << ',';
    }
    output << '\"' << index << "\":" << distribution[index];
    first = false;
  }
  output << '}';
}
#endif

#ifdef LOB_ENABLE_ALLOCATION_AUDIT
[[nodiscard]] bool write_allocation_sidecar(
    const std::string& path, const Options& options,
    const lob::benchmark::ExecutionProvenance& provenance,
    const std::vector<WorkloadRun>& runs) {
  std::ofstream output(path);
  if (!output) {
    return false;
  }
  output << "LOB_PHASE2_POOL_ALLOCATION_V4 " << options.mode << ' '
         << options.repetitions << ' ' << options.warmup << ' '
         << options.samples_override << ' ' << options.workload << '\n';
  output << "PROVENANCE " << std::quoted(provenance.build.source_commit) << ' '
         << (provenance.build.source_dirty_at_build ? 1 : 0) << ' '
         << std::quoted(provenance.build.latency_sha256) << ' '
         << std::quoted(provenance.build.allocation_sha256) << '\n';
  for (const auto& run : runs) {
    output << "TRACE " << run.workload << ' ' << run.seed << ' '
           << run.samples << ' ' << run.warmup << ' '
           << run.trace.trace_checksum << ' '
           << run.trace.trace_allocations.allocations << ' '
           << run.trace.trace_allocations.allocated_bytes << ' '
           << run.trace.trace_allocations.deallocations << '\n';
    for (std::size_t index = 0; index < run.repetitions.size(); ++index) {
      const auto& phases = run.repetitions[index].allocations;
      output << "REP " << run.workload << ' ' << run.seed << ' ' << index
             << ' ' << run.samples << ' ' << run.warmup;
      for (const auto* counters :
           {&phases.construction, &phases.initial_population, &phases.warmup,
            &phases.timed_process, &phases.timed_collection,
            &phases.post_statistics, &phases.destruction}) {
        output << ' ' << counters->allocations << ' '
               << counters->allocated_bytes << ' '
               << counters->deallocations;
      }
      output << '\n';
    }
  }
  return true;
}
#else

[[nodiscard]] std::vector<AllocationKey> read_allocation_sidecar(
    const std::string& path, const Options& options,
    const lob::benchmark::ExecutionProvenance& provenance) {
  std::ifstream input(path);
  std::vector<AllocationKey> records;
  std::string schema;
  std::string mode;
  std::size_t repetitions = 0;
  std::size_t warmup = 0;
  std::size_t samples_override = 0;
  std::string workload;
  if (!(input >> schema >> mode >> repetitions >> warmup >> samples_override >>
        workload) ||
      schema != "LOB_PHASE2_POOL_ALLOCATION_V4" || mode != options.mode ||
      repetitions != options.repetitions || warmup != options.warmup ||
      samples_override != options.samples_override ||
      workload != options.workload) {
    return {};
  }
  std::string kind;
  std::string source_commit;
  int source_dirty = 1;
  std::string latency_sha256;
  std::string allocation_sha256;
  if (!(input >> kind >> std::quoted(source_commit) >> source_dirty >>
        std::quoted(latency_sha256) >> std::quoted(allocation_sha256)) ||
      kind != "PROVENANCE" ||
      source_commit != provenance.build.source_commit ||
      source_dirty != (provenance.build.source_dirty_at_build ? 1 : 0) ||
      latency_sha256 != provenance.build.latency_sha256 ||
      allocation_sha256 != provenance.build.allocation_sha256) {
    return {};
  }
  while (input >> kind) {
    if (kind == "TRACE") {
      AllocationKey record;
      record.repetition = std::numeric_limits<std::size_t>::max();
      input >> record.workload >> record.seed >> record.samples >>
          record.warmup >> record.trace_checksum >>
          record.phases.trace_generation.allocations >>
          record.phases.trace_generation.allocated_bytes >>
          record.phases.trace_generation.deallocations;
      if (!input) {
        return {};
      }
      const bool duplicate = std::any_of(
          records.begin(), records.end(), [&](const auto& existing) {
            return existing.workload == record.workload &&
                   existing.seed == record.seed &&
                   existing.repetition == record.repetition;
          });
      if (duplicate) {
        return {};
      }
      records.push_back(record);
      continue;
    }
    if (kind != "REP") {
      return {};
    }
    AllocationKey record;
    input >> record.workload >> record.seed >> record.repetition >>
        record.samples >> record.warmup;
    for (auto* counters :
         {&record.phases.construction, &record.phases.initial_population,
          &record.phases.warmup, &record.phases.timed_process,
          &record.phases.timed_collection,
          &record.phases.post_statistics, &record.phases.destruction}) {
      input >> counters->allocations >> counters->allocated_bytes >>
          counters->deallocations;
    }
    if (!input) {
      return {};
    }
    const bool duplicate =
        std::any_of(records.begin(), records.end(), [&](const auto& existing) {
          return existing.workload == record.workload &&
                 existing.seed == record.seed &&
                 existing.repetition == record.repetition;
        });
    if (duplicate) {
      return {};
    }
    records.push_back(record);
  }
  return records;
}

[[nodiscard]] const AllocationKey* find_allocation(
    const std::vector<AllocationKey>& records, const std::string& workload,
    std::uint64_t seed, std::size_t repetition, std::size_t samples,
    std::size_t warmup) noexcept {
  const auto found = std::find_if(
      records.begin(), records.end(), [&](const auto& record) {
        return record.workload == workload && record.seed == seed &&
               record.repetition == repetition && record.samples == samples &&
               record.warmup == warmup;
      });
  return found == records.end() ? nullptr : &*found;
}

[[nodiscard]] bool write_json(const std::string& path, const Options& options,
                              const Environment& environment,
                              const lob::benchmark::ExecutionProvenance&
                                  provenance,
                              const std::vector<WorkloadRun>& runs,
                              bool local_gates_passed) {
  std::ostringstream output;
  const bool allocation_audit_attached = !options.allocation_input.empty();
  allocation_audit::Counters benchmark_owned_timed_activity;
  allocation_audit::Counters process_timed_activity;
  bool run_validity_passed = true;
  bool allocation_policy_compliant = true;
  bool performance_gate_compliant = true;
  for (const auto& run : runs) {
    run_validity_passed = run_validity_passed && run.workload_valid;
    allocation_policy_compliant =
        allocation_policy_compliant && run.allocation_policy_compliant;
    performance_gate_compliant =
        performance_gate_compliant && run.performance_gate_compliant;
    for (const auto& repetition : run.repetitions) {
      process_timed_activity.allocations +=
          repetition.allocations.timed_process.allocations;
      process_timed_activity.allocated_bytes +=
          repetition.allocations.timed_process.allocated_bytes;
      process_timed_activity.deallocations +=
          repetition.allocations.timed_process.deallocations;
      benchmark_owned_timed_activity.allocations +=
          repetition.allocations.timed_collection.allocations;
      benchmark_owned_timed_activity.allocated_bytes +=
          repetition.allocations.timed_collection.allocated_bytes;
      benchmark_owned_timed_activity.deallocations +=
          repetition.allocations.timed_collection.deallocations;
    }
  }
  const bool benchmark_owned_timed_allocation_zero =
      benchmark_owned_timed_activity.allocations == 0 &&
      benchmark_owned_timed_activity.allocated_bytes == 0 &&
      benchmark_owned_timed_activity.deallocations == 0;
  const lob::benchmark::ExperimentConfiguration configuration{
      options.mode, options.workload, options.seeds, options.samples_override,
      options.warmup, options.repetitions, options.cpu};
  const bool canonical_configuration_valid =
      lob::benchmark::canonical_acceptance_configuration(configuration);
  const bool local_acceptance_passed =
      local_gates_passed && canonical_configuration_valid &&
      provenance.canonical_eligible;
  const lob::StorageDiagnostics storage =
      runs.empty() || runs.front().repetitions.empty()
          ? lob::StorageDiagnostics{}
          : runs.front().repetitions.front().storage;
  allocation_audit::Counters construction_total;
  allocation_audit::Counters destruction_total;
  for (const auto& run : runs) {
    for (const auto& repetition : run.repetitions) {
      construction_total.allocations +=
          repetition.allocations.construction.allocations;
      construction_total.allocated_bytes +=
          repetition.allocations.construction.allocated_bytes;
      construction_total.deallocations +=
          repetition.allocations.construction.deallocations;
      destruction_total.allocations +=
          repetition.allocations.destruction.allocations;
      destruction_total.allocated_bytes +=
          repetition.allocations.destruction.allocated_bytes;
      destruction_total.deallocations +=
          repetition.allocations.destruction.deallocations;
    }
  }
  output << std::fixed << std::setprecision(3)
         << "{\n  \"schema\": \"lob.phase2.pool.performance.v2\",\n"
         << "  \"baseline_profile\": \"phase2_pool_backed_storage\",\n"
         << "  \"primary_boundary\": \"public_process_completion\",\n"
         << "  \"matching_core_measured\": false,\n"
         << "  \"allocation_policy\": \"strict_total_zero\",\n"
         << "  \"allocation_audit_attached\": "
         << (allocation_audit_attached ? "true" : "false") << ",\n"
         << "  \"strict_zero_allocation_applicable\": true,\n"
         << "  \"strict_zero_allocation_enforcement_milestone\": 10,\n"
         << "  \"git_commit\": \""
         << json_escape(provenance.execution.execution_commit) << "\",\n"
         << "  \"git_dirty\": "
         << (provenance.execution.execution_tree_dirty ? "true" : "false")
         << ",\n"
         << "  \"build_type\": \""
         << json_escape(provenance.build.build_type) << "\",\n"
         << "  \"compile_flags\": \""
         << json_escape(provenance.build.latency_compile_flags) << "\",\n"
         << "  \"link_flags\": \""
         << json_escape(provenance.build.latency_link_flags) << "\",\n"
         << "  \"mode\": \"" << options.mode << "\",\n"
         << "  \"canonical_configuration_valid\":"
         << (canonical_configuration_valid ? "true" : "false") << ",\n"
         << "  \"local_acceptance_passed\":"
         << (local_acceptance_passed ? "true" : "false") << ",\n"
         << "  \"baseline_comparison_status\":\"not_performed\",\n"
         << "  \"final_canonical_acceptance\":false,\n"
         << "  \"canonical_seeds\":[24301,12648430],\n"
         << "  \"canonical_workload_count\":14,\n"
         << "  \"canonical_warmup\":10000,\n"
         << "  \"canonical_repetitions\":5,\n"
         << "  \"sample_count_override\":"
         << options.samples_override << ",\n"
         << "  \"execution_outbox_capacity\":"
         << lob::kDefaultExecutionOutboxCapacity << ",\n"
         << "  \"status_outbox_capacity\":"
         << lob::kDefaultControlOutboxCapacity << ",\n"
         << "  \"outbox_drain_policy\":"
            "\"after_each_timed_process_call_outside_timing\",\n"
         << "  \"trace_generation_completed_before_timing\":true,\n"
         << "  \"sample_and_result_buffers_presized_before_timing\":true,\n"
         << "  \"statistics_and_serialization_after_timing\":true,\n"
         << "  \"percentile_convention\":"
            "\"nearest_rank_per_repetition_median_of_five\",\n"
         << "  \"run_validity_passed\":"
         << (run_validity_passed ? "true" : "false") << ",\n"
         << "  \"allocation_policy_compliant\":"
         << (allocation_policy_compliant ? "true" : "false") << ",\n"
         << "  \"performance_gate_compliant\":"
         << (performance_gate_compliant ? "true" : "false") << ",\n"
         << "  \"public_process_completion_gate\":{"
            "\"p50_ns\":1500,\"p99_ns\":5000,\"p999_ns\":15000,"
            "\"throughput_per_second\":500000},\n"
         << "  \"provenance\":{\"manifest_loaded\":"
         << (provenance.manifest_loaded ? "true" : "false")
         << ",\"source_commit\":\""
         << json_escape(provenance.build.source_commit)
         << "\",\"source_dirty_at_build\":"
         << (provenance.build.source_dirty_at_build ? "true" : "false")
         << ",\"execution_commit\":\""
         << json_escape(provenance.execution.execution_commit)
         << "\",\"execution_tree_dirty\":"
         << (provenance.execution.execution_tree_dirty ? "true" : "false")
         << ",\"source_commit_matches\":"
         << (provenance.source_commit_matches ? "true" : "false")
         << ",\"latency_executable_sha256\":\""
         << provenance.build.latency_sha256
         << "\",\"allocation_audit_executable_sha256\":\""
         << provenance.build.allocation_sha256
         << "\",\"both_binary_hashes_match\":"
         << (provenance.both_binary_hashes_match ? "true" : "false")
         << ",\"executing_expected_binary\":"
         << (provenance.execution.executing_expected_binary ? "true"
                                                            : "false")
         << ",\"compiler_id\":\""
         << json_escape(provenance.build.compiler_id)
         << "\",\"compiler_version\":\""
         << json_escape(provenance.build.compiler_version)
         << "\",\"compiler_banner\":\""
         << json_escape(provenance.build.compiler_banner)
         << "\",\"latency_compile_command\":\""
         << json_escape(provenance.build.latency_compile_command)
         << "\",\"latency_compile_flags\":\""
         << json_escape(provenance.build.latency_compile_flags)
         << "\",\"latency_link_command\":\""
         << json_escape(provenance.build.latency_link_command)
         << "\",\"latency_link_flags\":\""
         << json_escape(provenance.build.latency_link_flags)
         << "\",\"allocation_compile_command\":\""
         << json_escape(provenance.build.allocation_compile_command)
         << "\",\"allocation_compile_flags\":\""
         << json_escape(provenance.build.allocation_compile_flags)
         << "\",\"allocation_link_command\":\""
         << json_escape(provenance.build.allocation_link_command)
         << "\",\"allocation_link_flags\":\""
         << json_escape(provenance.build.allocation_link_flags)
         << "\",\"release_configuration_valid\":"
         << (provenance.release_configuration_valid ? "true" : "false")
         << ",\"canonical_eligible\":"
         << (provenance.canonical_eligible ? "true" : "false") << "},\n"
         << "  \"storage_memory\":{\"active_order_capacity\":"
         << storage.configured_active_order_capacity
         << ",\"order_node_size\":" << storage.order_node_size
         << ",\"order_node_alignment\":" << storage.order_node_alignment
         << ",\"pool_slot_size\":" << storage.pool_slot_size
         << ",\"pool_slot_alignment\":" << storage.pool_slot_alignment
         << ",\"pool_slot_backing_bytes\":"
         << storage.pool_slot_backing_bytes
         << ",\"free_index_backing_bytes\":"
         << storage.free_index_backing_bytes
         << ",\"pool_backing_bytes\":" << storage.pool_backing_bytes
         << ",\"active_index_capacity\":" << storage.active_index_capacity
         << ",\"active_index_backing_bytes\":"
         << storage.active_index_backing_bytes
         << ",\"price_level_backing_bytes\":"
         << storage.price_level_backing_bytes
         << ",\"total_configured_storage_bytes\":"
         << storage.total_configured_storage_bytes << "},\n"
         << "  \"construction_allocation_totals\":{\"allocations\":"
         << construction_total.allocations << ",\"allocated_bytes\":"
         << construction_total.allocated_bytes << ",\"deallocations\":"
         << construction_total.deallocations << "},\n"
         << "  \"destruction_allocation_totals\":{\"allocations\":"
         << destruction_total.allocations << ",\"allocated_bytes\":"
         << destruction_total.allocated_bytes << ",\"deallocations\":"
         << destruction_total.deallocations << "},\n"
         << "  \"environment\": {\"cpu_model\":\""
         << json_escape(environment.cpu_model) << "\",\"microcode\":\""
         << json_escape(environment.microcode) << "\",\"kernel\":\""
         << json_escape(environment.kernel) << "\",\"compiler\":\""
         << json_escape(environment.compiler) << "\",\"affinity_mask\":\""
         << environment.affinity << "\",\"smt_sibling\":\""
         << environment.sibling << "\",\"sibling_occupancy\":\""
         << json_escape(environment.sibling_occupancy) << '"'
         << ",\"governor\":\"" << json_escape(environment.governor)
         << "\",\"frequency_khz\":\""
         << json_escape(environment.frequency_khz)
         << "\",\"scaling_driver\":\""
         << json_escape(environment.scaling_driver)
         << "\",\"scaling_min_khz\":\""
         << json_escape(environment.scaling_min_khz)
         << "\",\"scaling_max_khz\":\""
         << json_escape(environment.scaling_max_khz)
         << "\",\"energy_performance_preference\":\""
         << json_escape(environment.energy_performance_preference)
         << "\",\"numa_node\":\"" << json_escape(environment.numa_node)
         << "\",\"clock\":\"std::chrono::steady_clock/CLOCK_MONOTONIC\""
         << ",\"clock_resolution_ns\":" << environment.clock_resolution_ns
         << ",\"clock_overhead\":"
         << lob::benchmark::statistics_json(environment.clock_overhead)
         << ",\"overhead_subtracted\":false},\n"
         << "  \"workloads\": [\n";
  for (std::size_t run_index = 0; run_index < runs.size(); ++run_index) {
    const auto& run = runs[run_index];
    output << "    {\"name\":\"" << run.workload << "\",\"seed\":"
           << run.seed << ",\"samples\":" << run.samples
           << ",\"warmup\":" << run.warmup
           << ",\"repetitions\":" << run.repetitions.size()
           << ",\"command_to_outbox_observed\":"
           << (run.trace.command_to_outbox_observed ? "true" : "false")
           << ",\"matching_core_measured\":false"
           << ",\"full_path_within_matching_core_threshold\":"
           << (lob::benchmark::evaluate_matching_core_threshold(run.median)
                       .passed()
                   ? "true"
                   : "false")
           << ",\"nominal_fills\":" << run.trace.nominal_fills
           << ",\"multi_fill_threshold_applicable\":"
           << (run.trace.nominal_fills != 0 ? "true" : "false")
           << ",\"operation_counts\":{\"cancel\":"
           << run.trace.mixed_counts.cancel << ",\"reduce\":"
           << run.trace.mixed_counts.reduce << ",\"increase\":"
           << run.trace.mixed_counts.increase << ",\"noop\":"
           << run.trace.mixed_counts.no_op_amend << ",\"noncross_add\":"
           << run.trace.mixed_counts.non_crossing_add << ",\"cross\":"
           << run.trace.mixed_counts.cross << "}"
           << ",\"operation_percentages\":{\"cancel_and_amend\":"
           << lob::benchmark::percentage(
                  run.trace.mixed_counts.cancel_and_amend(),
                  run.trace.mixed_counts.total())
           << ",\"noncross_add\":"
           << lob::benchmark::percentage(
                  run.trace.mixed_counts.non_crossing_add,
                  run.trace.mixed_counts.total())
           << ",\"cross\":"
           << lob::benchmark::percentage(run.trace.mixed_counts.cross,
                                         run.trace.mixed_counts.total())
           << '}'
           << ",\"command_quantity_volume\":"
           << run.trace.command_quantity_volume
           << ",\"quantity_bucket_64_is_overflow\":true"
           << ",\"command_quantity_distribution\":";
    write_distribution_json(output, run.trace.command_quantity_distribution);
    output
           << ",\"top_five_volume\":" << run.trace.top_five_volume
           << ",\"priced_volume\":" << run.trace.priced_volume
           << ",\"top_five_volume_percent\":"
           << lob::benchmark::percentage(run.trace.top_five_volume,
                                         run.trace.priced_volume)
           << ",\"expected_active_range\":["
           << run.trace.expected_min_active << ','
           << run.trace.expected_max_active << ']'
           << ",\"target_precondition_orders\":"
           << run.trace.target_precondition_orders
           << ",\"target_precondition_levels\":"
           << run.trace.target_precondition_levels
           << ",\"trace_checksum\":" << run.trace.trace_checksum
           << ",\"trace_generation_allocations\":";
    write_counters_json(output, run.trace.trace_allocations);
    output
           << ",\"workload_valid\":"
           << (run.workload_valid ? "true" : "false")
           << ",\"allocation_policy_compliant\":"
           << (run.allocation_policy_compliant ? "true" : "false")
           << ",\"performance_gate_applicable\":"
           << (run.public_gate_applicable ? "true" : "false")
           << ",\"performance_gate_compliant\":"
           << (run.performance_gate_compliant ? "true" : "false")
           << ",\"public_gate_passes\":" << run.public_gate_passes
           << ",\"pool_high_water_count\":"
           << (run.repetitions.empty()
                   ? 0
                   : run.repetitions.front().storage.pool_high_water_count)
           << ",\"bid_level_high_water_count\":"
           << (run.repetitions.empty()
                   ? 0
                   : run.repetitions.front().storage
                         .bid_level_high_water_count)
           << ",\"ask_level_high_water_count\":"
           << (run.repetitions.empty()
                   ? 0
                   : run.repetitions.front().storage
                         .ask_level_high_water_count)
           << ",\"median\":" << lob::benchmark::statistics_json(run.median)
           << ",\"repetition_results\":[";
    for (std::size_t repetition = 0; repetition < run.repetitions.size();
         ++repetition) {
      const auto& value = run.repetitions[repetition];
      output << "{\"index\":" << repetition << ",\"statistics\":"
             << lob::benchmark::statistics_json(value.statistics)
             << ",\"public_gate_passed\":"
             << (value.public_gate.passed() ? "true" : "false")
             << ",\"public_gate_components\":{"
             << "\"p50\":" << (value.public_gate.p50 ? "true" : "false")
             << ",\"p99\":" << (value.public_gate.p99 ? "true" : "false")
             << ",\"p999\":"
             << (value.public_gate.p999 ? "true" : "false")
             << ",\"throughput\":"
             << (value.public_gate.throughput ? "true" : "false") << '}'
             << ",\"full_path_within_matching_core_threshold\":"
             << (value.matching_core_threshold.passed() ? "true" : "false")
             << ",\"multi_fill_threshold_passed\":"
             << (value.multi_fill_threshold ? "true" : "false")
             << ",\"multi_fill_core_ceiling_informational\":true"
             << ",\"checksum\":" << value.checksum
             << ",\"accepted\":" << value.accepted
             << ",\"rejected\":" << value.rejected
             << ",\"fill_count_distribution\":";
      write_distribution_json(output, value.fill_distribution);
      output << ",\"emitted_event_count_distribution\":";
      write_distribution_json(output, value.event_distribution);
      output
             << ",\"active_range\":[" << value.minimum_active << ','
             << value.maximum_active << "]"
             << ",\"level_range\":[" << value.minimum_levels << ','
             << value.maximum_levels << "]"
             << ",\"starting_cpu\":" << value.starting_cpu
             << ",\"ending_cpu\":" << value.ending_cpu
             << ",\"timed_allocations\":";
      write_counters_json(output, value.allocations.timed_process);
      output << ",\"timed_sample_collection_allocations\":";
      write_counters_json(output, value.allocations.timed_collection);
      output << ",\"phase_allocations\":{\"construction\":";
      write_counters_json(output, value.allocations.construction);
      output << ",\"initial_population\":";
      write_counters_json(output, value.allocations.initial_population);
      output << ",\"warmup\":";
      write_counters_json(output, value.allocations.warmup);
      output << ",\"post_statistics\":";
      write_counters_json(output, value.allocations.post_statistics);
      output << ",\"destruction\":";
      write_counters_json(output, value.allocations.destruction);
      output << "}}";
      if (repetition + 1 != run.repetitions.size()) {
        output << ',';
      }
    }
    output << "]}";
    if (run_index + 1 != runs.size()) {
      output << ',';
    }
    output << '\n';
  }
  output << "  ],\n  \"allocation_site_reconciliation\": ["
            "\"FIFO orders use startup-backed FixedObjectPool slots and intrusive links\","
            "\"active IDs use a startup-backed open-addressed table with backward-shift deletion\","
            "\"bid and ask levels use startup-backed sorted arrays\","
            "\"erase, fill, cancel, reprice, and reset return bounded storage without heap deallocation\"],\n"
         << "  \"timed_process_allocation_count\": "
         << process_timed_activity.allocations << ",\n"
         << "  \"timed_process_allocated_bytes\": "
         << process_timed_activity.allocated_bytes << ",\n"
         << "  \"timed_process_deallocation_count\": "
         << process_timed_activity.deallocations << ",\n"
         << "  \"timed_benchmark_owned_allocation_count\": "
         << benchmark_owned_timed_activity.allocations << ",\n"
         << "  \"timed_benchmark_owned_allocated_bytes\": "
         << benchmark_owned_timed_activity.allocated_bytes << ",\n"
         << "  \"timed_benchmark_owned_deallocation_count\": "
         << benchmark_owned_timed_activity.deallocations << ",\n"
         << "  \"timed_benchmark_owned_allocations_zero\": "
         << (benchmark_owned_timed_allocation_zero ? "true" : "false")
         << "\n}\n";
  if (path.empty()) {
    std::cout << output.str();
    return true;
  }
  std::filesystem::create_directories(
      std::filesystem::path(path).parent_path());
  std::ofstream file(path);
  file << output.str();
  return static_cast<bool>(file);
}

[[nodiscard]] bool write_report(const std::string& path,
                                const Options& options,
                                const Environment& environment,
                                const lob::benchmark::ExecutionProvenance&
                                    provenance,
                                const std::vector<WorkloadRun>& runs,
                                bool local_gates_passed) {
  if (path.empty()) {
    return true;
  }
  std::filesystem::create_directories(
      std::filesystem::path(path).parent_path());
  std::ofstream output(path);
  if (!output) {
    return false;
  }
  const lob::StorageDiagnostics storage =
      runs.empty() || runs.front().repetitions.empty()
          ? lob::StorageDiagnostics{}
          : runs.front().repetitions.front().storage;
  allocation_audit::Counters construction_total;
  allocation_audit::Counters destruction_total;
  const lob::benchmark::ExperimentConfiguration configuration{
      options.mode, options.workload, options.seeds, options.samples_override,
      options.warmup, options.repetitions, options.cpu};
  const bool locally_accepted =
      local_gates_passed &&
      lob::benchmark::canonical_acceptance_configuration(configuration) &&
      provenance.canonical_eligible;
  for (const auto& run : runs) {
    for (const auto& repetition : run.repetitions) {
      construction_total.allocations +=
          repetition.allocations.construction.allocations;
      construction_total.allocated_bytes +=
          repetition.allocations.construction.allocated_bytes;
      construction_total.deallocations +=
          repetition.allocations.construction.deallocations;
      destruction_total.allocations +=
          repetition.allocations.destruction.allocations;
      destruction_total.allocated_bytes +=
          repetition.allocations.destruction.allocated_bytes;
      destruction_total.deallocations +=
          repetition.allocations.destruction.deallocations;
    }
  }
  output << "# Phase 2 Pool-backed Storage Candidate\n\n"
         << "Profile: `phase2_pool_backed_storage`\n\n"
         << "Boundary: public `MatchingEngine::process()` entry through "
            "return\n\n"
         << "Canonical configuration valid: "
         << (lob::benchmark::canonical_acceptance_configuration(configuration)
                 ? "yes"
                 : "no")
         << "\n\n"
         << "Local acceptance: " << (locally_accepted ? "pass" : "fail")
         << "\n\n"
         << "Baseline comparison: not performed\n\n"
         << "Final canonical acceptance: no\n\n"
         << "A locally accepted candidate is not a final canonical result "
            "until the retained Phase 1 comparison passes."
         << "\n\n"
         << "The matching-core endpoint was not independently measured. "
            "Report-producing commands include outbox cursor publication; "
            "zero-event commands are process-completion measurements only.\n\n"
         << "## Method\n\n"
         << "Every command trace and bounded result buffer is constructed "
            "before timing. Each elapsed interval surrounds exactly one "
            "public `process()` call. Result checks, checksums, invariant "
            "queries, and execution-outbox draining occur after that "
            "interval; no repair command is included in a target latency. "
            "The mixed trace maintains 5,000--10,000 live orders with an "
            "exact 70/20/10 operation mix and at least 80% of priced volume "
            "near the top five levels. Unknown-ID paths are separate. "
            "Isolated cases restore the same target order state between "
            "calls with untimed cancellation, amendment, or repopulation. "
            "The multi-level case restores four orders at each of four "
            "prices outside timing, so every target really sweeps four "
            "levels.\n\n"
         << "## Environment\n\n"
         << "- Measured source: `" << provenance.build.source_commit
         << "` (dirty at build: "
         << (provenance.build.source_dirty_at_build ? "yes" : "no")
         << ", dirty at execution: "
         << (provenance.execution.execution_tree_dirty ? "yes" : "no")
         << ")\n"
         << "- Latency executable SHA-256: `"
         << provenance.build.latency_sha256 << "`\n"
         << "- Allocation-audit executable SHA-256: `"
         << provenance.build.allocation_sha256 << "`\n"
         << "- Runtime provenance verification: "
         << (provenance.canonical_eligible ? "canonical-eligible" : "diagnostic")
         << "\n"
         << "- CPU: " << environment.cpu_model << "\n"
         << "- Microcode: " << environment.microcode << "\n"
         << "- Kernel: " << environment.kernel << "\n"
         << "- Compiler: " << provenance.build.compiler_banner << "\n"
         << "- Compile flags: `" << provenance.build.latency_compile_flags
         << "`\n"
         << "- Link flags: `" << provenance.build.latency_link_flags << "`\n"
         << "- Affinity: " << environment.affinity << "\n"
         << "- SMT sibling: " << environment.sibling
         << " (" << environment.sibling_occupancy << ")\n"
         << "- Frequency policy: driver " << environment.scaling_driver
         << ", governor " << environment.governor << ", range "
         << environment.scaling_min_khz << "--"
         << environment.scaling_max_khz << " kHz, preference "
         << environment.energy_performance_preference << "\n"
         << "- Observed frequency: " << environment.frequency_khz << " kHz\n"
         << "- NUMA node: " << environment.numa_node << "\n"
         << "- Clock: `std::chrono::steady_clock` backed by monotonic clock; "
            "resolution "
         << environment.clock_resolution_ns << " ns; median call-pair overhead "
         << environment.clock_overhead.p50_ns
         << " ns; overhead was not subtracted.\n"
         << "- Mode: " << options.mode << ", repetitions: "
         << options.repetitions << "\n\n"
         << "## Configured storage\n\n"
         << "- Active-order capacity: "
         << storage.configured_active_order_capacity << "\n"
         << "- Order node: " << storage.order_node_size << " bytes, alignment "
         << storage.order_node_alignment << " bytes\n"
         << "- Pool slot: " << storage.pool_slot_size << " bytes, alignment "
         << storage.pool_slot_alignment << " bytes\n"
         << "- Pool backing (slots plus free indexes): "
         << storage.pool_backing_bytes << " bytes\n"
         << "  - Slot backing: " << storage.pool_slot_backing_bytes
         << " bytes\n"
         << "  - Free-index backing: " << storage.free_index_backing_bytes
         << " bytes\n"
         << "- Active-ID table: " << storage.active_index_capacity
         << " buckets, " << storage.active_index_backing_bytes << " bytes\n"
         << "- Bid/ask price-level arrays: "
         << storage.price_level_backing_bytes << " bytes\n"
         << "- Total configured storage backing: "
         << storage.total_configured_storage_bytes << " bytes\n\n"
         << "Canonical audit construction totals (engine, storage, outboxes, "
            "and pre-sized sample buffers): `"
         << construction_total.allocations << '/'
         << construction_total.allocated_bytes << '/'
         << construction_total.deallocations
         << "` allocations/bytes/deallocations. Destruction totals: `"
         << destruction_total.allocations << '/'
         << destruction_total.allocated_bytes << '/'
         << destruction_total.deallocations << "`.\n\n"
         << "## Results\n\n"
         << "| Workload | Seed | Samples | p50 ns | p90 ns | p99 ns | p99.9 ns | "
            "p99.99 ns | Max ns | Throughput/s | Gate |\n"
         << "| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | "
            "---: | --- |\n";
  for (const auto& run : runs) {
    output << "| " << run.workload << " | " << run.seed << " | "
           << run.samples << " | " << run.median.p50_ns << " | "
           << run.median.p90_ns << " | " << run.median.p99_ns << " | "
           << run.median.p999_ns << " | " << run.median.p9999_ns << " | "
           << run.median.maximum_ns << " | "
           << static_cast<std::uint64_t>(run.median.throughput_per_second)
           << " | "
           << (run.public_gate_applicable
                   ? (run.performance_gate_compliant ? "pass" : "fail")
                   : "informational")
           << " |\n";
  }
  output << "\n### Storage high-water evidence\n\n"
         << "| Workload | Seed | Pool | Bid levels | Ask levels |\n"
         << "| --- | ---: | ---: | ---: | ---: |\n";
  for (const auto& run : runs) {
    const auto high_water = run.repetitions.empty()
                                ? lob::StorageDiagnostics{}
                                : run.repetitions.front().storage;
    output << "| " << run.workload << " | " << run.seed << " | "
           << high_water.pool_high_water_count << " | "
           << high_water.bid_level_high_water_count << " | "
           << high_water.ask_level_high_water_count << " |\n";
  }
  output << "\n### Per-repetition evidence\n\n"
         << "| Workload | Seed | Rep | p50 | p90 | p99 | p99.9 | p99.99 | "
            "Max | Throughput/s | Public gate | Core upper-bound | Multi-fill "
            "upper-bound | Checksum |\n"
         << "| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | "
            "---: | --- | --- | --- | ---: |\n";
  for (const auto& run : runs) {
    for (std::size_t repetition = 0; repetition < run.repetitions.size();
         ++repetition) {
      const auto& value = run.repetitions[repetition];
      output << "| " << run.workload << " | " << run.seed << " | "
             << repetition << " | " << value.statistics.p50_ns << " | "
             << value.statistics.p90_ns << " | " << value.statistics.p99_ns
             << " | " << value.statistics.p999_ns << " | "
             << value.statistics.p9999_ns << " | "
             << value.statistics.maximum_ns << " | "
             << static_cast<std::uint64_t>(
                    value.statistics.throughput_per_second)
             << " | "
             << (run.public_gate_applicable
                     ? (value.public_gate.passed() ? "pass" : "fail")
                     : "not applicable")
             << " | "
             << (value.matching_core_threshold.passed() ? "pass" : "miss")
             << " | "
             << (run.trace.nominal_fills == 0
                     ? "not applicable"
                     : (value.multi_fill_threshold ? "pass" : "miss"))
             << " | "
             << value.checksum << " |\n";
    }
  }
  output << "\nNearest-rank percentiles are computed independently per "
            "repetition; the table reports the median of repetition metrics. ";
  if (options.mode == "acceptance") {
    output << "Exactly four of five or five of five repetitions must pass "
              "each applicable public-path gate. ";
  } else {
    output << "This noncanonical run does not establish final acceptance. ";
  }
  output << "Multi-fill matching-core ceilings are informational conservative "
            "evidence only.\n\n"
         << "## Allocation classification\n\n"
         << "Global allocation overrides were enabled only in the separate "
            "audit executable. Strict validity requires both timed public "
            "`process()` and timed sample/checksum collection to report zero "
            "allocations, allocated bytes, and deallocations in every "
            "repetition. Order FIFOs, active IDs, and price levels are all "
            "startup-backed bounded storage.\n\n"
         << "| Workload | Seed | Repetition | Timed process allocs/bytes/frees | "
            "Timed collection allocs/bytes/frees |\n"
         << "| --- | ---: | ---: | ---: | ---: |\n";
  for (const auto& run : runs) {
    for (std::size_t repetition = 0; repetition < run.repetitions.size();
         ++repetition) {
      const auto& process =
          run.repetitions[repetition].allocations.timed_process;
      const auto& collection =
          run.repetitions[repetition].allocations.timed_collection;
      output << "| " << run.workload << " | " << run.seed << " | "
             << repetition << " | " << process.allocations << '/'
             << process.allocated_bytes << '/' << process.deallocations
             << " | " << collection.allocations << '/'
             << collection.allocated_bytes << '/' << collection.deallocations
             << " |\n";
    }
  }
  output << "\nTrace construction, sample-buffer setup, initial population, "
            "warm-up, timed collection, and post-run statistics are separate "
            "allocation phases in the machine-readable artifact. Trace "
            "generation and all serialization occur outside timing.\n\n"
         << "## Reproduction\n\n"
         << "```bash\ncmake --preset release\n"
         << "cmake --build --preset release --target benchmarks\n"
         << "./build/release/benchmarks/lob_phase2_pool_allocation_audit --mode "
         << options.mode << " --workload " << options.workload
         << " --seeds ";
  for (std::size_t index = 0; index < options.seeds.size(); ++index) {
    if (index != 0) {
      output << ',';
    }
    output << options.seeds[index];
  }
  output << " --repetitions " << options.repetitions
         << " --warmup " << options.warmup;
  if (options.samples_override != 0) {
    output << " --samples " << options.samples_override;
  }
  if (options.cpu >= 0) {
    output << " --cpu " << options.cpu;
  }
  output << " --sibling-occupancy " << options.sibling_occupancy
         << " --allocation-output <path>\n"
         << "./build/release/benchmarks/lob_phase2_pool_benchmark --mode "
         << options.mode << " --workload " << options.workload
         << " --seeds ";
  for (std::size_t index = 0; index < options.seeds.size(); ++index) {
    if (index != 0) {
      output << ',';
    }
    output << options.seeds[index];
  }
  output << " --repetitions " << options.repetitions
         << " --warmup " << options.warmup;
  if (options.samples_override != 0) {
    output << " --samples " << options.samples_override;
  }
  if (options.cpu >= 0) {
    output << " --cpu " << options.cpu;
  }
  output << " --sibling-occupancy " << options.sibling_occupancy
         << " --allocation-input <path> --json <local-path> "
            "--report <local-path>\n"
         << "python3 benchmarks/compare_phase2.py --baseline <phase1-v2.json> "
            "--candidate <local-path> --output-json <final-path> "
            "--report <comparison-path>\n```\n";
  return static_cast<bool>(output);
}
#endif

}  // namespace

int main(int argc, char** argv) {
  const auto parsed = parse_options(argc, argv);
  if (!parsed) {
    std::cerr << "usage: phase2 pool benchmark "
                 "--mode smoke|exploratory|acceptance "
                 "[--workload all|NAME] [--seeds N[,N]] [--samples N] "
                 "[--warmup N] [--repetitions N] [--cpu N] [--json PATH] "
                 "[--sibling-occupancy LABEL] "
                 "[--report PATH] [--allocation-input PATH] "
                 "[--allocation-output PATH] [--provenance PATH]\n";
    return 2;
  }
  const auto options = *parsed;
  const auto workloads = selected_workloads(options.workload);
  if (workloads.empty() || !pin_cpu(options.cpu)) {
    std::cerr << "invalid workload or CPU affinity request\n";
    return 2;
  }
  const auto provenance_path =
      options.provenance_path.empty()
          ? lob::benchmark::default_provenance_path()
          : options.provenance_path;
#ifdef LOB_ENABLE_ALLOCATION_AUDIT
  const auto provenance = lob::benchmark::collect_execution_provenance(
      provenance_path, lob::benchmark::BenchmarkBinaryRole::AllocationAudit);
#else
  const auto provenance = lob::benchmark::collect_execution_provenance(
      provenance_path, lob::benchmark::BenchmarkBinaryRole::Latency);
#endif
  if (options.mode == "acceptance" && !provenance.canonical_eligible) {
    std::cerr << "acceptance requires a clean committed Release build with "
                 "verified source and executable provenance\n";
    return 2;
  }

  std::vector<AllocationKey> allocation_records;
#ifndef LOB_ENABLE_ALLOCATION_AUDIT
  if (!options.allocation_input.empty()) {
    allocation_records = read_allocation_sidecar(options.allocation_input,
                                                  options, provenance);
    const auto expected_records = workloads.size() * options.seeds.size() *
                                  (options.repetitions + 1);
    if (allocation_records.size() != expected_records) {
      std::cerr << "allocation sidecar does not match run configuration\n";
      return 2;
    }
  }
#endif

#ifndef LOB_ENABLE_ALLOCATION_AUDIT
  const auto environment =
      collect_environment(options.cpu, options.sibling_occupancy);
#endif
  std::vector<WorkloadRun> runs;
  runs.reserve(workloads.size() * options.seeds.size());
  bool accepted = true;
  for (const auto seed : options.seeds) {
    for (const auto& workload : workloads) {
      WorkloadRun run;
      run.workload = workload;
      run.seed = seed;
      run.samples = default_samples(workload, options);
      run.warmup = options.warmup;
      run.trace = make_trace(workload, run.samples, run.warmup, seed);
#ifndef LOB_ENABLE_ALLOCATION_AUDIT
      if (!allocation_records.empty()) {
        const auto* trace_allocation = find_allocation(
            allocation_records, workload, seed,
            std::numeric_limits<std::size_t>::max(), run.samples,
            run.warmup);
        if (trace_allocation == nullptr) {
          std::cerr << "missing trace allocation record\n";
          return 2;
        }
        if (trace_allocation->trace_checksum != run.trace.trace_checksum) {
          std::cerr << "allocation trace checksum mismatch\n";
          return 2;
        }
        run.trace.trace_allocations =
            trace_allocation->phases.trace_generation;
      }
#endif
      run.repetitions.reserve(options.repetitions);
      for (std::size_t repetition = 0; repetition < options.repetitions;
           ++repetition) {
#ifdef LOB_ENABLE_ALLOCATION_AUDIT
        auto result = run_repetition(run.trace, run.warmup, false);
#else
        auto result = run_repetition(run.trace, run.warmup, true);
        if (!allocation_records.empty()) {
          const auto* allocation = find_allocation(
              allocation_records, workload, seed, repetition, run.samples,
              run.warmup);
          if (allocation == nullptr) {
            std::cerr << "missing allocation record\n";
            return 2;
          }
          result.allocations = allocation->phases;
        }
#endif
        run.repetitions.push_back(result);
      }
      run.median = median_statistics(run.repetitions);
      run.public_gate_applicable = run.trace.nominal_fills <= 1;
      for (const auto& repetition : run.repetitions) {
        if (repetition.public_gate.passed()) {
          ++run.public_gate_passes;
        }
      }
      const auto required_passes =
          options.mode == "acceptance" ? std::size_t{4}
                                       : options.repetitions;
      run.performance_gate_compliant =
          !run.public_gate_applicable ||
          run.public_gate_passes >= required_passes;
      const bool mix_valid =
          workload != "mixed" ||
          (lob::benchmark::valid_mixed_operation_counts(
               run.trace.mixed_counts) &&
           lob::benchmark::top_five_volume_valid(run.trace.top_five_volume,
                                                  run.trace.priced_volume) &&
           run.trace.expected_min_active >= 5'000 &&
           run.trace.expected_max_active <= 10'000);
      run.workload_valid = mix_valid;
      run.allocation_policy_compliant = true;
      for (const auto& repetition : run.repetitions) {
        run.workload_valid =
            run.workload_valid && repetition.unintended_outbox_failures == 0 &&
            repetition.lifecycle_transitions == 0 &&
            repetition.starting_cpu == repetition.ending_cpu &&
            distributions_valid(run.trace, repetition, run.samples);
        if (workload == "mixed") {
          run.workload_valid =
              run.workload_valid && repetition.minimum_active >= 5'000 &&
              repetition.maximum_active <= 10'000;
        }
        run.allocation_policy_compliant =
            run.allocation_policy_compliant &&
            repetition.allocations.timed_process.allocations == 0 &&
            repetition.allocations.timed_process.allocated_bytes == 0 &&
            repetition.allocations.timed_process.deallocations == 0 &&
            repetition.allocations.timed_collection.allocations == 0 &&
            repetition.allocations.timed_collection.allocated_bytes == 0 &&
            repetition.allocations.timed_collection.deallocations == 0;
      }
      accepted = accepted && run.workload_valid &&
                 run.allocation_policy_compliant;
#ifndef LOB_ENABLE_ALLOCATION_AUDIT
      if (options.mode == "acceptance") {
        accepted = accepted && run.performance_gate_compliant;
      }
#endif
      runs.push_back(std::move(run));
    }
  }

#ifdef LOB_ENABLE_ALLOCATION_AUDIT
  if (!write_allocation_sidecar(options.allocation_output, options, provenance,
                                runs)) {
    std::cerr << "failed to write allocation sidecar\n";
    return 1;
  }
#else
  if (!write_json(options.json_path, options, environment, provenance, runs,
                  accepted) ||
      !write_report(options.report_path, options, environment, provenance,
                    runs, accepted)) {
    std::cerr << "failed to write result artifacts\n";
    return 1;
  }
#endif
  return accepted ? 0 : 1;
}
