# Benchmark Methodology

## Purpose and timing boundaries

Benchmarks provide reproducible, machine-specific evidence. They never change
matching semantics, replace correctness tests, or justify a specialized data
structure without a measured benefit. Milestone 0 provides only a compileable
benchmark target; performance measurement starts in its scheduled milestone.

The measured boundaries are:

* **Matching-core latency:** normalized command entry into `process()` through
  completed book mutation and preparation of every output slot.
* **Local process-completion latency:** normalized command entry into the
  public `MatchingEngine::process()` overload through its return. This is the
  canonical Milestone 8 local boundary.
* **Local command-to-outbox latency:** normalized local command entry through
  publication of the outbox producer cursor.
* **External per-message latency:** decoder start for a packet already in
  userspace through `parse -> recovery gate -> external-book apply ->
  downstream publish`, divided by the packet's message count.
* **External packet completion latency:** decoder start through publication of
  the packet's final message.
* **Recovery convergence:** gap detection through restoration of a contiguous,
  current channel; this is reported separately from normal-path latency.
* **Snapshot rebuild throughput:** reconstructed orders or messages per second,
  reported separately from normal-path latency.

Socket receive wait, downstream consumer work, logging, random generation, and
trace construction are excluded from userspace processing measurements.
Kernel-inclusive measurement, if later added, is named
`socket ingress -> publish` and reported separately.

For a command that emits execution reports, public `process()` returns only
after the complete reserved batch and producer cursor are committed, so local
process-completion also observes command-to-outbox publication. Zero-event
commands do not advance an execution-outbox cursor and are reported only as
process-completion latency. Milestone 8 does not add a private matching-core
seam: `matching_core_measured` is false. A full public-path pass against the
tighter matching-core band is conservative upper-bound evidence only; a miss
is inconclusive about the unobserved internal boundary.

## Build and environment controls

Use the `release` preset and compile the benchmark target with
`-O3 -march=native -ffast-math`. ASan, UBSan, debug assertions, tracing, and
synchronous logging are disabled. Floating-point distribution generation is
completed before timing; matching prices, quantities, aggregates, and
comparisons remain integers.

Pin the benchmark thread to one physical core and keep its SMT sibling idle
when possible. Warm code, book state, pools, and benchmark data before
measurement. Pre-generate commands, packets, distributions, and expected
branch paths. Result collection is bounded and preallocated.

Milestone 8 uses a separate allocation-audit execution of each deterministic
trace so allocator interposition cannot distort the canonical latency run.
Bounded non-allocating counters distinguish engine/outbox and benchmark-buffer
construction, initial book population, trace generation, warm-up, timed public
`process()` calls, timed-loop sample/checksum collection, and post-run
statistics/serialization. Sorting and serialization occur after measurement.
The audit reports total timed `process()` activity rather than claiming dynamic
attribution that would change code generation. A source audit identifies the
possible Phase 1 production sites as `std::map::try_emplace` price-level nodes,
`std::list::push_back` FIFO nodes, and `std::unordered_map::emplace` active-ID
nodes, plus their corresponding erasure deallocations. Trace, sample,
collection, checksum, metric, and output code owns no allocation in the timed
loop.

The Release benchmark target is built with:

```bash
cmake --preset release
cmake --build --preset release --target benchmarks
```

## Workloads

The local mixed workload contains 70% cancels and amendments, 20%
non-crossing limit orders, and 10% crossing orders. Explicitly place 80% of
volume within the top five price levels; sample remaining prices from a
bounded normal distribution around BBO. Maintain 5,000–10,000 resting orders.
Measure valid and unknown-ID negative lookup paths separately. Generate the
entire RNG-driven trace before timing.

Measure cancel, same-price reduction, FIFO-requeue increase, non-crossing add,
one-fill cross, four-fill cross, and multi-level sweep independently as well as
the mixed workload. Report latency by fill count and emitted-event count.
External-path runs report one-message packets and representative multi-message
packets separately so batching cannot hide single-packet latency.

## Samples, repetitions, and statistics

