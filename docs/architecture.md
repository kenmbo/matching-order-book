# Limit Order Book Architecture

**Status:** Draft v0.1
**Scope:** Local matching engine now; external multicast market-data
reconstruction in a later phase.

## 1. Purpose and authority

This document defines component boundaries, ownership, data flow, and failure
handling for the Limit Order Book (LOB) system.  It explains *where* behavior
lives; [`book-rules.md`](book-rules.md) defines *what* that behavior must be.

Project decisions follow this authority order:

1. `docs/book-rules.md` defines normative behavior.
2. `docs/architecture.md` defines ownership and architectural boundaries.
3. `TODO.md` defines implementation order and acceptance gates.
4. `AGENTS.md` defines the required build, validation, repository, and agent
   workflow.

A lower-ranked document must not silently reinterpret a higher-ranked one.

## 2. Scope and phased delivery

| Phase | Deliverable                                                                                         | Concurrency model                                                           |
| ----- | --------------------------------------------------------------------------------------------------- | --------------------------------------------------------------------------- |
| 1     | Correct local matching engine for one instrument per book                                           | Synchronous direct calls; one writer; no sockets or inter-thread queues.    |
| 2     | Fixed-size object-pool allocator and memory measurements                                            | Still single-writer; no required additional threads.                        |
| 3     | External outbound multicast market-data feed handler, incremental re-request, and snapshot recovery | Bounded hand-offs and independently owned I/O; no concurrent book mutation. |

CPU pinning, NIC/IRQ tuning, and additional threads are performance work for
Phase 3 or later.  They are not correctness requirements for Phases 1–2.

## 3. Architectural principles

* **Behavior before optimization.** Preserve price-time priority and all book
  invariants before introducing pools, lock-free queues, or CPU affinity.
* **One writer per book.** Only its owning serial executor may mutate a book.
  Readers receive events or immutable snapshots; they never inspect internal
  orders or price levels directly.
* **Separate semantics.** Local matching and external market-data
  reconstruction are separate logical book instances.  They may reuse code,
  but they never share mutable state or event streams.
* **Bounded work.** Hot paths avoid exceptions, unbounded queues, uncontrolled
  allocation, and blocking I/O.
* **Fail closed for lossless output.** A mutation that requires mandatory
  output does not commit until its complete output batch has reserved capacity.
* **Recover by channel.** A multicast channel, not an individual instrument,
  owns sequence state and recovery because one packet can contain messages for
  several instruments.

## 4. Logical topology

```mermaid
flowchart TD
  subgraph L["Local matching path"]
    Entry["Order entry / test harness"] --> Matcher["Local matching engine"]
    Matcher --> LocalBook["Private local book state"]
    Matcher --> Outbox["Lossless execution outbox"]
  end

  subgraph M["External market-data path"]
    Udp["UDP ingress and packet decoder"] --> Gate["Channel recovery gate"]
    Gate --> Applier["Market-data applier"]
    Applier --> ExternalBook["Private external book state"]
  end
```

The two paths are deliberately not connected.  If the project later publishes
the local engine's activity over UDP, that is an **outbound publisher**, not an
external market-data feed handler.

## 5. Components and ownership

