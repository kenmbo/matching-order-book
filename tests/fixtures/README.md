# Test fixtures

Deterministic command traces, malformed packet samples, and minimized
regressions belong here. Fixture files must document their format, source or
generation seed, and expected result.

`phase1_replay.trace` is the Milestone 7 hand-written baseline trace. Its
format is documented in `docs/testing-strategy.md`. It has no RNG seed; its
expected result is exact agreement between the independent Phase 1 reference
model and `MatchingEngine`, including state, sequences, reports, statuses, and
outbox contents after every line. The reference-model test replays it twice to
prove determinism.

If a seeded differential run finds a defect, minimize the printed replay
prefix, add the minimized trace here, and keep it as a permanent regression
fixture with the original seed and command index in a leading comment.
