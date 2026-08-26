#!/usr/bin/env python3
"""Targeted contract tests for the Phase 2 canonical comparator."""

from __future__ import annotations

import copy
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


sys.dont_write_bytecode = True
COMPARATOR_PATH = Path(sys.argv.pop(1)).resolve()
spec = importlib.util.spec_from_file_location("compare_phase2", COMPARATOR_PATH)
if spec is None or spec.loader is None:
    raise RuntimeError("cannot load comparator")
compare_phase2 = importlib.util.module_from_spec(spec)
spec.loader.exec_module(compare_phase2)


def environment() -> dict[str, object]:
    return {
        "cpu_model": "test cpu",
        "microcode": "0x1",
        "kernel": "Linux 1 test",
        "compiler": "12.2.0",
        "affinity_mask": "2",
        "smt_sibling": "2,6",
        "sibling_occupancy": "sibling_idle",
        "governor": "performance",
        "frequency_khz": "3300000",
        "scaling_driver": "test-driver",
        "scaling_min_khz": "3300000",
        "scaling_max_khz": "3300000",
        "energy_performance_preference": "performance",
        "numa_node": "0",
        "clock": "std::chrono::steady_clock/CLOCK_MONOTONIC",
        "clock_resolution_ns": 1,
        "clock_overhead": {},
        "overhead_subtracted": False,
    }


def provenance(commit: str) -> dict[str, object]:
    flags = (
        "-O3 -DNDEBUG -Wall -Wextra -Werror -march=native -ffast-math "
        "-std=c++20"
    )
    return {
        "manifest_loaded": True,
        "source_commit": commit,
        "source_dirty_at_build": False,
        "execution_commit": commit,
        "execution_tree_dirty": False,
        "source_commit_matches": True,
        "latency_executable_sha256": "a" * 64,
        "allocation_audit_executable_sha256": "b" * 64,
        "both_binary_hashes_match": True,
        "executing_expected_binary": True,
        "compiler_id": "GNU",
        "compiler_version": "12.2.0",
        "compiler_banner": "g++ (Debian) 12.2.0",
        "latency_compile_command": "c++ test.cpp",
        "latency_compile_flags": flags,
        "latency_link_command": "c++ test.o",
        "latency_link_flags": "-O3 -DNDEBUG",
        "allocation_compile_command": "c++ -DLOB_ENABLE_ALLOCATION_AUDIT test.cpp",
        "allocation_compile_flags": flags + " -DLOB_ENABLE_ALLOCATION_AUDIT=1",
        "allocation_link_command": "c++ audit.o",
        "allocation_link_flags": "-O3 -DNDEBUG",
        "release_configuration_valid": True,
        "canonical_eligible": True,
    }


def nominal_fills(name: str) -> int:
    return {
        "fill1": 1,
        "fill4": 4,
        "fill16": 16,
        "fill64": 64,
        "fill256": 256,
        "multi_level": 16,
    }.get(name, 0)


def workload(name: str, seed: int, zero_allocations: bool) -> dict[str, object]:
    samples = compare_phase2.CANONICAL_SAMPLES[name]
    fills = nominal_fills(name)
    checksum = seed * 100 + compare_phase2.CANONICAL_WORKLOADS.index(name)
    statistics = {
        "sample_count": samples,
        "p50_ns": 500,
        "p90_ns": 600,
        "p99_ns": 700,
        "p999_ns": 800,
        "p9999_ns": 900,
        "maximum_ns": 1_000,
        "total_timed_ns": samples * 500,
        "throughput_per_second": 2_000_000.0,
    }
    repetitions = []
    for index in range(5):
        repetitions.append(
            {
                "index": index,
                "statistics": copy.deepcopy(statistics),
                "public_gate_passed": True,
                "checksum": checksum,
                "accepted": samples,
                "rejected": 0,
                "fill_count_distribution": {str(fills): samples},
                "emitted_event_count_distribution": {str(fills): samples},
                "active_range": [5_000, 6_000],
                "level_range": [10, 20],
                "timed_allocations": {
                    "allocations": 0 if zero_allocations else 1,
                    "allocated_bytes": 0 if zero_allocations else 64,
                    "deallocations": 0 if zero_allocations else 1,
                },
                "timed_sample_collection_allocations": {
                    "allocations": 0,
                    "allocated_bytes": 0,
                    "deallocations": 0,
                },
            }
        )
    return {
        "name": name,
        "seed": seed,
        "samples": samples,
        "warmup": 10_000,
        "repetitions": 5,
        "command_to_outbox_observed": fills > 0 or name == "mixed",
        "matching_core_measured": False,
        "nominal_fills": fills,
        "operation_counts": {
            "cancel": 200_000 if name == "mixed" else 0,
            "reduce": 200_000 if name == "mixed" else 0,
            "increase": 200_000 if name == "mixed" else 0,
            "noop": 100_000 if name == "mixed" else 0,
            "noncross_add": 200_000 if name == "mixed" else 0,
            "cross": 100_000 if name == "mixed" else 0,
        },
        "command_quantity_volume": samples,
        "quantity_bucket_64_is_overflow": True,
        "command_quantity_distribution": {"1": samples},
        "top_five_volume": samples,
        "priced_volume": samples,
        "expected_active_range": [5_000, 6_000],
        "target_precondition_orders": 6_000,
        "target_precondition_levels": 100,
        "trace_checksum": checksum + 1,
        "workload_valid": True,
        "allocation_policy_compliant": True,
        "performance_gate_applicable": fills <= 1,
        "performance_gate_compliant": True,
        "public_gate_passes": 5,
        "median": copy.deepcopy(statistics),
        "repetition_results": repetitions,
    }


