# Baseline Book Storage

## Scope and ownership

`OrderBookStorage` owns the private active state for one instrument: active
orders, the active order-ID lookup, bid and ask price indexes, price-level FIFO
membership, and aggregate leaves quantities. It performs no crossing,
matching, command sequencing, publication, lifecycle transition, networking,
or concurrency work.

Milestone 5 does not change that boundary. `MatchingEngine`, not
`OrderBookStorage`, owns the fixed-capacity execution and status outboxes. The
engine reserves output only after immutable matching and final-state storage
planning succeed, then invokes the existing narrow storage mutations. Storage
neither sees reservations nor exposes nodes or iterators to publication code.
An execution-outbox failure therefore occurs before any storage mutation; the
separate automatic halt changes engine-owned instrument state while preserving
all storage contents and FIFO priority.

Milestone 6 adds one narrow `clear()` operation for the local end-of-day
transition. The matching engine preflights and reserves the required status
record before invoking it. `clear()` removes all active-index entries, bid and
ask levels, FIFO records, and aggregates without deciding lifecycle policy or
publishing output. It retains container capacity where the standard baseline
representation permits, and a subsequent session may reuse previously active
order IDs.

The baseline is correctness-first. It uses standard-library containers:

* an `std::unordered_map` indexes currently active orders by `OrderId`;
* separate ordered `std::map` indexes keep bids descending and asks ascending;
* each price level owns an `std::list` FIFO of private resting-order records.

The active index stores private iterators into those FIFOs. Neither iterators,
nodes, levels, nor container references leave the storage boundary. Copy and
move are disabled so iterator ownership cannot migrate accidentally.

This representation may allocate through standard containers. It is not the
fixed-size object pool assigned to Milestones 9 and 10, and no performance
claim is made for it.

For Milestone 8, allocations and deallocations originating from these
containers during public `MatchingEngine::process()` calls are permitted only
as a measured Phase 1 diagnostic. They must be counted and disclosed, and the
result must be labeled `phase1_allocating_storage`. Benchmark-owned trace,
sample, checksum, statistics, and serialization storage must still be prepared
outside the timed loop. Milestone 10 ends this exception: any allocation or
deallocation anywhere in the timed command-processing path then invalidates
the run.

## Capacity and failure behavior

Production limits are 131,072 active orders per book and 4,096 active price
levels per side. Tests may configure smaller limits; requested limits are
clamped to the production maxima.

Insertion preflights domain values, duplicate active IDs, active-order
capacity, new-level capacity, and aggregate overflow before logical mutation.
Adding to an existing price level remains permitted when that side's level
capacity is full, provided active-order and aggregate capacity remain.
Ordinary rejection returns an explicit `OrderBookResult` and leaves logical
state unchanged.

Order-ID uniqueness is active-only under `docs/book-rules.md` v0.5. Removing an
order erases it from the active index, so the same ID can be used by a later
resting-order insertion. No historical-ID collection is retained.

Aggregate overflow is a representational capacity failure and returns
`CapacityExhausted`. The checked quantity helpers from Milestone 1 are used for
both aggregate addition and subtraction.

## Immutable queries

Queries return counts, optionals, or copied vectors of `RestingOrderView` and
`DepthEntry` values. Mutating those returned values cannot mutate storage.
Depth means all active levels on one requested side, ordered best to worst,
with price, aggregate leaves quantity, and active-order count.

Matching uses `visit_orders_by_priority` to inspect copied, immutable order
views without exposing storage nodes or iterators. The traversal follows each
side's price index and each level's FIFO order. `reduce_resting_by` is the
storage mutation used for a planned fill: it reduces leaves and the aggregate
in place, or removes the order and empty level when the reduction exactly
depletes leaves. It does not make crossing or amendment decisions.

Milestone 4 adds narrow quantity-update and FIFO-tail movement primitives.
They maintain aggregates and private iterator ownership, but the matching
engine decides whether an amendment retains priority, loses priority, or must
use the shared matching plan. Repricing continues to use the existing removal
and insertion primitives only after final-state planning succeeds.

## Invariant scope

Debug builds perform a full structural scan through `validate_invariants()`.
The scan checks index/FIFO bijection, order membership, instrument/side/price
agreement, aggregate sums, non-empty levels, counts, capacities, side ordering,
and best prices. Release builds retain the same callable test hook as a
constant-time success so the full scan is absent from the Release path.

Raw storage intentionally permits crossed bid and ask state for structural
testing. The normative rule that a completed active matching book is not
crossed becomes enforceable when Milestone 3 adds crossing and matching above
this storage boundary.
