#include "lob/matching/matching_engine.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <new>

namespace allocation_audit {

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

Counters counters{};

void allocation(std::size_t bytes) noexcept {
  ++counters.allocations;
  counters.allocated_bytes += static_cast<std::uint64_t>(bytes);
}

void deallocation() noexcept { ++counters.deallocations; }

[[nodiscard]] Counters snapshot() noexcept { return counters; }

}  // namespace allocation_audit

void* operator new(std::size_t bytes) {
  const auto size = bytes == 0 ? std::size_t{1} : bytes;
  if (void* memory = std::malloc(size)) {
    allocation_audit::allocation(size);
    return memory;
  }
  throw std::bad_alloc{};
}

void* operator new[](std::size_t bytes) { return ::operator new(bytes); }

void operator delete(void* memory) noexcept {
  if (memory != nullptr) {
    allocation_audit::deallocation();
  }
  std::free(memory);
}

void operator delete[](void* memory) noexcept { ::operator delete(memory); }
void operator delete(void* memory, std::size_t) noexcept {
  ::operator delete(memory);
}
void operator delete[](void* memory, std::size_t) noexcept {
  ::operator delete(memory);
}

void* operator new(std::size_t bytes, std::align_val_t alignment) {
  void* memory = nullptr;
  const auto size = bytes == 0 ? std::size_t{1} : bytes;
  if (posix_memalign(&memory, static_cast<std::size_t>(alignment), size) != 0) {
    throw std::bad_alloc{};
  }
  allocation_audit::allocation(size);
  return memory;
}

void* operator new[](std::size_t bytes, std::align_val_t alignment) {
  return ::operator new(bytes, alignment);
}

void operator delete(void* memory, std::align_val_t) noexcept {
  if (memory != nullptr) {
    allocation_audit::deallocation();
  }
  std::free(memory);
}

void operator delete[](void* memory, std::align_val_t alignment) noexcept {
  ::operator delete(memory, alignment);
}
void operator delete(void* memory, std::size_t,
                     std::align_val_t alignment) noexcept {
  ::operator delete(memory, alignment);
}
void operator delete[](void* memory, std::size_t,
                       std::align_val_t alignment) noexcept {
  ::operator delete(memory, alignment);
}

