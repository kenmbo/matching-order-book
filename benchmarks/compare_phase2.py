#!/usr/bin/env python3
"""Attach Milestone 10 before/after evidence to canonical benchmark artifacts."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import sys


LATENCY_FIELDS = ("p50_ns", "p90_ns", "p99_ns", "p999_ns", "p9999_ns", "maximum_ns")


def percentage(delta: float, baseline: float) -> float | None:
    return None if baseline == 0 else delta * 100.0 / baseline


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--baseline", required=True, type=Path)
    parser.add_argument("--candidate", required=True, type=Path)
    parser.add_argument("--report", required=True, type=Path)
    args = parser.parse_args()

    baseline = json.loads(args.baseline.read_text(encoding="utf-8"))
    candidate = json.loads(args.candidate.read_text(encoding="utf-8"))
    if baseline.get("baseline_profile") != "phase1_allocating_storage":
        raise SystemExit("comparison input is not the retained Phase 1 baseline")
    if candidate.get("baseline_profile") != "phase2_pool_backed_storage":
        raise SystemExit("candidate is not the Milestone 10 pool-backed profile")

    old = {(item["name"], item["seed"]): item for item in baseline["workloads"]}
    comparisons: list[dict[str, object]] = []
    baseline_timed = {"allocations": 0, "allocated_bytes": 0, "deallocations": 0}
    candidate_timed = {"allocations": 0, "allocated_bytes": 0, "deallocations": 0}
    trace_match = True
    relative_pass = True
    markdown = [
        "\n## Retained Phase 1 comparison\n",
        "Baseline: `phase1_allocating_storage_2026-08-17`\n",
        "| Workload | Seed | Public path | Multi-fill | Allocation | Validity |",
        "| --- | ---: | --- | --- | --- | --- |",
    ]
    metric_rows = [
        "",
        "| Workload | Seed | Metric | Before | After | Absolute | Percent | Gate |",
        "| --- | ---: | --- | ---: | ---: | ---: | ---: | --- |",
    ]
    for current in candidate["workloads"]:
        key = (current["name"], current["seed"])
        prior = old.get(key)
        if prior is None:
            raise SystemExit(f"baseline is missing workload/seed {key}")
        checksums_equal = current["trace_checksum"] == prior["trace_checksum"]
        trace_match = trace_match and checksums_equal
        for repetition in prior["repetition_results"]:
            for field in baseline_timed:
                baseline_timed[field] += repetition["timed_allocations"][field]
        for repetition in current["repetition_results"]:
            for field in candidate_timed:
                candidate_timed[field] += repetition["timed_allocations"][field]
        metrics: dict[str, object] = {}
        for field in (*LATENCY_FIELDS, "throughput_per_second"):
            before = prior["median"][field]
            after = current["median"][field]
            delta = after - before
            change = percentage(delta, before)
            gate = "informational"
            passed = True
            if field == "p99_ns":
                passed = after <= before * 1.10
                gate = "pass" if passed else "fail"
            elif field == "p999_ns":
                passed = after <= before * 1.15
                gate = "pass" if passed else "fail"
            elif field == "throughput_per_second":
                passed = after >= before * 0.90
                gate = "pass" if passed else "fail"
            relative_pass = relative_pass and passed
            metrics[field] = {
                "before": before,
                "after": after,
                "absolute_difference": delta,
                "percentage_difference": change,
                "relative_gate": gate,
            }
            percent_text = "n/a" if change is None else f"{change:.3f}%"
            metric_rows.append(
                f"| {key[0]} | {key[1]} | {field} | {before:.3f} | "
                f"{after:.3f} | {delta:.3f} | {percent_text} | {gate} |"
            )
        comparison = {
                "workload": key[0],
                "seed": key[1],
                "trace_checksum_match": checksums_equal,
                "public_path_gate_status": (
                    "informational"
                    if not current["performance_gate_applicable"]
                    else "pass" if current["performance_gate_compliant"] else "fail"
                ),
                "multi_fill_status": (
                    "not_applicable"
                    if not current["nominal_fills"]
                    else "pass"
                    if current["median"]["p99_ns"]
                    <= 1_000 + 300 * current["nominal_fills"]
                    else "miss"
                ),
                "allocation_policy_status": (
                    "pass" if current["allocation_policy_compliant"] else "fail"
                ),
                "run_validity_status": "pass" if current["workload_valid"] else "fail",
                "metrics": metrics,
            }
        comparisons.append(comparison)
        markdown.append(
            f"| {key[0]} | {key[1]} | {comparison['public_path_gate_status']} | "
            f"{comparison['multi_fill_status']} | "
            f"{comparison['allocation_policy_status']} | "
            f"{comparison['run_validity_status']} |"
        )

    accepted = bool(
        candidate["run_validity_passed"]
        and candidate["allocation_policy_compliant"]
        and candidate["performance_gate_compliant"]
        and trace_match
        and relative_pass
    )
    candidate["baseline_comparison"] = {
        "baseline_profile": "phase1_allocating_storage_2026-08-17",
        "trace_checksums_match": trace_match,
        "relative_rules": {
            "p99_max_regression_percent": 10,
            "p999_max_regression_percent": 15,
            "throughput_max_regression_percent": 10,
        },
        "relative_non_regression_compliant": relative_pass,
        "phase1_timed_process_totals": baseline_timed,
        "phase2_timed_process_totals": candidate_timed,
        "comparisons": comparisons,
    }
    candidate["relative_non_regression_compliant"] = relative_pass
    candidate["accepted_canonical_run"] = accepted
    args.candidate.write_text(json.dumps(candidate, indent=2) + "\n", encoding="utf-8")

    markdown.extend(metric_rows)
    markdown.extend(
        [
            "",
            f"Trace checksums match retained baseline: {'yes' if trace_match else 'no'}",
            "",
            f"Relative non-regression gates: {'pass' if relative_pass else 'fail'}",
            "",
            "Phase 1 timed process allocations/bytes/deallocations: "
            f"{baseline_timed['allocations']}/{baseline_timed['allocated_bytes']}/"
            f"{baseline_timed['deallocations']}",
            "",
            "Milestone 10 timed process allocations/bytes/deallocations: "
            f"{candidate_timed['allocations']}/{candidate_timed['allocated_bytes']}/"
            f"{candidate_timed['deallocations']}",
            "",
            f"Final canonical acceptance: {'pass' if accepted else 'fail'}",
            "",
        ]
    )
    with args.report.open("a", encoding="utf-8") as report:
        report.write("\n".join(markdown))
    return 0 if accepted else 1


if __name__ == "__main__":
    sys.exit(main())
