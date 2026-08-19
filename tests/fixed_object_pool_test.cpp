#include "lob/memory/fixed_object_pool.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

struct Value final {
  std::uint64_t value{};

  explicit Value(std::uint64_t initial) noexcept : value(initial) {}
};

struct alignas(128) OverAligned final {
  std::uint64_t value{};

  explicit OverAligned(std::uint64_t initial) noexcept : value(initial) {}
};

struct LifecycleCounts final {
  std::size_t constructions{};
  std::size_t destructions{};
  std::size_t live{};
};

struct Tracked final {
  LifecycleCounts* counts{};
  std::uint64_t value{};

  Tracked(LifecycleCounts& lifecycle, std::uint64_t initial) noexcept
      : counts(&lifecycle), value(initial) {
    ++counts->constructions;
    ++counts->live;
  }

  ~Tracked() noexcept {
    ++counts->destructions;
    --counts->live;
  }
};

using Pool = lob::FixedObjectPool<Value>;

static_assert(lob::kMaximumActiveOrders == 131'072);
static_assert(!std::is_copy_constructible_v<Pool>);
static_assert(!std::is_copy_assignable_v<Pool>);
static_assert(!std::is_move_constructible_v<Pool>);
static_assert(!std::is_move_assignable_v<Pool>);
static_assert(Pool::slot_alignment_bytes() >= alignof(Value));
static_assert(lob::FixedObjectPool<OverAligned>::slot_alignment_bytes() >=
              alignof(OverAligned));

class Checks final {
 public:
  void require(bool condition, const char* message) noexcept {
    if (!condition) {
      std::cerr << "fixed-object-pool failure: " << message << '\n';
      failed_ = true;
    }
  }

  [[nodiscard]] bool passed() const noexcept { return !failed_; }

 private:
  bool failed_{};
};

void test_empty_pool_counters(Checks& checks) {
  Pool pool(4);
  checks.require(pool.capacity() == 4, "configured capacity");
  checks.require(pool.used_count() == 0, "empty used count");
  checks.require(pool.free_count() == 4, "empty free count");
  checks.require(pool.high_water_count() == 0, "empty high-water count");
  checks.require(pool.backing_memory_bytes() ==
                     4 * (Pool::slot_size_bytes() +
                          sizeof(Pool::index_type)),
                 "reported backing footprint");
  checks.require(pool.validate_invariants(), "empty invariants");
}

void test_deterministic_order_and_exhaustion(Checks& checks) {
  Pool pool(4);
  std::array<Pool::Handle, 4> handles{};
  for (std::size_t index = 0; index < handles.size(); ++index) {
    const auto acquired = pool.acquire(index + 10);
    checks.require(acquired.acquired(), "initial acquisition");
    checks.require(acquired.handle.index() == index,
                   "initial acquisition uses ascending indexes");
    handles[index] = acquired.handle;
  }
  checks.require(pool.used_count() == 4 && pool.free_count() == 0 &&
                     pool.high_water_count() == 4,
                 "full counters");
  checks.require(pool.validate_invariants(), "full invariants");

  const auto exhausted = pool.acquire(99);
  checks.require(exhausted.status == lob::PoolAcquireStatus::Exhausted &&
                     exhausted.handle.is_invalid(),
                 "explicit exhaustion");
  checks.require(pool.used_count() == 4 && pool.free_count() == 0 &&
                     pool.high_water_count() == 4,
                 "exhaustion leaves counters unchanged");

  constexpr std::array<std::size_t, 4> release_order{1, 3, 0, 2};
  for (const auto index : release_order) {
    checks.require(pool.release(handles[index]) ==
                       lob::PoolReleaseStatus::Released,
                   "release for deterministic reuse");
  }
  checks.require(pool.validate_invariants(), "released invariants");
  for (const auto expected_index : release_order) {
    const auto acquired = pool.acquire(expected_index + 100);
    checks.require(acquired.acquired() &&
                       acquired.handle.index() == expected_index,
                   "FIFO release order controls reuse");
    const auto* value = pool.get(acquired.handle);
    checks.require(value != nullptr && value->value == expected_index + 100,
                   "constructed value is accessible");
    handles[expected_index] = acquired.handle;
  }
  checks.require(pool.validate_invariants(), "reacquired invariants");
  for (const auto handle : handles) {
    checks.require(pool.release(handle) == lob::PoolReleaseStatus::Released,
                   "final deterministic-order release");
  }
}

void test_capacity_one_stale_and_invalid_handles(Checks& checks) {
  Pool first(1);
  Pool second(1);
  const auto acquired = first.acquire(7);
  const auto foreign = second.acquire(8);
  checks.require(acquired.acquired() && foreign.acquired(),
                 "capacity-one acquisitions");

  const auto invalid = Pool::Handle{};
  const auto out_of_range = Pool::Handle::from_raw_parts(
      &first, 1, acquired.handle.generation(), acquired.handle.epoch());
  checks.require(first.get(invalid) == nullptr &&
                     first.release(invalid) ==
                         lob::PoolReleaseStatus::InvalidHandle,
                 "reserved invalid handle");
  checks.require(first.get(out_of_range) == nullptr &&
                     first.release(out_of_range) ==
                         lob::PoolReleaseStatus::InvalidHandle,
                 "out-of-range handle");
  checks.require(first.get(foreign.handle) == nullptr &&
                     first.release(foreign.handle) ==
                         lob::PoolReleaseStatus::InvalidHandle,
                 "foreign-pool handle");
  checks.require(first.used_count() == 1 && first.free_count() == 0,
                 "invalid operations leave capacity-one state unchanged");

  checks.require(first.release(acquired.handle) ==
                     lob::PoolReleaseStatus::Released,
                 "capacity-one release");
  checks.require(first.release(acquired.handle) ==
                     lob::PoolReleaseStatus::InvalidHandle,
                 "duplicate release detected");
  checks.require(first.get(acquired.handle) == nullptr,
                 "released handle is stale");
  const auto reused = first.acquire(9);
  checks.require(reused.acquired() && reused.handle.index() == 0 &&
                     reused.handle.generation() !=
                         acquired.handle.generation(),
                 "capacity-one reuse advances generation");
  checks.require(first.get(acquired.handle) == nullptr &&
                     first.release(acquired.handle) ==
                         lob::PoolReleaseStatus::InvalidHandle,
                 "old generation rejected after reuse");
  checks.require(first.validate_invariants(), "capacity-one invariants");
  checks.require(first.release(reused.handle) ==
                     lob::PoolReleaseStatus::Released,
                 "capacity-one cleanup");
  checks.require(second.release(foreign.handle) ==
                     lob::PoolReleaseStatus::Released,
                 "foreign pool cleanup");
}

void test_free_queue_wrap_and_repeated_cycles(Checks& checks) {
  constexpr std::size_t capacity = 7;
  Pool pool(capacity);
  std::array<Pool::Handle, capacity> handles{};
  for (std::size_t cycle = 0; cycle < 32; ++cycle) {
    for (std::size_t index = 0; index < capacity; ++index) {
      const auto acquired = pool.acquire(cycle * capacity + index);
      checks.require(acquired.acquired() && acquired.handle.index() == index,
                     "wrapped full-cycle acquisition order");
      handles[index] = acquired.handle;
    }
    checks.require(pool.used_count() == capacity && pool.free_count() == 0,
                   "wrapped cycle full counters");
    for (const auto handle : handles) {
      checks.require(pool.release(handle) == lob::PoolReleaseStatus::Released,
                     "wrapped full-cycle release");
    }
    checks.require(pool.used_count() == 0 && pool.free_count() == capacity &&
                       pool.high_water_count() == capacity,
                   "wrapped cycle empty counters");
    checks.require(pool.validate_invariants(), "wrapped cycle invariants");
  }

  std::array<Pool::Handle, 3> first{};
  for (auto& handle : first) {
    handle = pool.acquire(1).handle;
  }
  checks.require(pool.release(first[1]) == lob::PoolReleaseStatus::Released,
                 "partial wrap release one");
  const auto middle = pool.acquire(2);
  checks.require(middle.acquired() && middle.handle.index() == 3,
                 "older free indexes precede a newly released index");
  checks.require(pool.validate_invariants(), "partial wrap invariants");
  checks.require(pool.release(first[0]) == lob::PoolReleaseStatus::Released &&
                     pool.release(first[2]) ==
                         lob::PoolReleaseStatus::Released &&
                     pool.release(middle.handle) ==
                         lob::PoolReleaseStatus::Released,
                 "partial wrap cleanup");
}

void test_production_capacity_and_index_boundaries(Checks& checks) {
  Pool pool;
  checks.require(pool.capacity() == lob::kMaximumActiveOrders,
                 "default production capacity");
  std::vector<Pool::Handle> handles(pool.capacity());
  for (std::size_t index = 0; index < pool.capacity(); ++index) {
    const auto acquired = pool.acquire(index);
    if (!acquired.acquired() || acquired.handle.index() != index) {
      checks.require(false, "production full-capacity acquisition");
      return;
    }
    handles[index] = acquired.handle;
  }
  checks.require(pool.acquire(0).status == lob::PoolAcquireStatus::Exhausted,
                 "production exact-capacity exhaustion");

  constexpr std::array<std::size_t, 6> boundaries{
      0, 1, 65'535, 65'536, lob::kMaximumActiveOrders - 2,
      lob::kMaximumActiveOrders - 1};
  for (const auto index : boundaries) {
    checks.require(pool.release(handles[index]) ==
                       lob::PoolReleaseStatus::Released,
                   "boundary release");
  }
  for (const auto expected_index : boundaries) {
    const auto acquired = pool.acquire(expected_index + 1);
    checks.require(acquired.acquired() &&
                       acquired.handle.index() == expected_index,
                   "boundary deterministic reacquisition");
    handles[expected_index] = acquired.handle;
  }
  checks.require(pool.used_count() == lob::kMaximumActiveOrders &&
                     pool.free_count() == 0 &&
                     pool.high_water_count() == lob::kMaximumActiveOrders,
                 "production counters");
  checks.require(pool.validate_invariants(), "production full invariants");
  for (const auto handle : handles) {
    if (pool.release(handle) != lob::PoolReleaseStatus::Released) {
      checks.require(false, "production full-capacity cleanup");
      return;
    }
  }
  checks.require(pool.validate_invariants(), "production empty invariants");
}

void test_generation_and_epoch_exhaustion_policy(Checks& checks) {
  using SmallGenerationPool = lob::FixedObjectPool<Value, std::uint8_t>;
  SmallGenerationPool pool(1);
  SmallGenerationPool::Handle first{};
  SmallGenerationPool::Handle last{};
  for (std::uint16_t generation = 1;
       generation <= std::numeric_limits<std::uint8_t>::max(); ++generation) {
    const auto acquired = pool.acquire(generation);
    checks.require(acquired.acquired() &&
                       acquired.handle.generation() == generation,
                   "generation advances without wrapping");
    if (generation == 1) {
      first = acquired.handle;
    }
    last = acquired.handle;
    checks.require(pool.release(acquired.handle) ==
                       lob::PoolReleaseStatus::Released,
                   "generation-cycle release");
  }
  const auto counts_before =
      std::array{pool.used_count(), pool.free_count(), pool.high_water_count()};
  const auto generation_failure = pool.acquire(999);
  checks.require(generation_failure.status ==
                     lob::PoolAcquireStatus::GenerationExhausted &&
                     generation_failure.handle.is_invalid(),
                 "generation wrap fails closed");
  checks.require(counts_before ==
                     std::array{pool.used_count(), pool.free_count(),
                                pool.high_water_count()},
                 "generation exhaustion is non-mutating");
  checks.require(pool.get(first) == nullptr && pool.get(last) == nullptr,
                 "released generations remain stale");

  checks.require(pool.reset() == lob::PoolResetStatus::Reset,
                 "reset advances epoch after slot-generation exhaustion");
  const auto after_reset = pool.acquire(1'000);
  checks.require(after_reset.acquired() &&
                     after_reset.handle.generation() == 1 &&
                     after_reset.handle.epoch() != first.epoch(),
                 "new epoch safely restarts slot generations");
  checks.require(pool.get(first) == nullptr && pool.get(last) == nullptr,
                 "epoch rejects all pre-reset handles");
  checks.require(pool.release(after_reset.handle) ==
                     lob::PoolReleaseStatus::Released,
                 "post-generation-reset cleanup");

  using SmallEpochPool =
      lob::FixedObjectPool<Value, std::uint8_t, std::uint8_t>;
  SmallEpochPool epoch_pool(1);
  for (std::uint16_t epoch = 2;
       epoch <= std::numeric_limits<std::uint8_t>::max(); ++epoch) {
    checks.require(epoch_pool.reset() == lob::PoolResetStatus::Reset,
                   "epoch advances without wrapping");
  }
  const auto live = epoch_pool.acquire(44);
  checks.require(live.acquired(), "live object before epoch exhaustion");
  const auto reset_failure = epoch_pool.reset();
  const auto* live_value = epoch_pool.get(live.handle);
  checks.require(reset_failure == lob::PoolResetStatus::GenerationExhausted &&
                     live_value != nullptr && live_value->value == 44 &&
                     epoch_pool.used_count() == 1,
                 "epoch exhaustion leaves live state unchanged");
  checks.require(epoch_pool.release(live.handle) ==
                     lob::PoolReleaseStatus::Released,
                 "epoch-exhaustion cleanup");
}

void test_alignment_and_lifetime(Checks& checks) {
  lob::FixedObjectPool<OverAligned> aligned_pool(3);
  std::array<lob::FixedObjectPool<OverAligned>::Handle, 3> aligned_handles{};
  for (std::size_t index = 0; index < aligned_handles.size(); ++index) {
    const auto acquired = aligned_pool.acquire(index);
    checks.require(acquired.acquired(), "over-aligned acquisition");
    aligned_handles[index] = acquired.handle;
    const auto* object = aligned_pool.get(acquired.handle);
    checks.require(object != nullptr &&
                       reinterpret_cast<std::uintptr_t>(object) %
                               alignof(OverAligned) ==
                           0,
                   "over-aligned object address");
  }
  for (const auto handle : aligned_handles) {
    checks.require(aligned_pool.release(handle) ==
                       lob::PoolReleaseStatus::Released,
                   "over-aligned cleanup");
  }

  LifecycleCounts reset_counts;
  lob::FixedObjectPool<Tracked> reset_pool(8);
  std::array<lob::FixedObjectPool<Tracked>::Handle, 5> reset_handles{};
  for (std::size_t index = 0; index < reset_handles.size(); ++index) {
    reset_handles[index] = reset_pool.acquire(reset_counts, index).handle;
  }
  checks.require(reset_counts.constructions == 5 && reset_counts.live == 5,
                 "non-trivial construction counts");
  checks.require(reset_pool.reset() == lob::PoolResetStatus::Reset,
                 "non-trivial reset");
  checks.require(reset_counts.destructions == 5 && reset_counts.live == 0,
                 "reset destroys every live object exactly once");
  for (const auto handle : reset_handles) {
    checks.require(reset_pool.get(handle) == nullptr &&
                       reset_pool.release(handle) ==
                           lob::PoolReleaseStatus::InvalidHandle,
                   "reset invalidates every prior handle");
  }
  checks.require(reset_pool.used_count() == 0 && reset_pool.free_count() == 8 &&
                     reset_pool.high_water_count() == 5,
                 "reset counters and retained high-water");
  checks.require(reset_pool.reset() == lob::PoolResetStatus::Reset,
                 "repeated reset");
  const auto reused = reset_pool.acquire(reset_counts, 99);
  checks.require(reused.acquired() && reused.handle.index() == 0,
                 "reset restores ascending initial order");
  checks.require(reset_pool.release(reused.handle) ==
                     lob::PoolReleaseStatus::Released,
                 "reset reuse release");
  checks.require(reset_counts.constructions == reset_counts.destructions &&
                     reset_counts.live == 0,
                 "reset reuse lifecycle balance");

  LifecycleCounts destruction_counts;
  {
    lob::FixedObjectPool<Tracked> destruction_pool(6);
    for (std::size_t index = 0; index < 6; ++index) {
      static_cast<void>(destruction_pool.acquire(destruction_counts, index));
    }
    checks.require(destruction_counts.live == 6,
                   "live objects before pool destruction");
  }
  checks.require(destruction_counts.constructions == 6 &&
                     destruction_counts.destructions == 6 &&
                     destruction_counts.live == 0,
                 "pool destruction reclaims every remaining object once");
}

}  // namespace

int main() {
  Checks checks;
  test_empty_pool_counters(checks);
  test_deterministic_order_and_exhaustion(checks);
  test_capacity_one_stale_and_invalid_handles(checks);
  test_free_queue_wrap_and_repeated_cycles(checks);
  test_production_capacity_and_index_boundaries(checks);
  test_generation_and_epoch_exhaustion_policy(checks);
  test_alignment_and_lifetime(checks);
  return checks.passed() ? 0 : 1;
}