namespace {

template <typename Domain, typename Source>
[[nodiscard]] Domain domain(Source value) noexcept {
  const auto converted = lob::checked_domain_cast<Domain>(value);
  return converted.has_value() ? converted.value : Domain{};
}

[[nodiscard]] lob::OrderId oid(std::uint64_t value) noexcept {
  return domain<lob::OrderId>(value);
}

[[nodiscard]] lob::InstrumentId instrument() noexcept {
  return domain<lob::InstrumentId>(std::uint32_t{1});
}

[[nodiscard]] lob::PriceTicks price(std::int64_t value) noexcept {
  return domain<lob::PriceTicks>(value);
}

[[nodiscard]] lob::Quantity quantity(std::uint64_t value) noexcept {
  return domain<lob::Quantity>(value);
}

[[nodiscard]] bool zero_activity(allocation_audit::Counters value) noexcept {
  return value.allocations == 0 && value.allocated_bytes == 0 &&
         value.deallocations == 0;
}

template <typename Command>
[[nodiscard]] auto audited_process(lob::MatchingEngine& engine,
                                   const Command& command,
                                   bool& passed) noexcept {
  const auto before = allocation_audit::snapshot();
  auto result = engine.process(command);
  const auto activity = allocation_audit::snapshot() - before;
  if (!zero_activity(activity)) {
    std::fprintf(stderr,
                 "timed process heap activity: %llu/%llu/%llu\n",
                 static_cast<unsigned long long>(activity.allocations),
                 static_cast<unsigned long long>(activity.allocated_bytes),
                 static_cast<unsigned long long>(activity.deallocations));
    passed = false;
  }
  return result;
}

[[nodiscard]] lob::NewOrder add(std::uint64_t id, lob::Side side,
                                std::int64_t ticks,
                                std::uint64_t leaves) noexcept {
  return {oid(id), instrument(), side, price(ticks), quantity(leaves)};
}

void drain(lob::MatchingEngine& engine) noexcept {
  lob::ExecutionReport report;
  while (engine.try_consume_execution_report(report)) {
  }
  lob::SystemStatus status;
  while (engine.try_consume_status(status)) {
  }
}

void require(bool condition, bool& passed, const char* message) noexcept {
  if (!condition) {
    std::fprintf(stderr, "integrated allocation audit failure: %s\n", message);
    passed = false;
  }
}

void audit_mutation_families(bool& passed) {
  lob::MatchingEngine engine(instrument(), lob::StorageLimits{8, 4},
                             lob::LosslessOutboxLimits{512, 4});

  require(audited_process(engine, add(1, lob::Side::Buy, 99, 10), passed)
              .result == lob::OrderBookResult::Accepted,
          passed, "add and level creation");
  require(audited_process(engine, add(2, lob::Side::Buy, 99, 5), passed)
              .result == lob::OrderBookResult::Accepted,
          passed, "same-level add");
  require(audited_process(
              engine,
              lob::AmendOrder{oid(1), instrument(), price(99), quantity(8)},
              passed)
              .result == lob::OrderBookResult::Accepted,
          passed, "same-price reduction");
  require(audited_process(
              engine,
              lob::AmendOrder{oid(1), instrument(), price(99), quantity(12)},
              passed)
              .result == lob::OrderBookResult::Accepted,
          passed, "same-price increase and FIFO requeue");
  require(audited_process(
              engine,
              lob::AmendOrder{oid(1), instrument(), price(98), quantity(12)},
              passed)
              .result == lob::OrderBookResult::Accepted,
          passed, "price-changing amendment");
  require(audited_process(engine,
                          lob::CancelOrder{oid(1), instrument()}, passed)
              .result == lob::OrderBookResult::Accepted,
          passed, "cancel and empty-level removal");
  require(audited_process(engine, add(3, lob::Side::Sell, 100, 2), passed)
              .result == lob::OrderBookResult::Accepted,
          passed, "resting ask");
  const auto fill =
      audited_process(engine, add(4, lob::Side::Buy, 100, 2), passed);
  require(fill.result == lob::OrderBookResult::Accepted &&
              fill.reports().size() == 1,
          passed, "complete fill and release");
  drain(engine);
  require(audited_process(engine, lob::CloseInstrument{instrument()}, passed)
              .result == lob::OrderBookResult::Accepted,
          passed, "close/reset");
  drain(engine);
}

void audit_full_capacity_reuse(bool& passed) {
  lob::MatchingEngine engine(instrument(), lob::StorageLimits{2, 2},
                             lob::LosslessOutboxLimits{512, 4});
  require(engine.process(add(10, lob::Side::Sell, 100, 1)).result ==
              lob::OrderBookResult::Accepted &&
              engine.process(add(11, lob::Side::Sell, 100, 1)).result ==
                  lob::OrderBookResult::Accepted,
          passed, "populate full pool");
  const auto result =
      audited_process(engine, add(12, lob::Side::Buy, 100, 3), passed);
  require(result.result == lob::OrderBookResult::Accepted &&
              result.reports().size() == 2 && engine.active_order_count() == 1,
          passed, "full pool fills make room for remainder");
  drain(engine);
  const auto before = engine.storage_diagnostics();
  const auto rejected =
      audited_process(engine, add(13, lob::Side::Buy, 99, 1), passed);
  require(rejected.result == lob::OrderBookResult::Accepted,
          passed, "remaining free slot is reusable");
  require(engine.storage_diagnostics().pool_high_water_count ==
              before.pool_high_water_count,
          passed, "reuse retains high-water");
}

void audit_256_fill(bool& passed) {
  lob::MatchingEngine engine(instrument(), lob::StorageLimits{300, 8},
                             lob::LosslessOutboxLimits{512, 4});
  for (std::uint64_t index = 0; index < 256; ++index) {
    if (engine.process(add(1'000 + index, lob::Side::Sell, 100, 1)).result !=
        lob::OrderBookResult::Accepted) {
      require(false, passed, "populate 256-fill book");
      return;
    }
  }
  const auto result =
      audited_process(engine, add(2'000, lob::Side::Buy, 100, 256), passed);
  require(result.result == lob::OrderBookResult::Accepted &&
              result.reports().size() == 256 &&
              engine.active_order_count() == 0,
          passed, "256-fill audited command");
  drain(engine);
}

}  // namespace

int main() {
  bool passed = true;
  audit_mutation_families(passed);
  audit_full_capacity_reuse(passed);
  audit_256_fill(passed);
  return passed ? 0 : 1;
}
