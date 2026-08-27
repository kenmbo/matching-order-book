#!/usr/bin/env python3
"""Validate and compare canonical Phase 1 and Phase 2 benchmark artifacts."""

from __future__ import annotations

import argparse
import copy
import json
import os
from pathlib import Path
import re
import sys
import tempfile
from typing import Any


BASELINE_SCHEMA = "lob.phase1.performance.v2"
CANDIDATE_SCHEMA = "lob.phase2.pool.performance.v2"
BASELINE_PROFILE = "phase1_allocating_storage"
CANDIDATE_PROFILE = "phase2_pool_backed_storage"
CANONICAL_WORKLOADS = (
    "mixed",
    "cancel",
    "unknown_cancel",
    "reduce",
    "increase",
    "noop",
    "unknown_amend",
    "noncross_add",
    "fill1",
    "fill4",
    "fill16",
    "fill64",
    "fill256",
    "multi_level",
)
CANONICAL_SEEDS = (24301, 12648430)
CANONICAL_SAMPLES = {
    "mixed": 1_000_000,
    "cancel": 500_000,
    "unknown_cancel": 1_000_000,
    "reduce": 500_000,
    "increase": 500_000,
    "noop": 1_000_000,
    "unknown_amend": 1_000_000,
    "noncross_add": 500_000,
    "fill1": 200_000,
    "fill4": 50_000,
    "fill16": 20_000,
    "fill64": 5_000,
    "fill256": 1_000,
    "multi_level": 20_000,
}
LATENCY_FIELDS = (
    "p50_ns",
    "p90_ns",
    "p99_ns",
    "p999_ns",
    "p9999_ns",
    "maximum_ns",
)
TRACE_FIELDS = (
    "samples",
    "warmup",
    "repetitions",
    "command_to_outbox_observed",
    "matching_core_measured",
    "nominal_fills",
    "operation_counts",
    "command_quantity_volume",
    "quantity_bucket_64_is_overflow",
    "command_quantity_distribution",
    "top_five_volume",
    "priced_volume",
    "expected_active_range",
    "target_precondition_orders",
    "target_precondition_levels",
    "trace_checksum",
)
SEMANTIC_FIELDS = (
    "checksum",
    "accepted",
    "rejected",
    "fill_count_distribution",
    "emitted_event_count_distribution",
    "active_range",
    "level_range",
)
TIMING_FIELDS = (
    "primary_boundary",
    "matching_core_measured",
    "execution_outbox_capacity",
    "status_outbox_capacity",
    "outbox_drain_policy",
    "trace_generation_completed_before_timing",
    "sample_and_result_buffers_presized_before_timing",
    "statistics_and_serialization_after_timing",
    "percentile_convention",
)
ENVIRONMENT_FIELDS = (
    "cpu_model",
    "microcode",
    "kernel",
    "compiler",
    "affinity_mask",
    "smt_sibling",
    "sibling_occupancy",
    "governor",
    "scaling_driver",
    "scaling_min_khz",
    "scaling_max_khz",
    "energy_performance_preference",
    "numa_node",
    "clock",
    "overhead_subtracted",
)
PROVENANCE_COMPARISON_FIELDS = (
    "compiler_id",
    "compiler_version",
    "compiler_banner",
    "latency_compile_flags",
    "latency_link_flags",
)
SHA256 = re.compile(r"^[0-9a-f]{64}$")


class ValidationError(ValueError):
    """The supplied benchmark artifact is not eligible for comparison."""


def require(condition: bool, message: str) -> None:
    if not condition:
        raise ValidationError(message)


def load_object(path: Path) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise ValidationError(f"cannot read {path}: {error}") from error
    require(isinstance(value, dict), f"{path} does not contain a JSON object")
    return value


def percentage(delta: float, baseline: float) -> float | None:
    return None if baseline == 0 else delta * 100.0 / baseline


def canonical_keys() -> list[tuple[str, int]]:
    return [
        (workload, seed)
        for seed in CANONICAL_SEEDS
        for workload in CANONICAL_WORKLOADS
    ]


