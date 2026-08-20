#include "benchmark_support.hpp"
#include "lob/memory/fixed_object_pool.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
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
#include <sched.h>
#include <sstream>
#include <string>
#include <string_view>
#include <sys/utsname.h>
#include <utility>
#include <vector>

#ifndef LOB_BENCHMARK_GIT_COMMIT
#define LOB_BENCHMARK_GIT_COMMIT "unknown"
#endif

#ifndef LOB_BENCHMARK_GIT_DIRTY
#define LOB_BENCHMARK_GIT_DIRTY true
#endif

#ifndef LOB_BENCHMARK_BUILD_TYPE
#define LOB_BENCHMARK_BUILD_TYPE "unknown"
#endif

namespace allocation_audit {

enum class Phase : std::uint8_t {
  Inactive,
  Construction,
  Preparation,
  Warmup,
  TimedPool,
  TimedCollection,
  PostStatistics,
  Destruction,
  Count,
};

struct Counters final {
  std::uint64_t allocations{};
  std::uint64_t allocated_bytes{};
  std::uint64_t deallocations{};

  constexpr Counters& operator+=(const Counters& other) noexcept {
    allocations += other.allocations;
    allocated_bytes += other.allocated_bytes;
    deallocations += other.deallocations;
    return *this;
  }
};

#ifdef LOB_ENABLE_POOL_ALLOCATION_AUDIT
thread_local Phase current_phase = Phase::Inactive;
std::array<Counters, static_cast<std::size_t>(Phase::Count)> counters{};

void record_allocation(std::size_t bytes) noexcept {
  auto& current = counters[static_cast<std::size_t>(current_phase)];
  ++current.allocations;
  current.allocated_bytes += bytes;
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
#ifdef LOB_ENABLE_POOL_ALLOCATION_AUDIT
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
#ifdef LOB_ENABLE_POOL_ALLOCATION_AUDIT
    current_phase = previous_;
#endif
  }

 private:
#ifdef LOB_ENABLE_POOL_ALLOCATION_AUDIT
  Phase previous_{};
#endif
};

[[nodiscard]] Counters snapshot(Phase phase) noexcept {
  return counters[static_cast<std::size_t>(phase)];
}

}  // namespace allocation_audit

#ifdef LOB_ENABLE_POOL_ALLOCATION_AUDIT
void* operator new(std::size_t bytes) {
  const auto allocation_size = bytes == 0 ? std::size_t{1} : bytes;
  if (void* memory = std::malloc(allocation_size)) {
    allocation_audit::record_allocation(allocation_size);
    return memory;
  }
  throw std::bad_alloc{};
}

void* operator new[](std::size_t bytes) { return ::operator new(bytes); }

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
  ::operator delete(memory);
}

void operator delete[](void* memory, const std::nothrow_t&) noexcept {
  ::operator delete[](memory);
}

