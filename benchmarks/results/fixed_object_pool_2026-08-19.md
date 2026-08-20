# Standalone Fixed-Object-Pool Baseline

Accepted: yes

Boundary: standalone pool acquire/release only; this is not a MatchingEngine::process() measurement.

## Method

The production-capacity pool and all benchmark buffers are constructed before timing. Full cycles acquire every slot and then release every slot. Churn keeps half the capacity live and uses a pre-generated deterministic position sequence. No RNG, formatting, logging, serialization, or vector growth occurs in a timed interval. Checksums consume handles and node values.

## Environment

- Git: `3f75751dacb2a15beb971b8a716a08491c6cfa6c` (dirty: yes)
- CPU: Intel(R) Xeon(R) CPU E3-1230 V2 @ 3.30GHz
- Microcode: 0x21
- Kernel: Linux 6.1.0-52-amd64 x86_64
- Compiler: 12.2.0
- Flags: `-O3 -march=native -ffast-math -Wall -Wextra -Werror -std=c++20 -DNDEBUG`
- Affinity/core: 1
- Thread siblings: 1,5
- SMT sibling occupancy: launcher_present_not_continuously_monitored
- Governor/frequency: schedutil / 3589120 kHz
- NUMA node: 0
- Clock call-pair overhead: 16 ns (not subtracted)

## Storage and workload

- Capacity: 131072
- Object size/alignment: 32 / 8 bytes
- Slot size/alignment: 48 / 8 bytes
- Backing arrays: 6815744 bytes
- Full cycles per repetition: 8
- Churn operations per repetition: 1000000
- Churn live set: 65536
- Warm-up cycles: 2
- Repetitions: 5

## Median-of-repetition results

No speculative latency threshold applies. Allocation validity is the hard benchmark gate.

| Metric | Samples | p50 ns | p90 ns | p99 ns | p99.9 ns | p99.99 ns | Max ns | Throughput/s |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| full acquire | 1048576 | 23 | 26 | 33 | 81 | 320 | 17336 | 42075993 |
| full release | 1048576 | 21 | 24 | 29 | 89 | 318 | 18564 | 46636523 |
| full combined cycle | 8 | 5878495 | 6044210 | 6044210 | 6044210 | 6044210 | 6044210 | 168 |
| churn acquire | 1000000 | 30 | 31 | 41 | 99 | 354 | 19611 | 32733610 |
| churn release | 1000000 | 21 | 25 | 31 | 93 | 337 | 19779 | 45038827 |
| churn combined pair | 1000000 | 69 | 70 | 104 | 210 | 8570 | 22701 | 14127508 |

- Full-cycle component operations/s: 44083288
- Churn component operations/s: 28255016

Full-cycle and churn-combined throughput in the table is cycles/s and pairs/s respectively; acquire/release rows are operations/s. The machine-readable artifact contains the complete timing totals needed to derive component-operation throughput.

## Allocation audit

- Audit attached: yes
- Construction phase: 15 allocations, 34079080 bytes, 0 deallocations
- Timed pool operations: 0 allocations, 0 bytes, 0 deallocations
- Timed sample/checksum collection: 0 allocations, 0 bytes, 0 deallocations
- Timed allocation gate: pass

The audit executable is separate from the canonical latency executable. Startup pool backing allocation is permitted and reported independently.