def validate_provenance(artifact: dict[str, Any], label: str) -> None:
    provenance = artifact.get("provenance")
    require(isinstance(provenance, dict), f"{label} provenance is missing")
    for field in (
        "manifest_loaded",
        "source_commit_matches",
        "both_binary_hashes_match",
        "executing_expected_binary",
        "release_configuration_valid",
        "canonical_eligible",
    ):
        require(provenance.get(field) is True, f"{label} provenance {field} failed")
    require(
        provenance.get("source_dirty_at_build") is False,
        f"{label} was built from a dirty tree",
    )
    require(
        provenance.get("execution_tree_dirty") is False,
        f"{label} was executed from a dirty tree",
    )
    require(
        provenance.get("source_commit") == provenance.get("execution_commit"),
        f"{label} build and execution commits differ",
    )
    require(
        artifact.get("git_commit") == provenance.get("source_commit"),
        f"{label} measured source commit is inconsistent",
    )
    require(artifact.get("git_dirty") is False, f"{label} reports a dirty tree")
    for field in (
        "latency_executable_sha256",
        "allocation_audit_executable_sha256",
    ):
        require(
            isinstance(provenance.get(field), str)
            and SHA256.fullmatch(provenance[field]) is not None,
            f"{label} {field} is not a SHA-256 digest",
        )
    require(
        artifact.get("build_type") == "Release",
        f"{label} is not a Release build",
    )
    require(
        artifact.get("compile_flags") == provenance.get("latency_compile_flags"),
        f"{label} compile flags are not tied to build provenance",
    )
    require(
        artifact.get("link_flags") == provenance.get("latency_link_flags"),
        f"{label} link flags are not tied to build provenance",
    )


def validate_repetitions(workload: dict[str, Any], label: str) -> None:
    repetitions = workload.get("repetition_results")
    require(isinstance(repetitions, list), f"{label} repetitions are missing")
    require(len(repetitions) == 5, f"{label} must contain exactly five repetitions")
    indexes = [item.get("index") for item in repetitions if isinstance(item, dict)]
    require(indexes == list(range(5)), f"{label} repetition indexes must be 0..4")
    samples = workload["samples"]
    for repetition in repetitions:
        require(isinstance(repetition, dict), f"{label} repetition is not an object")
        statistics = repetition.get("statistics")
        require(isinstance(statistics, dict), f"{label} repetition statistics missing")
        require(
            statistics.get("sample_count") == samples,
            f"{label} repetition sample count differs from trace",
        )
        for field in (*LATENCY_FIELDS, "throughput_per_second"):
            require(
                isinstance(statistics.get(field), (int, float)),
                f"{label} repetition statistic {field} is missing",
            )
        for field in SEMANTIC_FIELDS:
            require(field in repetition, f"{label} semantic field {field} is missing")
    for field in SEMANTIC_FIELDS:
        require(
            all(item[field] == repetitions[0][field] for item in repetitions[1:]),
            f"{label} semantic field {field} differs between repetitions",
        )


def validate_workloads(
    artifact: dict[str, Any], label: str
) -> dict[tuple[str, int], dict[str, Any]]:
    workloads = artifact.get("workloads")
    require(isinstance(workloads, list), f"{label} workloads are missing")
    keys: list[tuple[str, int]] = []
    indexed: dict[tuple[str, int], dict[str, Any]] = {}
    for item in workloads:
        require(isinstance(item, dict), f"{label} workload is not an object")
        key = (item.get("name"), item.get("seed"))
        require(
            isinstance(key[0], str) and isinstance(key[1], int),
            f"{label} workload key is invalid",
        )
        typed_key = (key[0], key[1])
        require(typed_key not in indexed, f"{label} has duplicate key {typed_key}")
        keys.append(typed_key)
        indexed[typed_key] = item
    expected = canonical_keys()
    require(
        set(keys) == set(expected),
        f"{label} workload/seed key set is incomplete or extra",
    )
    require(keys == expected, f"{label} workload/seed entries are not canonical")
    for key, workload in indexed.items():
        name, _ = key
        for field in TRACE_FIELDS:
            require(field in workload, f"{label} {key} trace field {field} is missing")
        require(
            workload.get("samples") == CANONICAL_SAMPLES[name],
            f"{label} {key} has a noncanonical sample count",
        )
        require(workload.get("warmup") == 10_000, f"{label} {key} warm-up differs")
        require(
            workload.get("repetitions") == 5,
            f"{label} {key} repetition count differs",
        )
        require(workload.get("workload_valid") is True, f"{label} {key} is invalid")
        require(
            workload.get("allocation_policy_compliant") is True,
            f"{label} {key} failed allocation policy",
        )
        median = workload.get("median")
        require(isinstance(median, dict), f"{label} {key} median is missing")
        for field in (*LATENCY_FIELDS, "throughput_per_second"):
            require(
                isinstance(median.get(field), (int, float)),
                f"{label} {key} median {field} is missing",
            )
        validate_repetitions(workload, f"{label} {key}")
        for field in (*LATENCY_FIELDS, "throughput_per_second"):
            values = sorted(
                repetition["statistics"][field]
                for repetition in workload["repetition_results"]
            )
            require(
                median[field] == values[2],
                f"{label} {key} median {field} is inconsistent",
            )
    return indexed