def artifact(baseline: bool) -> dict[str, object]:
    commit = "1" * 40 if baseline else "2" * 40
    build = provenance(commit)
    result: dict[str, object] = {
        "schema": (
            compare_phase2.BASELINE_SCHEMA
            if baseline
            else compare_phase2.CANDIDATE_SCHEMA
        ),
        "baseline_profile": (
            compare_phase2.BASELINE_PROFILE
            if baseline
            else compare_phase2.CANDIDATE_PROFILE
        ),
        "primary_boundary": "public_process_completion",
        "matching_core_measured": False,
        "git_commit": commit,
        "git_dirty": False,
        "build_type": "Release",
        "compile_flags": build["latency_compile_flags"],
        "link_flags": build["latency_link_flags"],
        "mode": "acceptance",
        "canonical_configuration_valid": True,
        "local_acceptance_passed": True,
        "baseline_comparison_status": "not_required" if baseline else "not_performed",
        "final_canonical_acceptance": baseline,
        "canonical_seeds": list(compare_phase2.CANONICAL_SEEDS),
        "canonical_workload_count": 14,
        "canonical_warmup": 10_000,
        "canonical_repetitions": 5,
        "sample_count_override": 0,
        "execution_outbox_capacity": 1024,
        "status_outbox_capacity": 16,
        "outbox_drain_policy": "after_each_timed_process_call_outside_timing",
        "trace_generation_completed_before_timing": True,
        "sample_and_result_buffers_presized_before_timing": True,
        "statistics_and_serialization_after_timing": True,
        "percentile_convention": "nearest_rank_per_repetition_median_of_five",
        "run_validity_passed": True,
        "allocation_policy_compliant": True,
        "performance_gate_compliant": True,
        "provenance": build,
        "environment": environment(),
        "workloads": [
            workload(name, seed, not baseline)
            for seed in compare_phase2.CANONICAL_SEEDS
            for name in compare_phase2.CANONICAL_WORKLOADS
        ],
    }
    if baseline:
        result["accepted_canonical_baseline"] = True
    return result


