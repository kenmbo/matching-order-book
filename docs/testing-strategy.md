# Testing Strategy

## Authority and scope

Project decisions use this authority order:

1. `docs/book-rules.md` defines normative matching and recovery behavior.
2. `docs/architecture.md` defines ownership and architectural boundaries.
3. `TODO.md` defines implementation order and acceptance gates.
4. `AGENTS.md` defines build, test, repository, and agent workflow.

A lower-ranked document must not silently reinterpret a higher-ranked one.
Conflicts that affect the current milestone must be resolved before
implementation. Deferred conflicts are recorded without expanding the current
milestone.

Milestone 0 validates only the build and test harness. The smoke test contains
no order-book behavior and is not evidence for later behavioral milestones.

## Phase 1 hardening and reference boundary

Milestone 7 adds a deliberately simple reference book under `tests/`. It uses
ordered maps of price levels and vectors for FIFO membership, scans for active
IDs, and applies commands by copying logical model state before a transaction.
It does not call `MatchingEngine`, `OrderBookStorage`, the production match
planner, or production mutation helpers to calculate an expected result. It
reuses only fixed-width domain contracts, public result/event types, and the
documented capacity constants.

The differential runner drains both outboxes after every ordinary command and
compares the returned result, accepted command sequence, synchronous reports,
lifecycle state, all sequence and match counters, active counts, level counts,
BBO, full depth, aggregates, active-order fields, per-level FIFO ordering, and
the exact committed execution and status batches. Separate property checks
verify aggressive and resting quantity conservation, resting-price execution,
and price-time report order. Both the model and the engine invariant hooks run
after every generated command.

The text trace format is one normalized command per line:

```text
N <order> <instrument> <B|S|I> <price> <quantity>
C <order> <instrument>
A <order> <instrument> <new-price> <new-leaves>
H|R|X|O <instrument>
```

`H`, `R`, `X`, and `O` mean halt, resume, close, and open. Zero represents a
reserved invalid domain sentinel in adversarial traces. Comment lines begin
with `#`. The permanent `tests/fixtures/phase1_replay.trace` fixture covers
matching, amendments, active-ID reuse, invalid inputs, halted cancellation,
close/open reset, and lifecycle rejection.

The default CTest case runs 2,500 commands for each fixed seed `0x5eed`,
`0xc0ffee`, `0x12345678`, and `0xd1ff3e`, then serializes, parses, and replays
each generated trace. The deterministic extended soak runs 25,000 commands per
seed (100,000 total):

```bash
./build/debug/tests/lob_phase1_reference_model_test --soak
```

One counterexample can be reproduced with explicit decimal seed and count, or
a saved trace can be replayed directly:

```bash
./build/debug/tests/lob_phase1_reference_model_test --seed 12648430 --commands 25000
./build/debug/tests/lob_phase1_reference_model_test --replay tests/fixtures/phase1_replay.trace
```

On a mismatch the runner prints the seed, command index, command, expected and
actual state, and the complete replayable prefix. A discovered counterexample
must be minimized and committed under `tests/fixtures/` before its production
fix is applied.

Focused hardening tests construct near-maximum counters through a private
friend fixture; no public sequence mutation API exists. They cover the last
assignable command, engine sequence, and match ID, contiguous-range shortage,
maximum price and quantity, aggregate overflow, exact 256-report fit, 255-slot
shortage, fail-closed publication, and lifecycle engine-sequence preflight.
The existing focused suites retain active-order/level/status capacity,
wraparound, abort/reuse, validation precedence, and reset coverage.

In Debug, the storage scan validates index/FIFO bijection, membership,
aggregates, non-empty ordered levels, counts, capacities, and BBO. The engine
adds uncrossed-active, closed-empty, status-headroom, lifecycle-state, and
execution/status outbox occupancy/reservation checks. Tests call the hook on
successes, rejections, and aborted operations. The expensive storage scan is
compiled to constant-time success under `NDEBUG`; Release retains only the
constant-time engine/outbox checks.

## Test layers

Tests are organized by the narrowest useful scope:

1. Unit tests exercise matching and storage without networking.
2. Reference-model and deterministic replay tests compare complete command
   results, events, and state.
3. Decoder tests operate on binary fixtures without sockets or book mutation.
4. Recovery tests inject deterministic gaps, duplicates, corruption, timeout,
   and cache exhaustion independently of live sockets.
5. Runtime integration tests cover bounded handoffs only after networking and
   concurrency milestones begin.

Every behavior change adds focused automated coverage. Debug tests validate
invariants after each mutation once book mutation exists. Seeds, minimized
traces, malformed inputs, and expected outcomes are retained as fixtures.

## Build and test entry points

The checked-in presets keep ordinary and sanitizer builds separate:

```bash
cmake --preset debug
cmake --build --preset debug
ctest --preset debug

cmake --preset asan
cmake --build --preset asan
ctest --preset asan

cmake --preset ubsan
cmake --build --preset ubsan
ctest --preset ubsan
```

The ASan and UBSan presets are independent Debug configurations. A future TSan
preset must also remain separate and is added only when a concurrency
milestone requires it. Sanitizer failures are test failures and must not be
suppressed or hidden.

All production and test targets compile as C++20 without compiler extensions.
GCC project targets use `-Wall -Wextra -Werror`.

## Completion evidence

Run the narrowest relevant test during development, followed by the full Debug
suite and applicable sanitizer suites. A handoff records every configure,
build, and test command, CTest totals, sanitizer findings, deterministic seeds
used, and any remaining limitation. A configuration is reported as passing
only when its command completed successfully.
