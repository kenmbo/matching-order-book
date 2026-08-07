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
