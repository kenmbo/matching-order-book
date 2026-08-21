#pragma once

#include "lob/capacity.hpp"

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>

namespace lob {

enum class PoolAcquireStatus : std::uint8_t {
  Acquired,
  Exhausted,
  GenerationExhausted,
};

enum class PoolReleaseStatus : std::uint8_t {
  Released,
  InvalidHandle,
};

enum class PoolResetStatus : std::uint8_t {
  Reset,
  GenerationExhausted,
};

template <typename T, std::unsigned_integral Generation = std::uint64_t,
          std::unsigned_integral Epoch = std::uint64_t>
class FixedObjectPool final {
  static_assert(!std::same_as<Generation, bool>);
  static_assert(!std::same_as<Epoch, bool>);
  static_assert(std::is_nothrow_destructible_v<T>);

 public:
  using value_type = T;
  using index_type = std::uint32_t;
  using generation_type = Generation;
  using epoch_type = Epoch;

  static constexpr index_type kInvalidIndex =
      std::numeric_limits<index_type>::max();

  class Handle final {
   public:
    constexpr Handle() noexcept = default;

    [[nodiscard]] static constexpr Handle from_raw_parts(
        const FixedObjectPool* owner, index_type index, generation_type generation,
        epoch_type epoch) noexcept {
      return Handle{owner, index, generation, epoch};
    }

    [[nodiscard]] constexpr bool is_invalid() const noexcept {
      return owner_ == nullptr || index_ == kInvalidIndex || generation_ == 0 ||
             epoch_ == 0;
    }
    [[nodiscard]] constexpr index_type index() const noexcept { return index_; }
    [[nodiscard]] constexpr generation_type generation() const noexcept {
      return generation_;
    }
    [[nodiscard]] constexpr epoch_type epoch() const noexcept { return epoch_; }

    constexpr bool operator==(const Handle&) const noexcept = default;

   private:
    friend class FixedObjectPool;

    constexpr Handle(const FixedObjectPool* owner, index_type index,
                     generation_type generation, epoch_type epoch) noexcept
        : owner_(owner), index_(index), generation_(generation), epoch_(epoch) {}

    const FixedObjectPool* owner_{};
    index_type index_{kInvalidIndex};
    generation_type generation_{};
    epoch_type epoch_{};
  };

  struct AcquireResult final {
    PoolAcquireStatus status{PoolAcquireStatus::Exhausted};
    Handle handle{};

    [[nodiscard]] constexpr bool acquired() const noexcept {
      return status == PoolAcquireStatus::Acquired;
    }
  };

  explicit FixedObjectPool(std::size_t capacity = kMaximumActiveOrders)
      : capacity_(checked_capacity(capacity)),
        slots_(std::make_unique<Slot[]>(capacity_)),
        free_indices_(std::make_unique<index_type[]>(capacity_)) {
    initialize_empty_state();
  }

  FixedObjectPool(const FixedObjectPool&) = delete;
  FixedObjectPool& operator=(const FixedObjectPool&) = delete;
  FixedObjectPool(FixedObjectPool&&) = delete;
  FixedObjectPool& operator=(FixedObjectPool&&) = delete;

  ~FixedObjectPool() { destroy_live_objects(); }

  template <typename... Args>
    requires std::is_nothrow_constructible_v<T, Args...>
  [[nodiscard]] AcquireResult acquire(Args&&... args) noexcept {
    const auto status = next_acquire_status();
    if (status != PoolAcquireStatus::Acquired) {
      return {status, {}};
    }

    const auto index = free_indices_[free_head_];
    Slot& slot = slots_[index];
    const auto next_generation =
        static_cast<generation_type>(slot.generation + generation_type{1});
    std::construct_at(slot.raw_object_pointer(), std::forward<Args>(args)...);
    slot.generation = next_generation;
    slot.occupied = true;
    slot.free_position = kInvalidIndex;
    free_head_ = advance(free_head_);
    --free_count_;
    ++used_count_;
    if (used_count_ > high_water_count_) {
      high_water_count_ = used_count_;
    }
    return {PoolAcquireStatus::Acquired,
            Handle{this, index, slot.generation, epoch_}};
  }

