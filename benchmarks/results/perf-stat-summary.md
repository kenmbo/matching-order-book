# perf benchmarking
Linux's perf was used to benchmark the performance of the repository during state `ac0aa0d5a62cfda7ba0b4ea04f794113d41a82b6` (docs(perf): add \perf-stat-summary\) on August 28.


## perf and environment

- `perf version 6.1.180`
- `kernel.perf_event_paranoid = 1`
- CPU: Intel Xeon E3-1230 V2
- Kernel: Debian `6.1.0-52`
- Compiler: GCC 12.2.0
- Selected CPU: 1
- SMT sibling: CPU 5
- Policy: `sibling_cpu5_observed_idle_not_exclusively_reserved`
- Governor: `schedutil`
- Driver/frequency policy: `intel_cpufreq`, 1.6–3.7 GHz
- NUMA node: 0
- Clock source: TSC

The four core events scheduled together at 100%. The four cache events could not schedule as one group, so they were split into generic-cache and L1D pairs; every recorded event still had 100% enabled time with no multiplexing loss. Every `perf record` reported zero lost samples.

## perf commands

For each phase and workload, `perf stat` used:

```bash
taskset -c 1 perf stat --no-big-num -x ';' \
  -e '{cycles,instructions,branches,branch-misses}' \
  -e '{task-clock,context-switches,cpu-migrations,page-faults}' \
  -- <benchmark> \
  --mode exploratory --workload <workload> --seeds <seed> \
  --samples 200000 --warmup 10000 --repetitions 5 \
  --cpu 1 \
  --sibling-occupancy sibling_cpu5_observed_idle_not_exclusively_reserved
```

Separate otherwise-identical passes used:

```bash
-e '{cache-references,cache-misses}'
-e '{L1-dcache-loads,L1-dcache-load-misses}'
```

Longer sampling profiles used:

```bash
taskset -c 1 perf record -q -e cycles:u -c 250000 \
  -o <output.data> -- <benchmark> \
  --mode exploratory --workload <workload> --seeds <seed> \
  --samples 200000 --warmup 10000 --repetitions 10 \
  --cpu 1 \
  --sibling-occupancy sibling_cpu5_observed_idle_not_exclusively_reserved

perf report -i <output.data> --stdio --no-children \
  --percent-limit .10 --sort symbol

perf annotate -i <output.data> --stdio --symbol '<symbol>'
```

Seeds were `24301` except for `unknown_cancel` and `increase`, which used `12648430`. `reduce` with seed `24301` was the passing control.

## Counter results

The complete raw table is in [perf-stat-summary.csv](../../perf-stat-summary.csv).

All deltas are Phase 2 versus Phase 1.

i

| Workload | Cycles | IPC P1→P2 | Branch-miss rate P1→P2 | Task clock | Context switches | Migrations |
|---|---:|---:|---:|---:|---:|---:|
| mixed | +5.4% | 0.290→0.320 | 2.90%→2.58% | +4.8% | 36→38 | 0→0 |
| cancel | +1.7% | 0.319→0.299 | 0.49%→0.56% | +1.6% | 46→49 | 0→0 |
| noop | +5.9% | 0.234→0.265 | 1.20%→1.17% | +5.9% | 38→45 | 0→0 |
| fill16 | −5.0% | 0.626→0.663 | 0.34%→0.16% | −5.0% | 105→111 | 0→0 |
| unknown_cancel | +5.2% | 0.219→0.247 | 2.39%→1.25% | +5.4% | 39→40 | 0→0 |
| increase | +4.6% | 0.215→0.261 | 0.91%→0.84% | +4.9% | 46→46 | 0→0 |
| reduce, control | +3.9% | 0.214→0.264 | 0.98%→0.78% | +3.8% | 51→46 | 0→0 |

| Workload | Generic-cache miss rate P1→P2 | Cache misses Δ | L1D miss rate P1→P2 | L1D misses Δ | Page faults P1→P2 |
|---|---:|---:|---:|---:|---:|
| mixed | 3.14%→10.33% | +562.6% | 18.69%→15.78% | −15.5% | 3,274→9,724 |
| cancel | 0.12%→0.92% | +706.3% | 57.50%→60.35% | −0.5% | 3,283→10,152 |
| noop | 1.78%→14.05% | +792.1% | 6.25%→5.51% | −10.6% | 2,764→10,148 |
| fill16 | 0.21%→1.22% | +798.5% | 16.71%→17.31% | −2.0% | 3,281→8,614 |
| unknown_cancel | 2.46%→17.34% | +795.5% | 4.89%→5.06% | +4.4% | 3,285→8,609 |
| increase | 0.20%→1.56% | +730.7% | 53.78%→53.10% | +1.7% | 2,764→8,616 |
| reduce, control | 0.20%→1.48% | +740.4% | 53.40%→53.58% | +3.4% | 2,772→10,660 |

The cache and page-fault totals are whole-executable measurements, not isolated `process()` counters. They include storage construction, first-touch initialization, precondition restoration, sample processing, and destruction. They therefore demonstrate the larger Phase 2 process footprint but cannot be attributed directly to timed calls.

## Hot functions and instructions

The `perf record` was dominated by work outside the timed process boundary:

- Phase 1 `memset`: 50–68%; `memmove`: 4–30%.
- Phase 2 `memset`: 48–69%; `memmove`: 5–30%.
- Percentile sorting accounted for approximately 1–3%.
- Phase 2 storage constructors were visible at roughly 1–2.5%.

Relevant hot symbols after separating that overhead:

| Workload | Phase 1 | Phase 2 |
|---|---|---|
| mixed | `find_order` 3.86%, update 0.77%, level lookup 0.75% | `find_order` 4.12%, capacity preflight 0.98%, update 0.88%, unlink 0.67% |
| cancel | `find_order` 1.22%, remove 0.38% | `find_order` 1.17%, remove 0.82%; index erase also sampled |
| noop | `find_order` 1.77% | `find_order` 1.93% |
| fill16 | `find_order` 3.83%, insert 2.65%, remove 1.17% | insert 3.13%, `find_order` 2.64%, plan 2.59%, index insert 2.22% |
| unknown_cancel | process 0.40% | `find_order` 0.55%, process 0.33% |
| increase | `find_order` 3.16%, update 1.51%, FIFO move 1.21% | `find_order` 2.25%, update 1.10%, FIFO move 0.42% |
| reduce control | `find_order` 2.95%, update 1.11% | `find_order` 2.42%, update 1.27% |

## Measurement limitations

- The `perf stat` and sampling covered the whole executable, while the benchmark times only `process()` calls.