def validate_common(
    artifact: dict[str, Any], label: str, schema: str, profile: str
) -> dict[tuple[str, int], dict[str, Any]]:
    require(artifact.get("schema") == schema, f"{label} schema is not {schema}")
    require(artifact.get("baseline_profile") == profile, f"{label} profile is invalid")
    require(artifact.get("mode") == "acceptance", f"{label} is not an acceptance run")
    require(
        artifact.get("canonical_configuration_valid") is True,
        f"{label} canonical configuration failed",
    )
    require(
        artifact.get("local_acceptance_passed") is True,
        f"{label} did not pass local acceptance",
    )
    require(artifact.get("run_validity_passed") is True, f"{label} run validity failed")
    require(
        artifact.get("allocation_policy_compliant") is True,
        f"{label} allocation policy failed",
    )
    require(
        artifact.get("performance_gate_compliant") is True,
        f"{label} absolute performance gate failed",
    )
    require(
        artifact.get("canonical_seeds") == list(CANONICAL_SEEDS),
        f"{label} seed contract differs",
    )
    require(artifact.get("canonical_workload_count") == 14, f"{label} workload count differs")
    require(artifact.get("canonical_warmup") == 10_000, f"{label} canonical warm-up differs")
    require(artifact.get("canonical_repetitions") == 5, f"{label} canonical repetitions differ")
    require(artifact.get("sample_count_override") == 0, f"{label} used a sample override")
    for field in TIMING_FIELDS:
        require(field in artifact, f"{label} timing field {field} is missing")
    environment = artifact.get("environment")
    require(isinstance(environment, dict), f"{label} environment is missing")
    for field in ENVIRONMENT_FIELDS:
        require(field in environment, f"{label} environment field {field} is missing")
    require(
        isinstance(environment.get("affinity_mask"), str)
        and environment["affinity_mask"].isdigit(),
        f"{label} was not pinned to exactly one CPU",
    )
    require(
        environment.get("sibling_occupancy") not in (None, "", "not_observed"),
        f"{label} SMT sibling policy was not recorded",
    )
    validate_provenance(artifact, label)
    return validate_workloads(artifact, label)


def validate_candidate_absolute_gates(
    candidate: dict[tuple[str, int], dict[str, Any]]
) -> None:
    for key, workload in candidate.items():
        applicable = workload.get("performance_gate_applicable") is True
        require(
            applicable == (workload.get("nominal_fills") <= 1),
            f"{key} performance-gate applicability is inconsistent",
        )
        repetitions = workload["repetition_results"]
        calculated_passes = 0
        for repetition in repetitions:
            stats = repetition["statistics"]
            passed = (
                stats["p50_ns"] <= 1_500
                and stats["p99_ns"] <= 5_000
                and stats["p999_ns"] <= 15_000
                and stats["throughput_per_second"] >= 500_000
            )
            require(
                repetition.get("public_gate_passed") is passed,
                f"candidate {key} has an inconsistent absolute gate result",
            )
            calculated_passes += int(passed)
        require(
            workload.get("public_gate_passes") == calculated_passes,
            f"candidate {key} public gate pass count is inconsistent",
        )
        require(
            not applicable
            or (
                calculated_passes >= 4
                and workload.get("performance_gate_compliant") is True
            ),
            f"candidate {key} failed the four-of-five absolute gate",
        )
        require(
            workload.get("performance_gate_compliant")
            is (not applicable or calculated_passes >= 4),
            f"candidate {key} performance-gate summary is inconsistent",
        )