After warm-up, run at least five measured repetitions with several million
samples so p99.9 is meaningful. Use one fixed documented acceptance seed and
at least one additional fixed seed. Compute per-repetition percentiles and
throughput, and use the median of five results for the primary comparison.
At least four of five repetitions must satisfy each absolute hard gate.

Report p50, p90, p99, p99.9, p99.99, maximum, and throughput. Record maximum
latency but do not gate on it because Linux interrupts and system-management
activity can dominate isolated outliers.

The first accepted implementation must pass its applicable absolute initial
band. Once a machine-specific baseline exists, later acceptance runs must also
pass these relative non-regression limits:

* median p99 regression no greater than 10%;
* median p99.9 regression no greater than 15%;
* sustained-throughput regression no greater than 10%.

Do not compare absolute nanosecond results across CPU models as one machine
baseline. Replace a baseline only with a documented reason, complete metadata,
and a fresh acceptance run.

## Acceptance bands

The local matching-core initial band is p50 <= 500 ns, p99 <= 2 us,
p99.9 <= 6 us, and throughput >= 1 million commands/s. The later goal is
p50 <= 250 ns, p99 <= 1 us, p99.9 <= 3 us, and throughput >= 2 million
commands/s. During Milestone 8 this band is informational because its exact
endpoint is not independently observable. A public process-completion pass may
be recorded as conservative evidence but is not an independently measured
matching-core baseline.

Multi-fill matching uses this p99 ceiling:

```text
1.0 us + (0.20 us * fills) + (0.10 us * emitted events)
```

For one emitted event per fill, the ceilings for 1, 4, 16, 64, and 256 fills
are 1.3 us, 2.2 us, 5.8 us, 20.2 us, and 77.8 us respectively. Use the actual
event count when a fill emits more than one event.
During Milestone 8 these matching-core ceilings are informational. A public
process-completion pass is conservative evidence; a public-path miss is
inconclusive about the unobserved internal boundary and is not a Milestone 8
failure.

The Milestone 8 local process-completion initial band is p50 <= 1.5 us, p99 <=
5 us, p99.9 <= 15 us, and throughput >= 500,000 commands/s. Its later goal is
p50 <= 750 ns, p99 <= 3 us, p99.9 <= 8 us, and throughput >= 1 million
commands/s. For report-producing commands this is also the observable local
command-to-outbox band.

The contiguous external normal-path initial band is amortized p50 <= 1.5 us,
p99 <= 5 us, p99.9 <= 15 us, and throughput >= 500,000 messages/s. Its later
goal is p50 <= 500 ns, p99 <= 2 us, p99.9 <= 5 us, and throughput >= 1 million
messages/s. Recovery and snapshot measurements never contaminate these
normal-path percentiles.

## Required metadata and validity checks

Record the Git commit; CPU model and microcode; compiler and version; complete
compile and link flags; kernel version; core ID and affinity mask; SMT state
and sibling occupancy; CPU frequency governor and observed or effective
frequency; NUMA node when relevant; book depth and level occupancy; operation
distribution; packet message-count distribution; workload seed; and warm-up,
sample, and repetition counts.

Before accepting a run:

* verify the generated 70/20/10 operation mix;
* verify that 80% of volume is within the top five levels;
* keep unknown-order lookups separate from successful cancels and amendments;
* report fill-count and emitted-event-count distributions;
* report packet sizes and messages per packet;
* confirm output publication is included and downstream consumption excluded;
* confirm zero benchmark-owned heap allocations in the timed loop;
* for the Milestone 8 `phase1_allocating_storage` profile, measure and disclose
  total allocations, allocated bytes, and deallocations inside public
  `process()` calls;
* beginning with Milestone 10, confirm zero allocation and deallocation
  anywhere in the timed command-processing path.

Run validity, allocation-policy compliance, and performance-gate compliance
are separate outcomes. Reject any run with missing metadata, incorrect
workload composition, hot RNG, benchmark-owned timed allocation, or unplanned
consumer work. Phase 1 storage allocation is diagnostic and does not invalidate
Milestone 8. Starting with Milestone 10, any timed allocation or deallocation
invalidates the run. Store machine-readable results with a human-readable
baseline report, and never overwrite the Phase 1 allocating baseline with the
strict-zero Milestone 10 baseline.
