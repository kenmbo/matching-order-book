# Pool-backed Book Storage

## Scope and ownership

`OrderBookStorage` is the private mutable storage boundary for one instrument.
It owns all startup backing memory for resting orders, active-order lookup, bid
levels, and ask levels. `MatchingEngine` remains the transaction and sequencing
owner; public callers still receive copied `RestingOrderView` and `DepthEntry`
values and never receive pool handles, slot indexes, generations, epochs, or
mutable storage pointers.

The production active-order capacity is the single neutral
`kMaximumActiveOrders` constant, 131,072. Focused tests may configure smaller
active-order and per-side level limits. Storage remains single-writer,
non-copyable, and non-movable.

## Bounded representation

Construction performs every backing allocation:

* `FixedObjectPool<OrderRecord>` owns order-node slots and its FIFO free-index
  queue;
* a power-of-two open-addressed active-ID table owns at least two buckets per
  configured active order;
* one sorted fixed-capacity `PriceLevel` array is allocated for bids and one for
  asks;
* the matching engine separately allocates both lossless outbox arrays.

An order node stores its public order fields plus private index/generation/epoch
links to the previous and next node. A level stores aggregate leaves, count,
and head/tail links. Bids are maintained in descending array order and asks in
ascending order. Each level is FIFO from head to tail. Creating or removing a
level may shift a bounded number of level records, but it never allocates or
invalidates an order handle.

The active-ID table uses deterministic linear probing and backward-shift
deletion. It has no node allocation, tombstones, global-heap fallback, or
allocator cache. Public `OrderId` is only the lookup key and remains reusable
as soon as its prior order ceases to be active; it is never a pool identity.

## FIFO and release rules

New resting orders append to the destination tail. Partial fills and
same-price reductions retain both node identity and FIFO position. A
same-price increase unlinks the same node and appends it to the tail. A price
change removes and releases the old representation before the amended
remainder is acquired and appended, so priority is lost. Cancellation, full
fill, price-changing replacement, close/reset, and destruction return or
destroy every affected node exactly once. Empty levels are removed.

The pool's deterministic FIFO free-index rule therefore determines reuse:
initial acquisitions use ascending slot indexes, and released slots are reused
in release order. Storage does not expose that identity through production
APIs.

## Transaction preflight

Matching retains the `Validate -> Plan -> Reserve -> Execute -> Commit`
lifecycle. Before logical mutation, preflight proves:

* the final active-order count fits;
* the final destination-side level count fits;
* destination aggregate arithmetic is representable;
* engine and match sequence ranges fit;
* the execution outbox can reserve the complete report batch;
* when a remainder must rest, the pool's next free slot can advance its
  generation.

If the pool is currently full, preflight names the first immutable planned
removal: an amended order is removed first, otherwise it is the first complete
resting fill. The pool verifies that exact slot can be acquired after release.
Consequently a full book is not falsely rejected when execution is guaranteed
to make room before a remainder rests. With these resources proven and the
outbox reserved, intrusive unlink/link operations, index changes, level shifts,
pool release/acquire, report writes, and publication are infallible.

Capacity or generation failure uses the existing
`OrderBookResult::CapacityExhausted`; pool-local statuses remain private. A
failure before execution changes no book state, published report, match ID, or
engine sequence. Existing command-sequence acceptance and fail-closed outbox
rules are unchanged.

## Reset and diagnostics

Close preflights pool epoch advancement before lifecycle mutation or status
publication. Reset clears active-ID and level membership, destroys every live
order through the pool, restores deterministic ascending initial allocation
order, advances the epoch, and retains all backing allocations. Pool used/free
counts become `0/capacity`; pool and level high-water values are retained.
Pre-reset links are stale because their epoch no longer matches.

Constant-time diagnostics report configured capacity, pool used/free/high-water
counts, bid/ask level high-water counts, order-node and pool-slot size/alignment,
slot and free-index backing, active-index capacity/backing, level backing, and
the total configured storage footprint.

## Debug invariants

Debug validation checks the pool free structure and handle metadata, active-ID
table structure, the active-index/FIFO/pool bijection, intrusive forward and
back links, unique reachability, side/price ownership, aggregate quantities,
counts and capacities, price ordering, and BBO consistency. No mutation path
uses locks, atomics, exceptions, or heap allocation/deallocation.