def compare_exact_configuration(
    baseline: dict[str, Any],
    candidate: dict[str, Any],
    old: dict[tuple[str, int], dict[str, Any]],
    current: dict[tuple[str, int], dict[str, Any]],
) -> None:
    for field in TIMING_FIELDS:
        require(
            baseline[field] == candidate[field],
            f"timing configuration mismatch: {field}",
        )
    for field in ENVIRONMENT_FIELDS:
        require(
            baseline["environment"][field] == candidate["environment"][field],
            f"environment mismatch: {field}",
        )
    for field in PROVENANCE_COMPARISON_FIELDS:
        require(
            baseline["provenance"].get(field)
            == candidate["provenance"].get(field),
            f"build environment mismatch: {field}",
        )
    for key in canonical_keys():
        before = old[key]
        after = current[key]
        for field in TRACE_FIELDS:
            require(
                before.get(field) == after.get(field),
                f"trace configuration mismatch for {key}: {field}",
            )
        for index in range(5):
            old_repetition = before["repetition_results"][index]
            new_repetition = after["repetition_results"][index]
            for field in SEMANTIC_FIELDS:
                require(
                    old_repetition.get(field) == new_repetition.get(field),
                    f"semantic result mismatch for {key} repetition {index}: {field}",
                )


def timed_totals(
    workloads: dict[tuple[str, int], dict[str, Any]],
    counter_field: str = "timed_allocations",
) -> dict[str, int]:
    totals = {"allocations": 0, "allocated_bytes": 0, "deallocations": 0}
    for workload in workloads.values():
        for repetition in workload["repetition_results"]:
            counters = repetition.get(counter_field)
            require(
                isinstance(counters, dict),
                f"{counter_field} evidence is missing",
            )
            for field in totals:
                require(
                    isinstance(counters.get(field), int),
                    f"timed allocation {field} missing",
                )
                totals[field] += counters[field]
    return totals


