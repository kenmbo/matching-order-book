# Domain Contracts

## Authority and scope

This document records Milestone 1 representation and sequencing decisions. It
does not define matching, book storage, wire encoding, or publication. If it
conflicts with `docs/book-rules.md`, the book rules prevail.

## Fixed-width values

All values are strong wrappers around fixed-width integers. Unrelated wrappers
do not convert to one another. The default value is the reserved zero sentinel;
it supports explicit empty or unassigned results but is never a valid domain
identity, price, quantity, or assigned sequence.

| Type | Representation | Valid values | Zero policy |
| --- | --- | --- | --- |
| `OrderId` | `std::uint64_t` | 1 through `UINT64_MAX` | Invalid/unassigned; never a live order ID. |
| `InstrumentId` | `std::uint32_t` | 1 through `UINT32_MAX` | Invalid/unassigned. |
| `MatchId` | `std::uint64_t` | 1 through `UINT64_MAX` | Invalid/unassigned. |
| `CommandSequence` | `std::uint64_t` | 1 through `UINT64_MAX` | No accepted command assigned yet. |
| `EngineSequence` | `std::uint64_t` | 1 through `UINT64_MAX` | No committed event assigned yet. |
| `PriceTicks` | `std::int64_t` | 1 through `INT64_MAX` | Invalid price; negative ticks are also invalid. |
| `Quantity` | `std::uint64_t` | 1 through `UINT64_MAX` | Invalid quantity or no remaining active value. |

These are in-memory domain contracts, not wire layouts. Binary encoding, byte
order, packing, alignment, and protocol widths remain deferred to the protocol
milestone.

## Commands, results, and lossless events

`Side`, `CommandKind`, `InstrumentState`, `OrderBookResult`, `StatusScope`,
`StatusEventKind`, and `StatusReason` use explicit `std::uint8_t` storage. Zero
is the invalid enum value except for `OrderBookResult::Accepted`; every
normative result in `docs/book-rules.md` has a distinct representation.

`NewOrder` contains order and instrument IDs, side, limit price, and quantity.
`CancelOrder` and `AmendOrder` also carry instrument identity so a future
public entry point can validate routing before looking up an order. These are
fixed-size values and contain no storage handles or book references.

`HaltInstrument`, `ResumeInstrument`, `CloseInstrument`, and `OpenInstrument`
are the four normalized local lifecycle commands. Each contains only its
`InstrumentId`. `CommandKind` assigns them the distinct values `Halt`,
`Resume`, `Close`, and `Open` after the existing order-entry kinds.

`ExecutionReport` contains the match and instrument IDs, aggressive and
resting order IDs, match price and quantity, and its committed engine sequence.
`SystemStatus` remains a separate event and contains scope, optional instrument
identity, prior and resulting local instrument states, event kind, reason, and
committed engine sequence. Channel sequences and snapshot watermarks are not
part of this local Milestone 1 contract; their representation remains deferred
until the external protocol and recovery milestones.

## Conversion and arithmetic policy

External or wider integers enter a domain wrapper only through
`checked_domain_cast`. The conversion distinguishes success, the reserved zero
value, and an out-of-range value. It does not truncate, wrap, allocate, or
throw. Direct construction from an integer is intentionally unavailable.

Signed overflow is never relied upon. Quantity addition checks available range
before adding. Quantity subtraction distinguishes a positive result, exact
depletion to zero, and underflow. Invalid operands are rejected. Later mutation
code must complete these checks before changing state.

IDs and sequences never silently wrap. Exhaustion at the maximum value is a
deterministic result and leaves the prior value unchanged.

## Command acceptance and sequencing

A command becomes accepted after its shape, instrument identity and state,
identifier rules, side, price, and quantity pass non-mutating validation. A
rejection before that boundary consumes neither sequence.

Crossing the acceptance boundary consumes exactly one `CommandSequence`, even
if later planning or output reservation fails. `EngineSequence` values are
assigned only by committing a complete event batch:

* an accepted command with zero committed events consumes no engine sequence;
* one committed event consumes one engine sequence;
* a multi-event commit receives one contiguous inclusive sequence range;
* an aborted transaction or failed reservation consumes no engine sequence;
* a failed batch followed by a successful batch creates no sequence gap.

If lossless execution-report reservation fails, those rules apply to the
rejected order transaction. The separate fail-closed control transaction
commits one `SystemStatus` and therefore consumes one engine sequence of its
own.

Lifecycle commands validate the instrument and transition table before the
acceptance boundary. `InvalidInstrument` and `InvalidStateTransition` consume
no sequence. A valid lifecycle command then consumes one `CommandSequence`
before status-capacity and engine-sequence preflight. `StatusOutboxFull` or
engine-sequence `CapacityExhausted` after that point retains the command
sequence but consumes no engine sequence and publishes nothing. Every
successful lifecycle control transaction consumes exactly one engine sequence
and no match ID.

`SystemStatus` uses `TradingHalt`, `TradingResume`, `EndOfDay`,
`LosslessOutboxFull`, and `SessionOpen` to distinguish operator transitions,
the automatic safety halt, destructive close, and new-session open. Halt,
resume, automatic halt, and open use `StateTransition`; close uses `Reset`.

`SequenceState::commit_event_batch` checks the entire range before advancing.
`abort_event_batch` is explicit and does not alter engine sequence state. There
is no allocate-then-rollback API.

Sequence zero represents the state before any assignment. Once the maximum
command or engine sequence is assigned, a further non-empty allocation reports
`Exhausted` without changing state. A zero-event commit remains a successful
no-op even when the engine sequence is exhausted.

## Public exception policy

Future public matching and mutation entry points are intended to be `noexcept`
when their full dependency chain permits it. Ordinary invalid input, unknown
IDs, capacity exhaustion, range exhaustion, and output reservation failure use
explicit results. Matching and mutation code must not use `throw`, `try`, or
`catch`. The domain conversions, arithmetic helpers, and sequencing operations
defined in Milestone 1 are `noexcept` and require no dynamic allocation.