| Component                      | Owns                                                                              | Responsibilities                                                                                                  | Must not do                                                         |
| ------------------------------ | --------------------------------------------------------------------------------- | ----------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------- |
| Local matching engine          | One local matching-book executor, lifecycle state, and local outboxes             | Gate normalized order/lifecycle commands, match orders, reset at close, and publish local events.                  | Decode packets, perform I/O, or mutate an external book.            |
| `OrderBookStorage`             | Order-ID index, price-level indexes, FIFO queues, aggregates, and order memory    | Represent local or external book state privately for its owner.                                                   | Expose mutable nodes or accept concurrent access.                   |
| Lossless execution outbox      | Fixed-capacity execution-report ring and an independent system-status ring        | Reserve and commit mandatory local output; expose only committed records.                                         | Drop or overwrite a committed report, expose a partial batch, or perform downstream work. |
| UDP ingress and packet decoder | Socket interaction and receive buffers                                            | Receive datagrams; validate framing, lengths, and checksums; produce packet envelopes.                            | Apply messages to a book or decide recovery state.                  |
| Channel recovery gate          | Per-channel session, expected message sequence, bounded cache, and recovery state | Sequence packets, perform incremental re-request, select snapshot recovery, and release only contiguous messages. | Mutate order nodes directly or block on consumer work.              |
| Incremental re-request client  | Request transport for a bounded missing message range                             | Request and return retransmitted packets to the recovery gate.                                                    | Decide channel state or apply messages.                             |
| Snapshot client                | Snapshot transport and response bytes                                             | Request a complete channel snapshot and return its watermark plus payload.                                        | Mark books current or drain cached live data.                       |
| Market-data applier            | One external market-data-book executor per assigned book/shard                    | Apply normalized external `Add`, `Cancel`, `Execute`, snapshot, and status events.                                | Match orders or accept local order-entry commands.                  |
| Output consumers               | Their own read-side state                                                         | Consume execution, status, or analytics output.                                                                   | Read a book's private memory or stall a book writer.                |

## 6. Local matching path

Phase 1 calls the local matching engine directly from tests or a simple local
application. There is no dispatcher or inter-thread queue requirement at this
stage; the owned outboxes are passive bounded storage on the same serial
executor.

For a mutating command capable of producing lossless output, the engine uses
this lifecycle:

1. **Validate** the command, identifier, price, quantity, and instrument state.
2. **Plan** the fills and the exact mandatory output batch without changing the
   book.
3. **Reserve** exactly one slot for every required execution report in the
   lossless outbox.
4. **Execute** the planned order, queue, and aggregate mutations, writing the
   reports into the reserved slots.
5. **Commit** the batch and advance the engine sequence.

`MatchingEngine` owns two `LosslessOutbox<T>` instances: one for
`ExecutionReport` and a physically independent control outbox for
`SystemStatus`. `OrderBookStorage` has no outbox reference and remains unaware
of publication. The default execution and control capacities are 1,024 and 16
records respectively, and tests may configure smaller powers of two. Both
rings allocate their complete slot arrays during engine construction. Runtime
configuration is rejected at startup unless a capacity is non-zero and a
power of two; status capacity must additionally be at least two.

Each ring permits one exact producer reservation at a time. Reserved slots are
deducted from available capacity but remain invisible to consumption. The
producer writes only through the reservation and publishes the entire batch by
one cursor commit; an explicit abort or reservation destruction restores all
reserved capacity without publication. Read and write cursor advancement uses
`index & (capacity - 1)`. Occupancy prevents a committed record from being
overwritten before it is consumed, including across wraparound. A zero-record
reservation is a valid no-op even when the execution outbox is full.

`Commit` is infallible for a correctly filled reservation because capacity and
all ring storage were secured before mutation. On execution-outbox reservation
failure, the accepted order transaction stops without book mutation,
execution-report publication, `MatchId` assignment, or execution-report
`EngineSequence` assignment. A second control transaction reserves one status
slot, changes the instrument from `Active` to `Halted`, and commits one
`SystemStatus` with the next `EngineSequence`. Existing execution-outbox
contents remain unchanged. This automatic `LosslessOutboxFull` safety
transition was the only lifecycle transition delivered in Milestone 5;
Milestone 6 integrates it with operator halt, resume, close, and open.

Milestone 6 makes lifecycle control an explicit matching-engine boundary, not
a storage or consumer responsibility. The public normalized controls are
`HaltInstrument`, `ResumeInstrument`, `CloseInstrument`, and `OpenInstrument`.
The engine alone owns the `Active`, `Halted`, and `Closed` state, validates the
transition matrix, gates order commands, reserves status publication, and then
applies a transition. `OrderBookStorage` only performs the requested clear for
a successfully preflighted close.

| Current state | Halt | Resume | Close | Open |
| --- | --- | --- | --- | --- |
| `Active` | `Halted` | Reject | `Closed` | Reject |
| `Halted` | Reject | `Active` | `Closed` | Reject |
| `Closed` | Reject | Reject | Reject | `Active` |