  [[nodiscard]] T* get(Handle handle) noexcept {
    if (!valid_handle(handle)) {
      return nullptr;
    }
    return slots_[handle.index_].live_object_pointer();
  }

  [[nodiscard]] const T* get(Handle handle) const noexcept {
    if (!valid_handle(handle)) {
      return nullptr;
    }
    return slots_[handle.index_].live_object_pointer();
  }

  [[nodiscard]] PoolReleaseStatus release(Handle handle) noexcept {
    if (!valid_handle(handle)) {
      return PoolReleaseStatus::InvalidHandle;
    }

    Slot& slot = slots_[handle.index_];
    std::destroy_at(slot.live_object_pointer());
    slot.occupied = false;
    free_indices_[free_tail_] = handle.index_;
    slot.free_position = static_cast<index_type>(free_tail_);
    free_tail_ = advance(free_tail_);
    ++free_count_;
    --used_count_;
    return PoolReleaseStatus::Released;
  }

  [[nodiscard]] PoolResetStatus reset() noexcept {
    if (!can_reset()) {
      return PoolResetStatus::GenerationExhausted;
    }

    destroy_live_objects();
    epoch_ = static_cast<epoch_type>(epoch_ + epoch_type{1});
    initialize_empty_state();
    return PoolResetStatus::Reset;
  }

  [[nodiscard]] std::size_t capacity() const noexcept { return capacity_; }
  [[nodiscard]] std::size_t used_count() const noexcept { return used_count_; }
  [[nodiscard]] std::size_t free_count() const noexcept { return free_count_; }
  [[nodiscard]] std::size_t high_water_count() const noexcept {
    return high_water_count_;
  }
  [[nodiscard]] PoolAcquireStatus next_acquire_status() const noexcept {
    if (free_count_ == 0) {
      return PoolAcquireStatus::Exhausted;
    }
    const auto index = free_indices_[free_head_];
    return slots_[index].generation ==
                   std::numeric_limits<generation_type>::max()
               ? PoolAcquireStatus::GenerationExhausted
               : PoolAcquireStatus::Acquired;
  }
  [[nodiscard]] PoolAcquireStatus acquire_status_after_release(
      Handle first_release) const noexcept {
    if (free_count_ != 0) {
      return next_acquire_status();
    }
    if (!valid_handle(first_release)) {
      return PoolAcquireStatus::Exhausted;
    }
    return slots_[first_release.index_].generation ==
                   std::numeric_limits<generation_type>::max()
               ? PoolAcquireStatus::GenerationExhausted
               : PoolAcquireStatus::Acquired;
  }
  [[nodiscard]] bool can_reset() const noexcept {
    return epoch_ != std::numeric_limits<epoch_type>::max();
  }
  [[nodiscard]] std::size_t backing_memory_bytes() const noexcept {
    return capacity_ * (sizeof(Slot) + sizeof(index_type));
  }
  [[nodiscard]] std::size_t slot_backing_memory_bytes() const noexcept {
    return capacity_ * sizeof(Slot);
  }
  [[nodiscard]] std::size_t free_index_backing_memory_bytes() const noexcept {
    return capacity_ * sizeof(index_type);
  }
  [[nodiscard]] static constexpr std::size_t slot_size_bytes() noexcept {
    return sizeof(Slot);
  }
  [[nodiscard]] static constexpr std::size_t slot_alignment_bytes() noexcept {
    return alignof(Slot);
  }

#ifndef NDEBUG
  [[nodiscard]] bool validate_invariants() const noexcept {
    if (capacity_ == 0 || capacity_ >= kInvalidIndex || epoch_ == 0 ||
        free_head_ >= capacity_ || free_tail_ >= capacity_ ||
        used_count_ > capacity_ || free_count_ > capacity_ ||
        used_count_ + free_count_ != capacity_ ||
        high_water_count_ < used_count_ || high_water_count_ > capacity_ ||
        free_tail_ != (free_head_ + free_count_) % capacity_) {
      return false;
    }

    std::size_t occupied_count = 0;
    for (std::size_t index = 0; index < capacity_; ++index) {
      const Slot& slot = slots_[index];
      if (slot.occupied) {
        if (slot.free_position != kInvalidIndex || slot.generation == 0) {
          return false;
        }
        ++occupied_count;
        continue;
      }

      if (slot.free_position >= capacity_ ||
          !position_is_free(slot.free_position) ||
          free_indices_[slot.free_position] != index) {
        return false;
      }
    }
    if (occupied_count != used_count_) {
      return false;
    }

    for (std::size_t offset = 0; offset < free_count_; ++offset) {
      const auto position = (free_head_ + offset) % capacity_;
      const auto index = free_indices_[position];
      if (index >= capacity_ || slots_[index].occupied ||
          slots_[index].free_position != position) {
        return false;
      }
    }
    return true;
  }
#else
  [[nodiscard]] constexpr bool validate_invariants() const noexcept {
    return true;
  }
#endif