def compare_artifacts(
    baseline: dict[str, Any], candidate: dict[str, Any]
) -> tuple[dict[str, Any], str, bool]:
    old = validate_common(baseline, "baseline", BASELINE_SCHEMA, BASELINE_PROFILE)
    current = validate_common(
        candidate, "candidate", CANDIDATE_SCHEMA, CANDIDATE_PROFILE
    )
    require(
        baseline.get("accepted_canonical_baseline") is True
        and baseline.get("final_canonical_acceptance") is True,
        "baseline is not an eligible accepted canonical baseline",
    )
    require(
        candidate.get("baseline_comparison_status") == "not_performed"
        and candidate.get("final_canonical_acceptance") is False,
        "candidate comparison state is not pristine; a failed result cannot be promoted",
    )
    validate_candidate_absolute_gates(old)
    validate_candidate_absolute_gates(current)
    compare_exact_configuration(baseline, candidate, old, current)

    comparisons: list[dict[str, Any]] = []
    relative_pass = True
    markdown = [
        "# Phase 2 Pool-backed Storage Comparison",
        "",
        f"Baseline commit: `{baseline['provenance']['source_commit']}`",
        "",
        f"Candidate commit: `{candidate['provenance']['source_commit']}`",
        "",
        "| Workload | Seed | Metric | Before | After | Absolute | Percent | Gate |",
        "| --- | ---: | --- | ---: | ---: | ---: | ---: | --- |",
    ]
    for key in canonical_keys():
        prior = old[key]
        after = current[key]
        metrics: dict[str, Any] = {}
        for field in (*LATENCY_FIELDS, "throughput_per_second"):
            before_value = prior["median"][field]
            after_value = after["median"][field]
            delta = after_value - before_value
            change = percentage(delta, before_value)
            gate = "informational"
            passed = True
            if field == "p99_ns":
                passed = after_value <= before_value * 1.10
                gate = "pass" if passed else "fail"
            elif field == "p999_ns":
                passed = after_value <= before_value * 1.15
                gate = "pass" if passed else "fail"
            elif field == "throughput_per_second":
                passed = after_value >= before_value * 0.90
                gate = "pass" if passed else "fail"
            relative_pass = relative_pass and passed
            metrics[field] = {
                "before": before_value,
                "after": after_value,
                "absolute_difference": delta,
                "percentage_difference": change,
                "relative_gate": gate,
            }
            percent_text = "n/a" if change is None else f"{change:.3f}%"
            markdown.append(
                f"| {key[0]} | {key[1]} | {field} | {before_value:.3f} | "
                f"{after_value:.3f} | {delta:.3f} | {percent_text} | {gate} |"
            )
        comparisons.append(
            {
                "workload": key[0],
                "seed": key[1],
                "trace_checksum_match": True,
                "semantic_results_match": True,
                "public_path_gate_status": (
                    "pass"
                    if after["performance_gate_applicable"]
                    else "informational"
                ),
                "multi_fill_status": (
                    "not_applicable"
                    if after["nominal_fills"] == 0
                    else "pass"
                    if after["median"]["p99_ns"]
                    <= 1_000 + 300 * after["nominal_fills"]
                    else "miss"
                ),
                "allocation_policy_status": "pass",
                "run_validity_status": "pass",
                "metrics": metrics,
            }
        )

    baseline_timed = timed_totals(old)
    candidate_timed = timed_totals(current)
    baseline_collection = timed_totals(old, "timed_sample_collection_allocations")
    candidate_collection = timed_totals(
        current, "timed_sample_collection_allocations"
    )
    zero = {"allocations": 0, "allocated_bytes": 0, "deallocations": 0}
    require(
        candidate_timed == zero,
        "candidate timed process allocation totals are not zero",
    )
    require(
        baseline_collection == zero and candidate_collection == zero,
        "benchmark-owned timed allocation totals are not zero",
    )
    result = copy.deepcopy(candidate)
    result["baseline_comparison_status"] = "passed" if relative_pass else "failed"
    result["relative_non_regression_compliant"] = relative_pass
    result["final_canonical_acceptance"] = relative_pass
    result["baseline_comparison"] = {
        "baseline_profile": BASELINE_PROFILE,
        "baseline_source_commit": baseline["provenance"]["source_commit"],
        "trace_checksums_match": True,
        "semantic_results_match": True,
        "environment_comparable": True,
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
    markdown.extend(
        [
            "",
            "Canonical configuration, timing, trace, semantic, build, and environment parity: pass",
            "",
            f"Relative non-regression gates: {'pass' if relative_pass else 'fail'}",
            "",
            "Phase 1 timed process allocations/bytes/deallocations: "
            f"{baseline_timed['allocations']}/{baseline_timed['allocated_bytes']}/"
            f"{baseline_timed['deallocations']}",
            "",
            "Phase 2 timed process allocations/bytes/deallocations: "
            f"{candidate_timed['allocations']}/{candidate_timed['allocated_bytes']}/"
            f"{candidate_timed['deallocations']}",
            "",
            f"Final canonical acceptance: {'pass' if relative_pass else 'fail'}",
            "",
        ]
    )
    return result, "\n".join(markdown), relative_pass


def write_atomic(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(
        "w", encoding="utf-8", dir=path.parent, delete=False
    ) as output:
        output.write(text)
        temporary = Path(output.name)
    os.replace(temporary, path)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--baseline", required=True, type=Path)
    parser.add_argument("--candidate", required=True, type=Path)
    parser.add_argument("--output-json", required=True, type=Path)
    parser.add_argument("--report", required=True, type=Path)
    args = parser.parse_args()
    inputs = {args.baseline.resolve(), args.candidate.resolve()}
    outputs = {args.output_json.resolve(), args.report.resolve()}
    if inputs & outputs or len(outputs) != 2:
        raise SystemExit("comparison outputs must be distinct from both immutable inputs")
    try:
        result, report, accepted = compare_artifacts(
            load_object(args.baseline), load_object(args.candidate)
        )
    except ValidationError as error:
        print(f"comparison rejected: {error}", file=sys.stderr)
        return 2
    write_atomic(args.output_json, json.dumps(result, indent=2) + "\n")
    write_atomic(args.report, report)
    return 0 if accepted else 1


if __name__ == "__main__":
    sys.exit(main())