Invalid pairs return `InvalidStateTransition` before command acceptance.
Successful halt and resume preserve storage and FIFO priority. Close is the
only destructive local transition and empties the book. Open begins a new
empty session without resetting sequence, match-ID, diagnostic, or committed
outbox state.

`OrderBookStorage` is private to the matching engine.  The initial baseline
uses an order-ID lookup, price-level indexes, and FIFO queues; Phase 2 may
replace per-order allocation with a fixed-size object pool without changing
the public behavior or component boundary.

The Phase 1 storage representation is intentionally allocating. Its bounded
logical capacities do not imply that standard-container nodes were
preallocated. Milestone 8 measures and discloses those timed allocations as a
diagnostic while requiring the benchmark harness itself to remain
allocation-free inside the timed loop. Milestone 10 replaces this exception
with strict zero allocation and deallocation for the complete timed
command-processing path and records a separate before/after baseline.

Milestone 3 implements the `Plan` and `Execute` sides of this lifecycle for
`NewOrder`.  Its internal fixed-size plan records at most 256 resting order
IDs, resting prices, and fill quantities, plus the aggressive remainder.  The
planning pass traverses immutable storage views in price-time order and makes
no mutation or sequence assignment.  Final active-order, price-level,
aggregate, match-ID, and engine-sequence capacity are checked before the plan
is executed.

Milestone 5 inserts physical reservation between the existing plan and
execution steps; it does not replace the match plan or introduce a competing
transaction model. `process(NewOrder)` and `process(AmendOrder)` retain their
bounded synchronous report arrays as post-commit mirrors for direct callers,
but the owned execution outbox is the authoritative publication boundary. A
caller cannot receive the synchronous success result until the entire planned
batch has committed to that outbox.

Milestone 4 reuses that same match planner for price-changing amendments by
describing both a new order and an amended order as one private logical
aggressive order.  The amended order remains in its original FIFO location
while the engine plans fills and preflights the final state.  Capacity
accounting logically subtracts the replaced order and any fully filled resting
orders before adding an amended remainder; only then does execution remove the
old representation, apply the shared fill plan, and append any remainder.
Cancel and same-price amendments produce zero reports, while a marketable
amendment uses the same physical reservation and exact bounded report batch as
a marketable new order. Non-marketable replacement amendments pass through a
zero-record reservation and therefore remain valid when execution-report
capacity is saturated.

One planned fill maps to exactly one `ExecutionReport`, one `MatchId`, and one
`EngineSequence`. The immutable plan contains at most 256 fills, so the maximum
execution-outbox batch is exactly 256 records. An outbox with 256 available
slots accepts that batch exactly; 255 available slots do not. A command whose
plan would require 257 fills fails planning before reservation and does not
trigger the outbox-full safety transition.

### 6.1 Lifecycle control transaction

A valid lifecycle command follows `Validate -> Accept -> Preflight -> Reserve
-> Transition/Reset -> Commit`. Acceptance consumes one `CommandSequence`.
Preflight requires one status slot and one engine sequence; failure afterward
retains the command sequence but changes no state or book contents and
publishes nothing. `StatusOutboxFull` identifies ordinary control saturation,
while `CapacityExhausted` identifies engine-sequence exhaustion. Lifecycle
commands never consume `MatchId`.

The status outbox has power-of-two capacity of at least two. Resume and open
may reserve only when at least two slots are available, leaving one safety slot
after the instrument becomes active. Operator halt, automatic
`LosslessOutboxFull` halt, and close may use the final slot because their
resulting state is non-active. Thus an active engine always retains capacity
for its mandatory automatic halt; no physical slot is permanently tied to a
particular command.

The producer writes the complete `SystemStatus` before changing state, but the
record remains invisible in its reservation. After successful preflight,
state transition, optional storage clear, sequence commit, and publication are
infallible invariants. Halt/resume/open publish `StateTransition`; close
publishes `Reset`. Reasons distinguish `TradingHalt`, `TradingResume`,
`EndOfDay`, `SessionOpen`, and the automatic `LosslessOutboxFull` halt.

## 7. External market-data path

Phase 3 reconstructs a separate venue's book from an outbound multicast feed.
It does not place or match local orders.

