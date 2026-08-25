#!/usr/bin/env python3
"""Verify that canonical acceptance rejects every configurable deviation."""

from __future__ import annotations

import os
import subprocess
import sys
import unittest


BENCHMARK = sys.argv.pop(1)


class BenchmarkCliTests(unittest.TestCase):
    def assert_rejected(self, *arguments: str) -> None:
        completed = subprocess.run(
            (BENCHMARK, *arguments),
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            check=False,
        )
        self.assertEqual(completed.returncode, 2, completed.stderr)

    def test_acceptance_deviations_are_rejected_before_execution(self) -> None:
        common = (
            "--mode",
            "acceptance",
            "--workload",
            "all",
            "--seeds",
            "24301,12648430",
            "--warmup",
            "10000",
            "--repetitions",
            "5",
            "--cpu",
            "0",
            "--sibling-occupancy",
            "sibling_idle",
        )
        cases = (
            common[:5] + ("24301,24301",) + common[6:],
            common[:5] + ("12648430,24301",) + common[6:],
            common[:3] + ("mixed",) + common[4:],
            common + ("--samples", "1000"),
            common[:7] + ("9999",) + common[8:],
            common[:9] + ("6",) + common[10:],
            common[:5] + ("24301",) + common[6:],
            common[:-4],
        )
        for arguments in cases:
            with self.subTest(arguments=arguments):
                self.assert_rejected(*arguments)

    def test_custom_configuration_requires_exploratory_or_smoke_mode(self) -> None:
        cpu = str(min(os.sched_getaffinity(0)))
        completed = subprocess.run(
            (
                BENCHMARK,
                "--mode",
                "exploratory",
                "--workload",
                "unknown_cancel",
                "--seeds",
                "24301",
                "--samples",
                "10",
                "--warmup",
                "1",
                "--repetitions",
                "1",
                "--cpu",
                cpu,
            ),
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            check=False,
        )
        self.assertEqual(completed.returncode, 0, completed.stderr)
        self.assertIn('"mode": "exploratory"', completed.stdout)
        self.assertIn('"final_canonical_acceptance":false', completed.stdout)


if __name__ == "__main__":
    unittest.main()
