# Standalone Fixed-Object Pool

## Scope and ownership

Milestone 9 provides `FixedObjectPool<T>` as a standalone, single-writer
component. It is not connected to `OrderBookStorage` or `MatchingEngine`.
Milestone 10 owns that integration and its transaction preflight; this
milestone does not change matching behavior, public `OrderId` reuse, or
`OrderBookResult`.

The default capacity uses the one neutral `kMaximumActiveOrders` constant and
is exactly 131,072 slots. Tests and component benchmarks may configure a
smaller non-zero capacity. Construction allocates both complete backing
arrays: one aligned slot array containing object storage and slot metadata,
and one fixed free-index array. Copy and move are disabled so pool identity,
backing storage, and handles cannot migrate.

Construction is a startup operation and may report allocation failure through
the ordinary C++ allocation mechanism. After successful construction,
`acquire`, `get`, `release`, and `reset` neither allocate nor deallocate heap
memory. The pool owns every constructed object until a successful release,
reset, or pool destruction ends that object's lifetime.

## Handles and results

A handle contains:

* the pool owner's identity;
* a 32-bit slot index;
* a per-slot generation; and
* a pool reset epoch.

The default handle uses 64-bit unsigned generation and epoch fields. A null
owner, the maximum 32-bit index, generation zero, and epoch zero are reserved
as invalid values. Access and release validate owner, bounds, occupancy,
generation, and epoch before touching object storage. A handle from another
pool, a malformed handle, a duplicate release, or a stale handle therefore
returns `nullptr` or `PoolReleaseStatus::InvalidHandle` without changing the
free ring, counters, objects, or later allocation order.

Slot generations advance before each construction. Generation values do not
wrap: if the next acquisition would advance a slot beyond its maximum value,
it returns `PoolAcquireStatus::GenerationExhausted` without mutation. A
successful reset advances the pool epoch and restarts every slot generation
at zero, so every pre-reset handle stays invalid. Epoch values also do not
wrap; reset returns `PoolResetStatus::GenerationExhausted` without mutation if
the current epoch is already maximal. Production 64-bit fields make either
condition operationally remote, while smaller unsigned types permit direct
boundary tests. This fail-closed policy prevents the handle alias that a
modular generation wrap would otherwise permit.

Internal handles remain independent of public `OrderId`. They neither retain
historical order IDs nor alter the active-only reuse rule in
`docs/book-rules.md`.

## Allocation order and counters

Free indexes are held in a fixed circular FIFO. Startup acquisition order is
ascending slot index, `0..capacity-1`. Release appends its index to the FIFO
tail, so once older free slots have been consumed, slots are reacquired in
release order. A slot cannot enter the free FIFO while it is live. Reset
rebuilds the FIFO in the original ascending order.

`capacity()`, `used_count()`, `free_count()`, and `high_water_count()` are
constant-time diagnostics. Successful acquisition increments used count and
updates high water; successful release decrements used exactly once. Failed
operations do not change any counter. Reset returns used/free to
`0/capacity`, retains the diagnostic high-water count, and does not release
the backing arrays.

Debug `validate_invariants()` scans the configured slots and active free-ring
span. It verifies occupancy/free membership, the one-to-one slot/index
mapping, queue-position uniqueness, count identities, current metadata, and
ring cursors. Invalid and duplicate releases are explicit results in every
build and are exercised with the full Debug scan.

## Alignment and lifetime

Each slot uses `alignas(T)` uninitialized byte storage. Acquisition begins a
lifetime with `std::construct_at`; validated access launders the pointer to the
live object; release, reset, and destruction end lifetimes with
`std::destroy_at`. Inactive object representations are never read.

Pooled types must be nothrow destructible, and an `acquire(args...)` overload
participates only when `T` is nothrow constructible from those arguments.
This keeps construction and cleanup out of exception paths intended for later
matching use. Over-aligned and non-trivially destructible types are covered by
component and sanitizer tests.

Reset scans all slots, destroys each live object exactly once, advances the
epoch, and restores the initial free order. Pool destruction destroys each
remaining live object exactly once before its two backing allocations are
released. Repeated reset and reuse are supported.

## Standalone benchmark boundary

`lob_fixed_object_pool_benchmark` measures only pool construction-independent
acquire/release work in Release. It warms the pool, pre-generates the churn
positions, and preallocates handles and sample arrays. It reports individual
acquire/release latency, complete full-cycle timing, and paired steady-state
churn timing with checksums and environment/storage metadata.

`lob_fixed_object_pool_allocation_audit` repeats the same deterministic
operations with bounded global allocation counters. Pool construction and
benchmark preparation are separate permitted phases. Any allocation or
deallocation in timed pool operations or timed sample/checksum collection
invalidates the run. Milestone 9 sets no latency threshold; this component
baseline must not be compared directly with or overwrite the Milestone 8
public `MatchingEngine::process()` baseline.