class ComparatorTests(unittest.TestCase):
    def setUp(self) -> None:
        self.baseline = artifact(True)
        self.candidate = artifact(False)

    def rejected(self, baseline: dict[str, object] | None = None, candidate: dict[str, object] | None = None) -> None:
        with self.assertRaises(compare_phase2.ValidationError):
            compare_phase2.compare_artifacts(
                self.baseline if baseline is None else baseline,
                self.candidate if candidate is None else candidate,
            )

    def test_complete_comparison_passes_without_mutating_inputs(self) -> None:
        before = copy.deepcopy(self.candidate)
        result, report, accepted = compare_phase2.compare_artifacts(
            self.baseline, self.candidate
        )
        self.assertTrue(accepted)
        self.assertTrue(result["final_canonical_acceptance"])
        self.assertEqual(result["baseline_comparison_status"], "passed")
        self.assertIn("Final canonical acceptance: pass", report)
        self.assertEqual(self.candidate, before)

    def test_schema_and_profile_rejected(self) -> None:
        for field, value in (("schema", "old"), ("baseline_profile", "wrong")):
            with self.subTest(field=field):
                changed = copy.deepcopy(self.baseline)
                changed[field] = value
                self.rejected(baseline=changed)

    def test_incomplete_extra_and_duplicate_keys_rejected(self) -> None:
        incomplete = copy.deepcopy(self.candidate)
        incomplete["workloads"].pop()  # type: ignore[union-attr]
        self.rejected(candidate=incomplete)
        extra = copy.deepcopy(self.candidate)
        extra_item = copy.deepcopy(extra["workloads"][0])  # type: ignore[index]
        extra_item["name"] = "extra"
        extra["workloads"].append(extra_item)  # type: ignore[union-attr]
        self.rejected(candidate=extra)
        duplicate = copy.deepcopy(self.candidate)
        duplicate["workloads"].append(  # type: ignore[union-attr]
            copy.deepcopy(duplicate["workloads"][0])  # type: ignore[index]
        )
        self.rejected(candidate=duplicate)

    def test_noncanonical_counts_and_repetition_indexes_rejected(self) -> None:
        mutations = (
            ("samples", 999),
            ("warmup", 9_999),
            ("repetitions", 4),
        )
        for field, value in mutations:
            with self.subTest(field=field):
                changed = copy.deepcopy(self.candidate)
                changed["workloads"][0][field] = value  # type: ignore[index]
                self.rejected(candidate=changed)
        changed = copy.deepcopy(self.candidate)
        changed["workloads"][0]["repetition_results"][4]["index"] = 3  # type: ignore[index]
        self.rejected(candidate=changed)

    def test_trace_and_semantic_checksum_mismatch_rejected(self) -> None:
        trace = copy.deepcopy(self.candidate)
        trace["workloads"][0]["trace_checksum"] += 1  # type: ignore[index,operator]
        self.rejected(candidate=trace)
        semantic = copy.deepcopy(self.candidate)
        semantic["workloads"][0]["repetition_results"][0]["checksum"] += 1  # type: ignore[index,operator]
        self.rejected(candidate=semantic)

    def test_environment_and_build_mismatch_rejected(self) -> None:
        changed = copy.deepcopy(self.candidate)
        changed["environment"]["microcode"] = "different"  # type: ignore[index]
        self.rejected(candidate=changed)
        changed = copy.deepcopy(self.candidate)
        changed["provenance"]["latency_compile_flags"] += " -fomit-frame-pointer"  # type: ignore[index,operator]
        changed["compile_flags"] = changed["provenance"]["latency_compile_flags"]  # type: ignore[index]
        self.rejected(candidate=changed)

    def test_timing_configuration_mismatch_rejected(self) -> None:
        changed = copy.deepcopy(self.candidate)
        changed["percentile_convention"] = "different"
        self.rejected(candidate=changed)

    def test_dirty_or_unverified_provenance_rejected(self) -> None:
        for field in ("source_dirty_at_build", "execution_tree_dirty"):
            with self.subTest(field=field):
                changed = copy.deepcopy(self.candidate)
                changed["provenance"][field] = True  # type: ignore[index]
                if field == "execution_tree_dirty":
                    changed["git_dirty"] = True
                self.rejected(candidate=changed)
        changed = copy.deepcopy(self.candidate)
        changed["provenance"]["both_binary_hashes_match"] = False  # type: ignore[index]
        self.rejected(candidate=changed)

    def test_relative_gate_failure_stays_failed(self) -> None:
        changed = copy.deepcopy(self.candidate)
        changed["workloads"][0]["median"]["p99_ns"] = 800  # type: ignore[index]
        for repetition in changed["workloads"][0]["repetition_results"]:  # type: ignore[index]
            repetition["statistics"]["p99_ns"] = 800
        result, _, accepted = compare_phase2.compare_artifacts(
            self.baseline, changed
        )
        self.assertFalse(accepted)
        self.assertEqual(result["baseline_comparison_status"], "failed")
        self.assertFalse(result["final_canonical_acceptance"])
        self.rejected(candidate=result)

    def test_rejected_baseline_candidate_and_prior_failure_rejected(self) -> None:
        baseline = copy.deepcopy(self.baseline)
        baseline["accepted_canonical_baseline"] = False
        self.rejected(baseline=baseline)
        candidate = copy.deepcopy(self.candidate)
        candidate["local_acceptance_passed"] = False
        self.rejected(candidate=candidate)
        prior_failure = copy.deepcopy(self.candidate)
        prior_failure["baseline_comparison_status"] = "failed"
        self.rejected(candidate=prior_failure)

    def test_cli_is_idempotent_and_keeps_inputs_immutable(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            baseline_path = root / "baseline.json"
            candidate_path = root / "candidate.json"
            output_path = root / "compared.json"
            report_path = root / "comparison.md"
            baseline_path.write_text(json.dumps(self.baseline), encoding="utf-8")
            candidate_path.write_text(json.dumps(self.candidate), encoding="utf-8")
            original = candidate_path.read_bytes()
            command = (
                sys.executable,
                str(COMPARATOR_PATH),
                "--baseline",
                str(baseline_path),
                "--candidate",
                str(candidate_path),
                "--output-json",
                str(output_path),
                "--report",
                str(report_path),
            )
            subprocess.run(command, check=True)
            first_output = output_path.read_bytes()
            first_report = report_path.read_bytes()
            subprocess.run(command, check=True)
            self.assertEqual(output_path.read_bytes(), first_output)
            self.assertEqual(report_path.read_bytes(), first_report)
            self.assertEqual(candidate_path.read_bytes(), original)
            in_place = subprocess.run(
                (
                    sys.executable,
                    str(COMPARATOR_PATH),
                    "--baseline",
                    str(baseline_path),
                    "--candidate",
                    str(candidate_path),
                    "--output-json",
                    str(candidate_path),
                    "--report",
                    str(report_path),
                ),
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                check=False,
            )
            self.assertNotEqual(in_place.returncode, 0)
            self.assertEqual(candidate_path.read_bytes(), original)


if __name__ == "__main__":
    unittest.main()
