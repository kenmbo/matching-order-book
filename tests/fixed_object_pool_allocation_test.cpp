#include "lob/memory/fixed_object_pool.hpp"

#include <array>
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

  [[nodiscard]] constexpr Counters operator-(const Counters& other) const
      noexcept {
    return {allocations - other.allocations,
            allocated_bytes - other.allocated_bytes,
            deallocations - other.deallocations};
  }
};

Counters counters{};

void allocation(std::size_t bytes) noexcept {
  ++counters.allocations;
  counters.allocated_bytes += bytes;
}

void deallocation() noexcept { ++counters.deallocations; }

[[nodiscard]] Counters snapshot() noexcept { return counters; }

}  // namespace allocation_audit

void* operator new(std::size_t bytes) {
  const auto allocation_size = bytes == 0 ? std::size_t{1} : bytes;
  if (void* memory = std::malloc(allocation_size)) {
    allocation_audit::allocation(allocation_size);
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
  const auto allocation_size = bytes == 0 ? std::size_t{1} : bytes;
  if (posix_memalign(&memory, static_cast<std::size_t>(alignment),
                     allocation_size) != 0) {
    throw std::bad_alloc{};
  }
  allocation_audit::allocation(allocation_size);
  return memory;
}

void* operator new[](std::size_t bytes, std::align_val_t alignment) {
  return ::operator new(bytes, alignment);
}

void operator delete(void* memory, std::align_val_t) noexcept {
  ::operator delete(memory);
}

void operator delete[](void* memory, std::align_val_t) noexcept {
  ::operator delete(memory);
}

void operator delete(void* memory, std::size_t,
                     std::align_val_t) noexcept {
  ::operator delete(memory);
}

void operator delete[](void* memory, std::size_t,
                       std::align_val_t) noexcept {
  ::operator delete(memory);
}

namespace {

struct alignas(128) AuditedObject final {
  std::uint64_t value{};

  explicit AuditedObject(std::uint64_t initial) noexcept : value(initial) {}
  ~AuditedObject() noexcept { value = 0; }
};

using Pool = lob::FixedObjectPool<AuditedObject>;

[[nodiscard]] bool equal(allocation_audit::Counters lhs,
                         allocation_audit::Counters rhs) noexcept {
  return lhs.allocations == rhs.allocations &&
         lhs.allocated_bytes == rhs.allocated_bytes &&
         lhs.deallocations == rhs.deallocations;
}

}  // namespace

int main() {
  constexpr std::size_t capacity = 1'024;
  const auto before_construction = allocation_audit::snapshot();
  allocation_audit::Counters startup{};
  allocation_audit::Counters steady{};
  allocation_audit::Counters destruction{};
  std::size_t reported_backing_bytes = 0;
  {
    Pool pool(capacity);
    reported_backing_bytes = pool.backing_memory_bytes();
    startup = allocation_audit::snapshot() - before_construction;
    std::array<Pool::Handle, capacity> handles{};
    const auto before_steady = allocation_audit::snapshot();

    for (std::size_t cycle = 0; cycle < 8; ++cycle) {
      for (std::size_t index = 0; index < capacity; ++index) {
        const auto acquired = pool.acquire(cycle * capacity + index);
        if (!acquired.acquired()) {
          std::fputs("allocation audit acquire failure\n", stderr);
          return 1;
        }
        handles[index] = acquired.handle;
      }
      for (std::size_t index = 0; index < capacity; index += 2) {
        if (pool.release(handles[index]) != lob::PoolReleaseStatus::Released) {
          std::fputs("allocation audit release failure\n", stderr);
          return 1;
        }
      }
      for (std::size_t index = 0; index < capacity; index += 2) {
        const auto acquired = pool.acquire(index);
        if (!acquired.acquired()) {
          std::fputs("allocation audit reacquire failure\n", stderr);
          return 1;
        }
        handles[index] = acquired.handle;
      }
      if (pool.reset() != lob::PoolResetStatus::Reset) {
        std::fputs("allocation audit reset failure\n", stderr);
        return 1;
      }
    }
    steady = allocation_audit::snapshot() - before_steady;
    const auto before_destruction = allocation_audit::snapshot();
    static_cast<void>(before_destruction);
  }
  destruction = allocation_audit::snapshot() - before_construction - startup -
                steady;

  const bool startup_valid =
      startup.allocations == 2 && startup.deallocations == 0 &&
      startup.allocated_bytes >= reported_backing_bytes;
  const bool steady_valid = equal(steady, {});
  const bool destruction_valid = destruction.allocations == 0 &&
                                 destruction.deallocations == 2;
  if (!startup_valid || !steady_valid || !destruction_valid) {
    std::fprintf(stderr,
                 "allocation audit mismatch: startup=%llu/%llu/%llu "
                 "steady=%llu/%llu/%llu destruction=%llu/%llu/%llu\n",
                 static_cast<unsigned long long>(startup.allocations),
                 static_cast<unsigned long long>(startup.allocated_bytes),
                 static_cast<unsigned long long>(startup.deallocations),
                 static_cast<unsigned long long>(steady.allocations),
                 static_cast<unsigned long long>(steady.allocated_bytes),
                 static_cast<unsigned long long>(steady.deallocations),
                 static_cast<unsigned long long>(destruction.allocations),
                 static_cast<unsigned long long>(destruction.allocated_bytes),
                 static_cast<unsigned long long>(destruction.deallocations));
    return 1;
  }
  return 0;
}