void* operator new(std::size_t bytes, std::align_val_t alignment) {
  void* memory = nullptr;
  const auto allocation_size = bytes == 0 ? std::size_t{1} : bytes;
  if (posix_memalign(&memory, static_cast<std::size_t>(alignment),
                     allocation_size) != 0) {
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

void operator delete(void* memory, std::align_val_t alignment,
                     const std::nothrow_t&) noexcept {
  ::operator delete(memory, alignment);
}

void operator delete[](void* memory, std::align_val_t alignment,
                       const std::nothrow_t&) noexcept {
  ::operator delete[](memory, alignment);
}
#endif

namespace {

using Clock = std::chrono::steady_clock;

struct BenchmarkNode final {
  std::uint64_t order_id{};
  std::uint64_t price{};
  std::uint64_t quantity{};
  std::uint64_t queue_links{};

  BenchmarkNode(std::uint64_t id, std::uint64_t sequence) noexcept
      : order_id(id),
        price(10'000 + sequence % 64),
        quantity(1 + sequence % 1'000),
        queue_links(sequence ^ id) {}
};

using Pool = lob::FixedObjectPool<BenchmarkNode>;

struct Options final {
  std::string mode{};
  std::size_t capacity{lob::kMaximumActiveOrders};
  std::size_t repetitions{};
  std::size_t full_cycles{};
  std::size_t churn_operations{};
  std::size_t warmup_cycles{};
  int cpu{-1};
  std::string sibling_occupancy{"not_observed"};
  std::string allocation_input{};
  std::string allocation_output{};
  std::string json_path{};
  std::string report_path{};
};

struct SampleBuffers final {
  std::vector<std::uint64_t> full_acquire{};
  std::vector<std::uint64_t> full_release{};
  std::vector<std::uint64_t> full_cycle{};
  std::vector<std::uint64_t> churn_acquire{};
  std::vector<std::uint64_t> churn_release{};
  std::vector<std::uint64_t> churn_combined{};
};

struct Metrics final {
  lob::benchmark::LatencyStatistics full_acquire{};
  lob::benchmark::LatencyStatistics full_release{};
  lob::benchmark::LatencyStatistics full_cycle{};
  lob::benchmark::LatencyStatistics churn_acquire{};
  lob::benchmark::LatencyStatistics churn_release{};
  lob::benchmark::LatencyStatistics churn_combined{};
  double full_cycle_operations_per_second{};
  double churn_operations_per_second{};
};

struct RepetitionResult final {
  Metrics metrics{};
  std::uint64_t checksum{};
  std::size_t high_water{};
  int starting_cpu{-1};
  int ending_cpu{-1};
  bool valid{};
};

struct AllocationEvidence final {
  allocation_audit::Counters construction{};
  allocation_audit::Counters preparation{};
  allocation_audit::Counters warmup{};
  allocation_audit::Counters timed_pool{};
  allocation_audit::Counters timed_collection{};
  allocation_audit::Counters post_statistics{};
  allocation_audit::Counters destruction{};
  bool attached{};

  [[nodiscard]] bool timed_zero() const noexcept {
    return timed_pool.allocations == 0 && timed_pool.allocated_bytes == 0 &&
           timed_pool.deallocations == 0 &&
           timed_collection.allocations == 0 &&
           timed_collection.allocated_bytes == 0 &&
           timed_collection.deallocations == 0;
  }
};

struct Environment final {
  std::string cpu_model{};
  std::string microcode{};
  std::string kernel{};
  std::string governor{};
  std::string frequency_khz{};
  std::string numa_node{};
  std::string thread_siblings{};
  std::uint64_t clock_overhead_ns{};
};

template <typename Integer>
[[nodiscard]] bool parse_integer(std::string_view text, Integer& value) {
  const auto* first = text.data();
  const auto* last = first + text.size();
  const auto parsed = std::from_chars(first, last, value);
  return parsed.ec == std::errc{} && parsed.ptr == last;
}

[[nodiscard]] std::optional<Options> parse_options(int argc, char** argv) {
  Options options;
  for (int index = 1; index < argc; ++index) {
    const std::string_view argument{argv[index]};
    if (index + 1 >= argc) {
      return std::nullopt;
    }
    const std::string_view value{argv[++index]};
    if (argument == "--mode") {
      options.mode = value;
    } else if (argument == "--capacity") {
      if (!parse_integer(value, options.capacity)) {
        return std::nullopt;
      }
    } else if (argument == "--repetitions") {
      if (!parse_integer(value, options.repetitions)) {
        return std::nullopt;
      }
    } else if (argument == "--full-cycles") {
      if (!parse_integer(value, options.full_cycles)) {
        return std::nullopt;
      }
    } else if (argument == "--churn-operations") {
      if (!parse_integer(value, options.churn_operations)) {
        return std::nullopt;
      }
    } else if (argument == "--warmup-cycles") {
      if (!parse_integer(value, options.warmup_cycles)) {
        return std::nullopt;
      }
    } else if (argument == "--cpu") {
      if (!parse_integer(value, options.cpu)) {
        return std::nullopt;
      }
    } else if (argument == "--sibling-occupancy") {
      options.sibling_occupancy = value;
    } else if (argument == "--allocation-input") {
      options.allocation_input = value;
    } else if (argument == "--allocation-output") {
      options.allocation_output = value;
    } else if (argument == "--json") {
      options.json_path = value;
    } else if (argument == "--report") {
      options.report_path = value;
    } else {
      return std::nullopt;
    }
  }

  if (options.mode != "smoke" && options.mode != "acceptance") {
    return std::nullopt;
  }
  if (options.repetitions == 0) {
    options.repetitions = options.mode == "acceptance" ? 5 : 1;
  }
  if (options.full_cycles == 0) {
    options.full_cycles = options.mode == "acceptance" ? 8 : 2;
  }
  if (options.churn_operations == 0) {
    options.churn_operations =
        options.mode == "acceptance" ? 1'000'000 : 1'000;
  }
  if (options.warmup_cycles == 0) {
    options.warmup_cycles = options.mode == "acceptance" ? 2 : 1;
  }
  if (options.capacity == 0 || options.capacity >= Pool::kInvalidIndex ||
      options.churn_operations == 0) {
    return std::nullopt;
  }
  if (options.mode == "acceptance" &&
      (options.capacity != lob::kMaximumActiveOrders ||
       options.repetitions < 5 || options.cpu < 0)) {
    return std::nullopt;
  }
#ifdef LOB_ENABLE_POOL_ALLOCATION_AUDIT
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

[[nodiscard]] bool pin_cpu(int cpu) noexcept {
  if (cpu < 0) {
    return true;
  }
  cpu_set_t mask;
  CPU_ZERO(&mask);
  CPU_SET(cpu, &mask);
  return sched_setaffinity(0, sizeof(mask), &mask) == 0;
}

[[maybe_unused, nodiscard]] std::string trim(std::string value) {
  const auto first = value.find_first_not_of(" \t");
  if (first == std::string::npos) {
    return {};
  }
  const auto last = value.find_last_not_of(" \t");
  return value.substr(first, last - first + 1);
}

[[maybe_unused, nodiscard]] std::string first_cpu_value(std::string_view key) {
  std::ifstream input("/proc/cpuinfo");
  std::string line;
  while (std::getline(input, line)) {
    if (line.starts_with(key)) {
      const auto colon = line.find(':');
      return colon == std::string::npos ? trim(line)
                                        : trim(line.substr(colon + 1));
    }
  }
  return "unavailable";
}

[[maybe_unused, nodiscard]] std::string read_line(const std::string& path) {
  std::ifstream input(path);
  std::string line;
  return std::getline(input, line) ? trim(line) : "unavailable";
}

[[maybe_unused, nodiscard]] std::string find_numa_node(
    const std::string& cpu_path) {
  std::error_code error;
  for (const auto& entry :
       std::filesystem::directory_iterator(cpu_path, error)) {
    const auto name = entry.path().filename().string();
    if (name.starts_with("node")) {
      return name.substr(4);
    }
  }
  return "unavailable";
}

[[maybe_unused, nodiscard]] std::uint64_t measure_clock_overhead() {
  std::uint64_t best = std::numeric_limits<std::uint64_t>::max();
  for (std::size_t index = 0; index < 10'000; ++index) {
    const auto start = Clock::now();
    const auto finish = Clock::now();
    const auto elapsed = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(finish - start)
            .count());
    best = std::min(best, elapsed);
  }
  return best;
}

[[maybe_unused, nodiscard]] Environment collect_environment(int cpu) {
  Environment environment;
  environment.cpu_model = first_cpu_value("model name");
  environment.microcode = first_cpu_value("microcode");
  utsname system{};
  if (uname(&system) == 0) {
    environment.kernel = std::string{system.sysname} + ' ' + system.release +
                         ' ' + system.machine;
  } else {
    environment.kernel = "unavailable";
  }
  const auto cpu_text = std::to_string(cpu < 0 ? sched_getcpu() : cpu);
  const auto cpu_path =
      std::string{"/sys/devices/system/cpu/cpu"} + cpu_text + "/";
  environment.governor =
      read_line(cpu_path + "cpufreq/scaling_governor");
  environment.frequency_khz =
      read_line(cpu_path + "cpufreq/scaling_cur_freq");
  environment.numa_node = find_numa_node(cpu_path);
  environment.thread_siblings =
      read_line(cpu_path + "topology/thread_siblings_list");
  environment.clock_overhead_ns = measure_clock_overhead();
  return environment;
}

[[nodiscard]] std::uint64_t elapsed_ns(Clock::time_point start,
                                       Clock::time_point finish) noexcept {
  return static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::nanoseconds>(finish - start)
          .count());
}

void warm_pool(Pool& pool, std::vector<Pool::Handle>& handles,
               std::size_t cycles) {
  allocation_audit::Guard phase(allocation_audit::Phase::Warmup);
  for (std::size_t cycle = 0; cycle < cycles; ++cycle) {
    for (std::size_t index = 0; index < pool.capacity(); ++index) {
      handles[index] = pool.acquire(index, cycle).handle;
    }
    for (const auto handle : handles) {
      static_cast<void>(pool.release(handle));
    }
  }
}

[[nodiscard]] RepetitionResult run_repetition(const Options& options,
                                              bool collect_latency) {
  RepetitionResult result;
  result.starting_cpu = sched_getcpu();
  std::unique_ptr<Pool> pool;
  {
    allocation_audit::Guard phase(allocation_audit::Phase::Construction);
    pool = std::make_unique<Pool>(options.capacity);
  }

  std::vector<Pool::Handle> handles;
  std::vector<Pool::Handle> live_handles;
  std::vector<std::size_t> churn_positions;
  SampleBuffers samples;
  {
    allocation_audit::Guard phase(allocation_audit::Phase::Preparation);
    handles.resize(options.capacity);
    const auto live_count = std::max<std::size_t>(1, options.capacity / 2);
    live_handles.resize(live_count);
    churn_positions.resize(options.churn_operations);
    for (std::size_t index = 0; index < churn_positions.size(); ++index) {
      churn_positions[index] = (index * 65'537 + 17) % live_count;
    }
    samples.full_acquire.resize(options.capacity * options.full_cycles);
    samples.full_release.resize(options.capacity * options.full_cycles);
    samples.full_cycle.resize(options.full_cycles);
    samples.churn_acquire.resize(options.churn_operations);
    samples.churn_release.resize(options.churn_operations);
    samples.churn_combined.resize(options.churn_operations);
  }

  warm_pool(*pool, handles, options.warmup_cycles);
  std::size_t full_sample = 0;
  std::uint64_t checksum = 14'695'981'039'346'656'037ULL;
  for (std::size_t cycle = 0; cycle < options.full_cycles; ++cycle) {
    std::uint64_t cycle_duration = 0;
    for (std::size_t index = 0; index < options.capacity; ++index) {
      Pool::AcquireResult acquired;
      std::uint64_t duration = 0;
      {
        allocation_audit::Guard phase(allocation_audit::Phase::TimedPool);
        const auto start = Clock::now();
        acquired = pool->acquire(index, cycle);
        const auto finish = Clock::now();
        if (collect_latency) {
          duration = elapsed_ns(start, finish);
        }
      }
      {
        allocation_audit::Guard phase(
            allocation_audit::Phase::TimedCollection);
        if (!acquired.acquired()) {
          return result;
        }
        handles[index] = acquired.handle;
        samples.full_acquire[full_sample] = duration;
        const auto* node = pool->get(acquired.handle);
        if (node == nullptr) {
          return result;
        }
        checksum = lob::benchmark::checksum_mix(checksum, node->order_id);
        checksum = lob::benchmark::checksum_mix(
            checksum, acquired.handle.generation());
      }
      cycle_duration += duration;
      ++full_sample;
    }

    const auto release_base = cycle * options.capacity;
    for (std::size_t index = 0; index < options.capacity; ++index) {
      lob::PoolReleaseStatus released{};
      std::uint64_t duration = 0;
      {
        allocation_audit::Guard phase(allocation_audit::Phase::TimedPool);
        const auto start = Clock::now();
        released = pool->release(handles[index]);
        const auto finish = Clock::now();
        if (collect_latency) {
          duration = elapsed_ns(start, finish);
        }
      }
      {
        allocation_audit::Guard phase(
            allocation_audit::Phase::TimedCollection);
        if (released != lob::PoolReleaseStatus::Released) {
          return result;
        }
        samples.full_release[release_base + index] = duration;
        checksum = lob::benchmark::checksum_mix(checksum, index);
      }
      cycle_duration += duration;
    }
    samples.full_cycle[cycle] = cycle_duration;
  }

  for (std::size_t index = 0; index < live_handles.size(); ++index) {
    live_handles[index] = pool->acquire(index + 1'000'000, index).handle;
  }
  for (std::size_t operation = 0; operation < options.churn_operations;
       ++operation) {
    const auto position = churn_positions[operation];
    lob::PoolReleaseStatus released{};
    Pool::AcquireResult acquired;
    std::uint64_t release_duration = 0;
    std::uint64_t acquire_duration = 0;
    std::uint64_t combined_duration = 0;
    {
      allocation_audit::Guard phase(allocation_audit::Phase::TimedPool);
      const auto combined_start = Clock::now();
      const auto release_start = Clock::now();
      released = pool->release(live_handles[position]);
      const auto release_finish = Clock::now();
      acquired = pool->acquire(operation + 2'000'000, operation);
      const auto acquire_finish = Clock::now();
      if (collect_latency) {
        release_duration = elapsed_ns(release_start, release_finish);
        acquire_duration = elapsed_ns(release_finish, acquire_finish);
        combined_duration = elapsed_ns(combined_start, acquire_finish);
      }
    }
    {
      allocation_audit::Guard phase(
          allocation_audit::Phase::TimedCollection);
      if (released != lob::PoolReleaseStatus::Released ||
          !acquired.acquired()) {
        return result;
      }
      live_handles[position] = acquired.handle;
      samples.churn_release[operation] = release_duration;
      samples.churn_acquire[operation] = acquire_duration;
      samples.churn_combined[operation] = combined_duration;
      checksum = lob::benchmark::checksum_mix(
          checksum, acquired.handle.index());
      checksum = lob::benchmark::checksum_mix(
          checksum, acquired.handle.generation());
    }
  }

  result.high_water = pool->high_water_count();
  result.checksum = checksum;
  result.valid = pool->used_count() == live_handles.size() &&
                 pool->free_count() + pool->used_count() == pool->capacity() &&
                 pool->validate_invariants();
  {
    allocation_audit::Guard phase(allocation_audit::Phase::PostStatistics);
    result.metrics.full_acquire =
        lob::benchmark::summarize_latencies(samples.full_acquire);
    result.metrics.full_release =
        lob::benchmark::summarize_latencies(samples.full_release);
    result.metrics.full_cycle =
        lob::benchmark::summarize_latencies(samples.full_cycle);
    result.metrics.churn_acquire =
        lob::benchmark::summarize_latencies(samples.churn_acquire);
    result.metrics.churn_release =
        lob::benchmark::summarize_latencies(samples.churn_release);
    result.metrics.churn_combined =
        lob::benchmark::summarize_latencies(samples.churn_combined);
    if (result.metrics.full_cycle.total_timed_ns != 0) {
      result.metrics.full_cycle_operations_per_second =
          static_cast<double>(options.full_cycles * options.capacity * 2) *
          1'000'000'000.0 /
          static_cast<double>(result.metrics.full_cycle.total_timed_ns);
    }
    if (result.metrics.churn_combined.total_timed_ns != 0) {
      result.metrics.churn_operations_per_second =
          static_cast<double>(options.churn_operations * 2) *
          1'000'000'000.0 /
          static_cast<double>(result.metrics.churn_combined.total_timed_ns);
    }
  }
  result.ending_cpu = sched_getcpu();
  {
    allocation_audit::Guard phase(allocation_audit::Phase::Destruction);
    pool.reset();
  }
  return result;
}

[[maybe_unused, nodiscard]] AllocationEvidence current_allocation_evidence()
    noexcept {
  return {allocation_audit::snapshot(allocation_audit::Phase::Construction),
          allocation_audit::snapshot(allocation_audit::Phase::Preparation),
          allocation_audit::snapshot(allocation_audit::Phase::Warmup),
          allocation_audit::snapshot(allocation_audit::Phase::TimedPool),
          allocation_audit::snapshot(
              allocation_audit::Phase::TimedCollection),
          allocation_audit::snapshot(
              allocation_audit::Phase::PostStatistics),
          allocation_audit::snapshot(allocation_audit::Phase::Destruction),
#ifdef LOB_ENABLE_POOL_ALLOCATION_AUDIT
          true
#else
          false
#endif
  };
}

[[maybe_unused]] void write_counter(
    std::ostream& output, std::string_view name,
    allocation_audit::Counters counters) {
  output << name << ' ' << counters.allocations << ' '
         << counters.allocated_bytes << ' ' << counters.deallocations << '\n';
}

[[maybe_unused, nodiscard]] bool write_allocation_sidecar(
    const Options& options, const AllocationEvidence& evidence) {
  std::ofstream output(options.allocation_output);
  if (!output) {
    return false;
  }
  output << "LOB_POOL_ALLOCATION_V1\n"
         << "capacity " << options.capacity << '\n'
         << "repetitions " << options.repetitions << '\n'
         << "full_cycles " << options.full_cycles << '\n'
         << "churn_operations " << options.churn_operations << '\n'
         << "warmup_cycles " << options.warmup_cycles << '\n';
  write_counter(output, "construction", evidence.construction);
  write_counter(output, "preparation", evidence.preparation);
  write_counter(output, "warmup", evidence.warmup);
  write_counter(output, "timed_pool", evidence.timed_pool);
  write_counter(output, "timed_collection", evidence.timed_collection);
  write_counter(output, "post_statistics", evidence.post_statistics);
  write_counter(output, "destruction", evidence.destruction);
  return output.good();
}

[[maybe_unused, nodiscard]] bool read_named_size(
    std::istream& input, std::string_view expected, std::size_t& value) {
  std::string name;
  return static_cast<bool>(input >> name >> value) && name == expected;
}

[[maybe_unused, nodiscard]] bool read_counter(
    std::istream& input, std::string_view expected,
    allocation_audit::Counters& counters) {
  std::string name;
  return static_cast<bool>(input >> name >> counters.allocations >>
                           counters.allocated_bytes >>
                           counters.deallocations) &&
         name == expected;
}

[[maybe_unused, nodiscard]] std::optional<AllocationEvidence>
read_allocation_sidecar(
    const Options& options) {
  std::ifstream input(options.allocation_input);
  std::string schema;
  std::size_t capacity = 0;
  std::size_t repetitions = 0;
  std::size_t full_cycles = 0;
  std::size_t churn_operations = 0;
  std::size_t warmup_cycles = 0;
  AllocationEvidence evidence;
  if (!(input >> schema) || schema != "LOB_POOL_ALLOCATION_V1" ||
      !read_named_size(input, "capacity", capacity) ||
      !read_named_size(input, "repetitions", repetitions) ||
      !read_named_size(input, "full_cycles", full_cycles) ||
      !read_named_size(input, "churn_operations", churn_operations) ||
      !read_named_size(input, "warmup_cycles", warmup_cycles) ||
      !read_counter(input, "construction", evidence.construction) ||
      !read_counter(input, "preparation", evidence.preparation) ||
      !read_counter(input, "warmup", evidence.warmup) ||
      !read_counter(input, "timed_pool", evidence.timed_pool) ||
      !read_counter(input, "timed_collection", evidence.timed_collection) ||
      !read_counter(input, "post_statistics", evidence.post_statistics) ||
      !read_counter(input, "destruction", evidence.destruction) ||
      capacity != options.capacity || repetitions != options.repetitions ||
      full_cycles != options.full_cycles ||
      churn_operations != options.churn_operations ||
      warmup_cycles != options.warmup_cycles) {
    return std::nullopt;
  }
  evidence.attached = true;
  return evidence;
}

[[maybe_unused, nodiscard]] lob::benchmark::LatencyStatistics median_statistics(
    const std::vector<RepetitionResult>& repetitions,
    const lob::benchmark::LatencyStatistics Metrics::*member) {
  std::vector<lob::benchmark::LatencyStatistics> values;
  values.reserve(repetitions.size());
  for (const auto& repetition : repetitions) {
    values.push_back(repetition.metrics.*member);
  }
  const auto median_u64 = [&values](auto field) {
    std::vector<std::uint64_t> samples;
    samples.reserve(values.size());
    for (const auto& value : values) {
      samples.push_back(value.*field);
    }
    std::sort(samples.begin(), samples.end());
    return samples[samples.size() / 2];
  };
  const auto median_double = [&values](auto field) {
    std::vector<double> samples;
    samples.reserve(values.size());
    for (const auto& value : values) {
      samples.push_back(value.*field);
    }
    std::sort(samples.begin(), samples.end());
    return samples[samples.size() / 2];
  };
  lob::benchmark::LatencyStatistics result;
  result.sample_count = median_u64(
      &lob::benchmark::LatencyStatistics::sample_count);
  result.p50_ns = median_u64(&lob::benchmark::LatencyStatistics::p50_ns);
  result.p90_ns = median_u64(&lob::benchmark::LatencyStatistics::p90_ns);
  result.p99_ns = median_u64(&lob::benchmark::LatencyStatistics::p99_ns);
  result.p999_ns = median_u64(&lob::benchmark::LatencyStatistics::p999_ns);
  result.p9999_ns = median_u64(&lob::benchmark::LatencyStatistics::p9999_ns);
  result.maximum_ns = median_u64(&lob::benchmark::LatencyStatistics::maximum_ns);
  result.total_timed_ns = median_u64(
      &lob::benchmark::LatencyStatistics::total_timed_ns);
  result.throughput_per_second = median_double(
      &lob::benchmark::LatencyStatistics::throughput_per_second);
  return result;
}

[[maybe_unused, nodiscard]] double median_metric(
    const std::vector<RepetitionResult>& repetitions,
    double Metrics::*member) {
  std::vector<double> values;
  values.reserve(repetitions.size());
  for (const auto& repetition : repetitions) {
    values.push_back(repetition.metrics.*member);
  }
  std::sort(values.begin(), values.end());
  return values[values.size() / 2];
}

[[maybe_unused]] void write_statistics_json(
    std::ostream& output, std::string_view name,
    const lob::benchmark::LatencyStatistics& value,
    bool trailing_comma = true) {
  output << "    \"" << name << "\": "
         << lob::benchmark::statistics_json(value)
         << (trailing_comma ? ",\n" : "\n");
}

[[maybe_unused, nodiscard]] std::string make_json(
    const Options& options, const Environment& environment,
    const AllocationEvidence& allocations,
    const std::vector<RepetitionResult>& repetitions, bool accepted) {
  const auto full_acquire = median_statistics(
      repetitions, &Metrics::full_acquire);
  const auto full_release = median_statistics(
      repetitions, &Metrics::full_release);
  const auto full_cycle = median_statistics(repetitions, &Metrics::full_cycle);
  const auto churn_acquire = median_statistics(
      repetitions, &Metrics::churn_acquire);
  const auto churn_release = median_statistics(
      repetitions, &Metrics::churn_release);
  const auto churn_combined = median_statistics(
      repetitions, &Metrics::churn_combined);
  const auto full_cycle_operations_per_second = median_metric(
      repetitions, &Metrics::full_cycle_operations_per_second);
  const auto churn_operations_per_second = median_metric(
      repetitions, &Metrics::churn_operations_per_second);
  std::ostringstream output;
  output << std::fixed << std::setprecision(3)
         << "{\n  \"schema\": \"lob.fixed_object_pool.performance.v1\",\n"
         << "  \"accepted\": " << (accepted ? "true" : "false") << ",\n"
         << "  \"component_boundary\": \"standalone_pool_only\",\n"
         << "  \"matching_engine_comparison_applicable\": false,\n"
         << "  \"git_commit\": \"" << LOB_BENCHMARK_GIT_COMMIT << "\",\n"
         << "  \"git_dirty\": " << (LOB_BENCHMARK_GIT_DIRTY ? "true" : "false")
         << ",\n  \"build_type\": \"" << LOB_BENCHMARK_BUILD_TYPE
         << "\",\n"
         << "  \"compile_flags\": \"-O3 -march=native -ffast-math -Wall "
            "-Wextra -Werror -std=c++20 -DNDEBUG\",\n"
         << "  \"compiler\": \"" << __VERSION__ << "\",\n"
         << "  \"cpu_model\": \"" << environment.cpu_model << "\",\n"
         << "  \"microcode\": \"" << environment.microcode << "\",\n"
         << "  \"kernel\": \"" << environment.kernel << "\",\n"
         << "  \"cpu\": " << options.cpu << ",\n"
         << "  \"affinity_mask\": \"" << options.cpu << "\",\n"
         << "  \"smt_sibling_occupancy\": \""
         << options.sibling_occupancy << "\",\n"
         << "  \"thread_siblings\": \"" << environment.thread_siblings
         << "\",\n"
         << "  \"governor\": \"" << environment.governor << "\",\n"
         << "  \"frequency_khz\": \"" << environment.frequency_khz
         << "\",\n  \"numa_node\": \"" << environment.numa_node << "\",\n"
         << "  \"clock\": \"std::chrono::steady_clock\",\n"
         << "  \"clock_call_pair_overhead_ns\": "
         << environment.clock_overhead_ns << ",\n"
         << "  \"capacity\": " << options.capacity << ",\n"
         << "  \"object_size\": " << sizeof(BenchmarkNode) << ",\n"
         << "  \"object_alignment\": " << alignof(BenchmarkNode) << ",\n"
         << "  \"slot_size\": " << Pool::slot_size_bytes() << ",\n"
         << "  \"slot_alignment\": " << Pool::slot_alignment_bytes() << ",\n"
         << "  \"backing_memory_bytes\": "
         << options.capacity *
                (Pool::slot_size_bytes() + sizeof(Pool::index_type))
         << ",\n"
         << "  \"workload\": {\"full_cycles\":" << options.full_cycles
         << ",\"churn_operations\":" << options.churn_operations
         << ",\"churn_live_set\":" << std::max<std::size_t>(1, options.capacity / 2)
         << ",\"warmup_cycles\":" << options.warmup_cycles
         << ",\"repetitions\":" << options.repetitions << "},\n"
         << "  \"allocation_audit_attached\": "
         << (allocations.attached ? "true" : "false") << ",\n"
         << "  \"timed_allocation_policy_passed\": "
         << (allocations.timed_zero() ? "true" : "false") << ",\n"
         << "  \"timed_pool_allocations\": " << allocations.timed_pool.allocations
         << ",\n  \"timed_pool_allocated_bytes\": "
         << allocations.timed_pool.allocated_bytes
         << ",\n  \"timed_pool_deallocations\": "
         << allocations.timed_pool.deallocations
         << ",\n  \"timed_collection_allocations\": "
         << allocations.timed_collection.allocations
         << ",\n  \"timed_collection_allocated_bytes\": "
         << allocations.timed_collection.allocated_bytes
         << ",\n  \"timed_collection_deallocations\": "
         << allocations.timed_collection.deallocations << ",\n"
         << "  \"construction_allocations\": "
         << allocations.construction.allocations
         << ",\n  \"construction_allocated_bytes\": "
         << allocations.construction.allocated_bytes
         << ",\n  \"construction_deallocations\": "
         << allocations.construction.deallocations
         << ",\n  \"full_cycle_operations_per_second\": "
         << full_cycle_operations_per_second
         << ",\n  \"churn_operations_per_second\": "
         << churn_operations_per_second << ",\n"
         << "  \"median_statistics\": {\n";
  write_statistics_json(output, "full_acquire", full_acquire);
  write_statistics_json(output, "full_release", full_release);
  write_statistics_json(output, "full_cycle", full_cycle);
  write_statistics_json(output, "churn_acquire", churn_acquire);
  write_statistics_json(output, "churn_release", churn_release);
  write_statistics_json(output, "churn_combined", churn_combined, false);
  output << "  },\n  \"repetitions\": [\n";
  for (std::size_t index = 0; index < repetitions.size(); ++index) {
    const auto& repetition = repetitions[index];
    output << "    {\"index\":" << index << ",\"checksum\":"
           << repetition.checksum << ",\"high_water\":"
           << repetition.high_water << ",\"starting_cpu\":"
           << repetition.starting_cpu << ",\"ending_cpu\":"
           << repetition.ending_cpu << ",\"valid\":"
           << (repetition.valid ? "true" : "false") << '}';
    output << (index + 1 == repetitions.size() ? "\n" : ",\n");
  }
  output << "  ]\n}\n";
  return output.str();
}

[[maybe_unused]] void write_metric_row(
    std::ostream& output, std::string_view name,
    const lob::benchmark::LatencyStatistics& value) {
  output << "| " << name << " | " << value.sample_count << " | "
         << value.p50_ns << " | " << value.p90_ns << " | " << value.p99_ns
         << " | " << value.p999_ns << " | " << value.p9999_ns << " | "
         << value.maximum_ns << " | " << std::fixed << std::setprecision(0)
         << value.throughput_per_second << " |\n";
}

[[maybe_unused, nodiscard]] std::string make_report(
    const Options& options, const Environment& environment,
    const AllocationEvidence& allocations,
    const std::vector<RepetitionResult>& repetitions, bool accepted) {
  const auto full_acquire = median_statistics(
      repetitions, &Metrics::full_acquire);
  const auto full_release = median_statistics(
      repetitions, &Metrics::full_release);
  const auto full_cycle = median_statistics(repetitions, &Metrics::full_cycle);
  const auto churn_acquire = median_statistics(
      repetitions, &Metrics::churn_acquire);
  const auto churn_release = median_statistics(
      repetitions, &Metrics::churn_release);
  const auto churn_combined = median_statistics(
      repetitions, &Metrics::churn_combined);
  const auto full_cycle_operations_per_second = median_metric(
      repetitions, &Metrics::full_cycle_operations_per_second);
  const auto churn_operations_per_second = median_metric(
      repetitions, &Metrics::churn_operations_per_second);
  std::ostringstream output;
  output << "# Standalone Fixed-Object-Pool Baseline\n\n"
         << "Accepted: " << (accepted ? "yes" : "no") << "\n\n"
         << "Boundary: standalone pool acquire/release only; this is not a "
            "MatchingEngine::process() measurement.\n\n"
         << "## Method\n\n"
         << "The production-capacity pool and all benchmark buffers are "
            "constructed before timing. Full cycles acquire every slot and "
            "then release every slot. Churn keeps half the capacity live and "
            "uses a pre-generated deterministic position sequence. No RNG, "
            "formatting, logging, serialization, or vector growth occurs in "
            "a timed interval. Checksums consume handles and node values.\n\n"
         << "## Environment\n\n"
         << "- Git: `" << LOB_BENCHMARK_GIT_COMMIT << "` (dirty: "
         << (LOB_BENCHMARK_GIT_DIRTY ? "yes" : "no") << ")\n"
         << "- CPU: " << environment.cpu_model << "\n"
         << "- Microcode: " << environment.microcode << "\n"
         << "- Kernel: " << environment.kernel << "\n"
         << "- Compiler: " << __VERSION__ << "\n"
         << "- Flags: `-O3 -march=native -ffast-math -Wall -Wextra -Werror "
            "-std=c++20 -DNDEBUG`\n"
         << "- Affinity/core: " << options.cpu << "\n"
         << "- Thread siblings: " << environment.thread_siblings << "\n"
         << "- SMT sibling occupancy: " << options.sibling_occupancy << "\n"
         << "- Governor/frequency: " << environment.governor << " / "
         << environment.frequency_khz << " kHz\n"
         << "- NUMA node: " << environment.numa_node << "\n"
         << "- Clock call-pair overhead: " << environment.clock_overhead_ns
         << " ns (not subtracted)\n\n"
         << "## Storage and workload\n\n"
         << "- Capacity: " << options.capacity << "\n"
         << "- Object size/alignment: " << sizeof(BenchmarkNode) << " / "
         << alignof(BenchmarkNode) << " bytes\n"
         << "- Slot size/alignment: " << Pool::slot_size_bytes() << " / "
         << Pool::slot_alignment_bytes() << " bytes\n"
         << "- Backing arrays: "
         << options.capacity *
                (Pool::slot_size_bytes() + sizeof(Pool::index_type))
         << " bytes\n"
         << "- Full cycles per repetition: " << options.full_cycles << "\n"
         << "- Churn operations per repetition: "
         << options.churn_operations << "\n"
         << "- Churn live set: " << std::max<std::size_t>(1, options.capacity / 2)
         << "\n- Warm-up cycles: " << options.warmup_cycles
         << "\n- Repetitions: " << options.repetitions << "\n\n"
         << "## Median-of-repetition results\n\n"
         << "No speculative latency threshold applies. Allocation validity "
            "is the hard benchmark gate.\n\n"
         << "| Metric | Samples | p50 ns | p90 ns | p99 ns | p99.9 ns | "
            "p99.99 ns | Max ns | Throughput/s |\n"
         << "| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |\n";
  write_metric_row(output, "full acquire", full_acquire);
  write_metric_row(output, "full release", full_release);
  write_metric_row(output, "full combined cycle", full_cycle);
  write_metric_row(output, "churn acquire", churn_acquire);
  write_metric_row(output, "churn release", churn_release);
  write_metric_row(output, "churn combined pair", churn_combined);
  output << "\n- Full-cycle component operations/s: " << std::fixed
         << std::setprecision(0) << full_cycle_operations_per_second
         << "\n- Churn component operations/s: " << churn_operations_per_second
         << "\n";
  output << "\nFull-cycle and churn-combined throughput in the table is cycles/s "
            "and pairs/s respectively; acquire/release rows are operations/s. "
            "The machine-readable artifact contains the complete timing "
            "totals needed to derive component-operation throughput.\n\n"
         << "## Allocation audit\n\n"
         << "- Audit attached: " << (allocations.attached ? "yes" : "no")
         << "\n- Construction phase: "
         << allocations.construction.allocations << " allocations, "
         << allocations.construction.allocated_bytes << " bytes, "
         << allocations.construction.deallocations
         << " deallocations\n- Timed pool operations: "
         << allocations.timed_pool.allocations
         << " allocations, " << allocations.timed_pool.allocated_bytes
         << " bytes, " << allocations.timed_pool.deallocations
         << " deallocations\n- Timed sample/checksum collection: "
         << allocations.timed_collection.allocations << " allocations, "
         << allocations.timed_collection.allocated_bytes << " bytes, "
         << allocations.timed_collection.deallocations
         << " deallocations\n- Timed allocation gate: "
         << (allocations.timed_zero() ? "pass" : "fail") << "\n\n"
         << "The audit executable is separate from the canonical latency "
            "executable. Startup pool backing allocation is permitted and "
            "reported independently.\n";
  return output.str();
}

[[maybe_unused, nodiscard]] bool write_file(const std::string& path,
                                            const std::string& contents) {
  if (path.empty()) {
    return true;
  }
  std::ofstream output(path);
  output << contents;
  return output.good();
}

}  // namespace

int main(int argc, char** argv) {
  const auto parsed = parse_options(argc, argv);
  if (!parsed.has_value()) {
    std::cerr << "invalid pool benchmark configuration\n";
    return 2;
  }
  const auto options = *parsed;
  if (!pin_cpu(options.cpu)) {
    std::cerr << "unable to apply requested CPU affinity\n";
    return 2;
  }

  std::vector<RepetitionResult> repetitions;
  repetitions.reserve(options.repetitions);
  bool valid = true;
  for (std::size_t repetition = 0; repetition < options.repetitions;
       ++repetition) {
#ifdef LOB_ENABLE_POOL_ALLOCATION_AUDIT
    auto result = run_repetition(options, false);
#else
    auto result = run_repetition(options, true);
#endif
    valid = valid && result.valid && result.high_water == options.capacity &&
            (options.cpu < 0 || result.starting_cpu == result.ending_cpu);
    repetitions.push_back(std::move(result));
  }

#ifdef LOB_ENABLE_POOL_ALLOCATION_AUDIT
  const auto allocations = current_allocation_evidence();
  const bool accepted = valid && allocations.timed_zero();
  if (!write_allocation_sidecar(options, allocations)) {
    std::cerr << "unable to write allocation sidecar\n";
    return 1;
  }
  std::cout << "{\"schema\":\"lob.fixed_object_pool.allocation.v1\","
            << "\"accepted\":" << (accepted ? "true" : "false") << ','
            << "\"timed_pool_allocations\":"
            << allocations.timed_pool.allocations << ','
            << "\"timed_pool_allocated_bytes\":"
            << allocations.timed_pool.allocated_bytes << ','
            << "\"timed_pool_deallocations\":"
            << allocations.timed_pool.deallocations << ','
            << "\"timed_collection_allocations\":"
            << allocations.timed_collection.allocations << ','
            << "\"timed_collection_deallocations\":"
            << allocations.timed_collection.deallocations << "}\n";
  return accepted ? 0 : 1;
#else
  AllocationEvidence allocations;
  if (!options.allocation_input.empty()) {
    const auto read = read_allocation_sidecar(options);
    if (!read.has_value()) {
      std::cerr << "allocation sidecar does not match benchmark configuration\n";
      return 2;
    }
    allocations = *read;
  }
  const bool allocation_required = options.mode == "acceptance";
  const bool accepted = valid && allocations.timed_zero() &&
                        (!allocation_required || allocations.attached);
  const auto environment = collect_environment(options.cpu);
  const auto json =
      make_json(options, environment, allocations, repetitions, accepted);
  const auto report =
      make_report(options, environment, allocations, repetitions, accepted);
  if (!write_file(options.json_path, json) ||
      !write_file(options.report_path, report)) {
    std::cerr << "unable to write benchmark result\n";
    return 1;
  }
  std::cout << json;
  return accepted ? 0 : 1;
#endif
}