### 7.1 Packet and event flow

1. UDP ingress receives a datagram and validates its transport/application
   framing.
2. The packet decoder emits a bounded `PacketEnvelope` containing at least
   `ChannelId`, `SessionId`, `FirstMessageSequence`, `MessageCount`, and its
   validated message blocks.
3. The `ChannelRecoveryGate` is the sole owner of expected-sequence and
   recovery state for that channel.
4. Only when the gate releases a contiguous sequence does the market-data
   applier receive normalized `MarketDataEvent` values.
5. The applier updates the external market-data books and publishes their
   permitted outputs.

Packet decoding may occur on the ingress thread.  The recovery gate and
market-data applier MAY initially run on the same serial executor; split them
only after measurement demonstrates a benefit.  Regardless of threading, the
applier remains the only writer of each external book.

### 7.2 Channel sequencing and recovery

Each channel tracks message sequence, not merely packet count.  A packet can
contain multiple messages, and the header identifies the first message's
sequence.  The following messages are implicitly sequential.

```mermaid
stateDiagram-v2
  [*] --> InSync
  InSync --> IncrementalRecovery: "message gap"
  IncrementalRecovery --> InSync: "re-request and cache drain succeed"
  IncrementalRecovery --> SnapshotRecovery: "unsafe or unavailable recovery"
  InSync --> SnapshotRecovery: "session mismatch or malformed packet"
  SnapshotRecovery --> InSync: "snapshot and catch-up succeed"
  SnapshotRecovery --> Failed: "snapshot recovery fails"
```

For an ordinary recoverable gap, the gate retains current external book state,
marks every book assigned to that channel unavailable, caches later packets,
and requests the missing message range.  It applies neither live nor recovered
data out of sequence.  After it applies the missing range and drains the
contiguous cache, the channel returns to `InSync`.

Snapshot recovery is the catastrophic fallback.  It is required after session
mismatch, malformed data, invalid or unavailable re-request, timeout, cache
overflow, restart, or another condition that makes preserved state unsafe.  A
snapshot response must identify its session and last included message sequence
(watermark).  The gate clears all external books on that channel, applies the
snapshot, sets the expected sequence to the message after the watermark, and
then drains only later cached messages in order.

The simulator's short-gap profile uses UDP-unicast re-request.  A TCP service
may supply complete snapshots; it is not part of ordinary incremental replay.

## 8. Data contracts

All hot-path contracts are fixed-size or bounded, use integer prices and
quantities, and contain no dynamically allocated symbol strings.

| Contract          | Producer              | Consumer                            | Key contents                                                                     |
| ----------------- | --------------------- | ----------------------------------- | -------------------------------------------------------------------------------- |
| `OrderCommand`    | Local caller          | Local matching engine               | `OrderId`, `InstrumentId`, side, tick price, quantity, command kind.             |
| `ExecutionReport` | Local matching engine | Lossless execution outbox           | Match ID, aggressive/resting IDs, price, quantity, engine sequence.              |
| `SystemStatus`    | Book or recovery gate | Lossless status path                | Scope, state transition, reason, and sequence/watermark when applicable.         |
| `PacketEnvelope`  | UDP ingress/decoder   | Channel recovery gate               | Channel/session identity, first message sequence, count, bounded payload.        |
| `MarketDataEvent` | Recovery gate         | Market-data applier                 | External add/cancel/execute/snapshot/status event plus channel message sequence. |
| `MarketExecution` | Market-data applier   | Lossless consumer path when enabled | External fill information; never invents an aggressive local order ID.           |
| `BookUpdate`      | Book writer           | Analytics/UI path                   | Immutable best-price, depth, or delta information; eligible for conflation.      |

`EngineSequence` is assigned by the local matching engine.  A channel message
sequence belongs to the external feed and is never used as a substitute for an
engine sequence.

## 9. Egress and delivery guarantees

