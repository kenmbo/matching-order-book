#!/usr/bin/env python3

import csv
import json
import re
from pathlib import Path


ROOT = Path(__file__).parent
PATTERN = re.compile(
    r"(?P<ordinal>\d+)-(?P<phase>baseline|candidate)-"
    r"(?P<workload>[a-z0-9_]+)-(?P<seed>\d+)\.json"
)
WORKLOADS = [
    "mixed",
    "unknown_cancel",
    "noop",
    "cancel",
    "increase",
    "fill16",
    "reduce",
]
SEEDS = [24301, 12648430]
LATENCY_METRICS = ["p50_ns", "p90_ns", "p99_ns", "p999_ns", "p9999_ns"]
METRICS = LATENCY_METRICS + ["throughput_per_second"]


def delta(before, after):
    return after - before, ((after / before) - 1.0) * 100.0


runs = {}
for path in ROOT.glob("*.json"):
    match = PATTERN.fullmatch(path.name)
    if not match:
        continue
    artifact = json.loads(path.read_text())
    workload = match.group("workload")
    seed = int(match.group("seed"))
    phase = match.group("phase")
    assert artifact["mode"] == "exploratory"
    assert artifact["run_validity_passed"] is True
    assert artifact["canonical_configuration_valid"] is False
    assert len(artifact["workloads"]) == 1
    result = artifact["workloads"][0]
    assert result["name"] == workload and result["seed"] == seed
    assert result["samples"] == 1_000_000
    assert result["warmup"] == 10_000
    assert result["repetitions"] == 5
    assert [entry["index"] for entry in result["repetition_results"]] == list(
        range(5)
    )
    assert all(
        entry["starting_cpu"] == 1 and entry["ending_cpu"] == 1
        for entry in result["repetition_results"]
    )
    key = (workload, seed, phase)
    assert key not in runs
    runs[key] = result

assert len(runs) == len(WORKLOADS) * len(SEEDS) * 2

rows = []
summary = []
for workload in WORKLOADS:
    for seed in SEEDS:
        baseline = runs[(workload, seed, "baseline")]
        candidate = runs[(workload, seed, "candidate")]
        assert baseline["trace_checksum"] == candidate["trace_checksum"]
        for repetition in range(5):
            before = baseline["repetition_results"][repetition]["statistics"]
            after = candidate["repetition_results"][repetition]["statistics"]
            row = {
                "workload": workload,
                "seed": seed,
                "result": f"rep{repetition}",
            }
            for metric in METRICS:
                absolute, relative = delta(before[metric], after[metric])
                row[f"baseline_{metric}"] = before[metric]
                row[f"candidate_{metric}"] = after[metric]
                row[f"absolute_{metric}"] = absolute
                row[f"relative_{metric}_pct"] = relative
            rows.append(row)

        before = baseline["median"]
        after = candidate["median"]
        row = {"workload": workload, "seed": seed, "result": "median"}
        for metric in METRICS:
            absolute, relative = delta(before[metric], after[metric])
            row[f"baseline_{metric}"] = before[metric]
            row[f"candidate_{metric}"] = after[metric]
            row[f"absolute_{metric}"] = absolute
            row[f"relative_{metric}_pct"] = relative
        p99_regression = row["relative_p99_ns_pct"]
        p999_regression = row["relative_p999_ns_pct"]
        throughput_regression = -row["relative_throughput_per_second_pct"]
        row["relative_gates_passed"] = (
            p99_regression <= 10.0
            and p999_regression <= 15.0
            and throughput_regression <= 10.0
        )
        rows.append(row)
        summary.append(row)

fieldnames = list(rows[-1].keys())
with (ROOT / "comparison.csv").open("w", newline="") as output:
    writer = csv.DictWriter(output, fieldnames=fieldnames, restval="")
    writer.writeheader()
    writer.writerows(rows)

(ROOT / "summary.json").write_text(json.dumps(summary, indent=2) + "\n")

lines = [
    "| workload | seed | p50 ns B→C | Δ% | p90 ns B→C | Δ% | "
    "p99 ns B→C | Δ% | p99.9 ns B→C | Δ% | p99.99 ns B→C | Δ% | "
    "throughput/s B→C | Δ% | gates |",
    "|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---|",
]
for row in summary:
    lines.append(
        f"| {row['workload']} | {row['seed']} | "
        f"{row['baseline_p50_ns']:.0f}→{row['candidate_p50_ns']:.0f} | "
        f"{row['relative_p50_ns_pct']:+.2f}% | "
        f"{row['baseline_p90_ns']:.0f}→{row['candidate_p90_ns']:.0f} | "
        f"{row['relative_p90_ns_pct']:+.2f}% | "
        f"{row['baseline_p99_ns']:.0f}→{row['candidate_p99_ns']:.0f} | "
        f"{row['relative_p99_ns_pct']:+.2f}% | "
        f"{row['baseline_p999_ns']:.0f}→{row['candidate_p999_ns']:.0f} | "
        f"{row['relative_p999_ns_pct']:+.2f}% | "
        f"{row['baseline_p9999_ns']:.0f}→{row['candidate_p9999_ns']:.0f} | "
        f"{row['relative_p9999_ns_pct']:+.2f}% | "
        f"{row['baseline_throughput_per_second']:.0f}→"
        f"{row['candidate_throughput_per_second']:.0f} | "
        f"{row['relative_throughput_per_second_pct']:+.2f}% | "
        f"{'PASS' if row['relative_gates_passed'] else 'FAIL'} |"
    )
(ROOT / "median-comparison.md").write_text("\n".join(lines) + "\n")