 private:
  struct Slot final {
    alignas(T) std::byte storage[sizeof(T)]{};
    generation_type generation{};
    index_type free_position{kInvalidIndex};
    bool occupied{};

    [[nodiscard]] T* raw_object_pointer() noexcept {
      return reinterpret_cast<T*>(storage);
    }
    [[nodiscard]] T* live_object_pointer() noexcept {
      return std::launder(reinterpret_cast<T*>(storage));
    }
    [[nodiscard]] const T* live_object_pointer() const noexcept {
      return std::launder(reinterpret_cast<const T*>(storage));
    }
  };

  [[nodiscard]] static std::size_t checked_capacity(std::size_t capacity) {
    if (capacity == 0 || capacity >= kInvalidIndex) {
      std::abort();
    }
    return capacity;
  }

  [[nodiscard]] std::size_t advance(std::size_t position) const noexcept {
    ++position;
    return position == capacity_ ? 0 : position;
  }

  [[nodiscard]] bool valid_handle(Handle handle) const noexcept {
    return handle.owner_ == this && handle.epoch_ == epoch_ &&
           handle.index_ < capacity_ && handle.generation_ != 0 &&
           slots_[handle.index_].occupied &&
           slots_[handle.index_].generation == handle.generation_;
  }

#ifndef NDEBUG
  [[nodiscard]] bool position_is_free(std::size_t position) const noexcept {
    if (free_count_ == capacity_) {
      return true;
    }
    if (free_count_ == 0) {
      return false;
    }
    if (free_head_ < free_tail_) {
      return position >= free_head_ && position < free_tail_;
    }
    return position >= free_head_ || position < free_tail_;
  }
#endif

  void initialize_empty_state() noexcept {
    for (std::size_t index = 0; index < capacity_; ++index) {
      slots_[index].generation = 0;
      slots_[index].free_position = static_cast<index_type>(index);
      slots_[index].occupied = false;
      free_indices_[index] = static_cast<index_type>(index);
    }
    free_head_ = 0;
    free_tail_ = 0;
    used_count_ = 0;
    free_count_ = capacity_;
  }

  void destroy_live_objects() noexcept {
    for (std::size_t index = 0; index < capacity_; ++index) {
      Slot& slot = slots_[index];
      if (slot.occupied) {
        std::destroy_at(slot.live_object_pointer());
        slot.occupied = false;
      }
    }
  }

  std::size_t capacity_{};
  std::unique_ptr<Slot[]> slots_{};
  std::unique_ptr<index_type[]> free_indices_{};
  std::size_t free_head_{};
  std::size_t free_tail_{};
  std::size_t used_count_{};
  std::size_t free_count_{};
  std::size_t high_water_count_{};
  epoch_type epoch_{1};
};

}  // namespace lob