| Output class             | Delivery rule                       | Backpressure behavior                                                       |
| ------------------------ | ----------------------------------- | --------------------------------------------------------------------------- |
| Local `ExecutionReport`  | Lossless and ordered                | Reserve the complete batch before mutation; if unavailable, fail closed.    |
| `MarketExecution`        | Lossless and ordered when published | Use a bounded lossless path; do not silently discard.                       |
| `SystemStatus`           | Lossless and ordered                | Use reserved control capacity so a full fills queue cannot suppress a halt. |
| `BookUpdate` / analytics | Lossy or conflated                  | Keep the newest useful state; drop intermediate updates when full.          |

Each independently scheduled consumer needs its own queue or a dedicated
fan-out mechanism.  A single SPSC queue cannot safely serve multiple consumer
threads.  The writer never waits for a UI or analytics consumer.

Milestone 5 has no consumer thread or fan-out implementation. Tests and local
applications synchronously pop committed copies through the matching engine's
narrow consumption methods. Reservation objects and mutable slots never cross
the matching-engine boundary. Future consumers may use committed event values
and their global `EngineSequence`; they must not access private book storage or
make the matching core aware of sockets, persistence, logging, or consumer
behavior.

The exact persistence mechanism for the lossless outbox is deferred.  An
in-memory bounded ring protects the process lifetime; true crash durability
requires a later write-ahead log or equivalent durable store.

## 10. Threading and memory model

### 10.1 Phase 1

* One thread invokes the local matching engine.
* The engine is the sole reader and writer of its local book state.
* Tests call the public API directly; they require no socket, dispatcher, or
  scheduler. The matching engine's owned outboxes are passive in-memory
  storage, not inter-thread queues.

### 10.2 Phase 3 target

* A UDP ingress owner receives datagrams and hands off bounded packet envelopes
  to the relevant channel gate, either directly or through an SPSC queue.
* A channel gate is the sole writer of its recovery state and cache.
* A market-data applier is the sole writer of every assigned external book.
* Snapshot and re-request I/O may be asynchronous, but their results return to
  the channel gate for sequencing; I/O clients never mutate a book.
* Outbound queues use explicit ownership: one producer and one consumer for an
  SPSC ring, or a separately specified fan-out design.

CPU affinity can reduce core migration but does not eliminate interrupts or
context switches.  Add it only after profiling demonstrates that the chosen
thread layout benefits from it.

## 11. Failure and observability rules

* Public mutation paths do not use C++ exceptions.  They return explicit result
  codes and leave book data unchanged on ordinary rejection.
* Lossless output-capacity failure is a fail-closed state transition, not an
  accepted partial match.
* Ordinary lifecycle status saturation returns `StatusOutboxFull` after command
  acceptance but before state mutation; the protected headroom rule keeps the
  emergency halt publishable whenever the instrument is active.
* Malformed market-data packets are logged, never applied, and trigger channel
  snapshot recovery.
* Recovery, resets, lossless-outbox faults, and status changes emit bounded,
  structured diagnostics with channel/instrument IDs and relevant sequences.
* Debug builds expose invariant validation after every mutation.  Production
  hot paths must not rely on unbounded diagnostic logging.

## 12. Validation strategy

* Unit-test local matching independently of all networking.
* Compare randomized local command streams with a simple reference model.
* Test the packet decoder with binary fixtures, including multi-message and
  multi-instrument packets.
* Test channel recovery with deterministic packet loss, duplicates, stale
  packets, session changes, malformed data, timeout, cache overflow, and
  snapshot-watermark catch-up.
* Test lossless outbox reservation with multi-fill commands so no partial book
  mutation is possible when output capacity is insufficient.
* Run the Debug suite under ASan and UBSan.  Run ThreadSanitizer only after a
  concurrency milestone adds shared state or inter-thread queues.

## 13. Deferred decisions

The following decisions are intentionally deferred but require a dedicated
specification before their implementation:

1. Exact binary wire format, checksum algorithm, maximum packet size, and
   maximum message count (`docs/market-data-protocol.md`).
2. Snapshot payload format, transport request/response framing, and retention
   policy.
3. Lossless-outbox crash-persistence mechanism and capacity configuration.
4. Channel-to-instrument assignment, multi-book sharding, and consumer fan-out
   strategy at larger scale.
5. Benchmark workloads, latency targets, and the evidence required before
   replacing baseline storage with specialized ladders or allocators.
