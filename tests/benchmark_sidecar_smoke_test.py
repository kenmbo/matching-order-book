#!/usr/bin/env python3
"""Produce and consume a Phase 2 allocation sidecar through public binaries."""

from __future__ import annotations

import json
import os
from pathlib import Path
import subprocess
import sys


AUDIT = sys.argv[1]
LATENCY = sys.argv[2]
SIDECAR = Path(sys.argv[3])
CPU = str(min(os.sched_getaffinity(0)))
COMMON = (
    "--mode",
    "smoke",
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
    CPU,
)


def main() -> int:
    subprocess.run(
        (AUDIT, *COMMON, "--allocation-output", str(SIDECAR)), check=True
    )
    if not SIDECAR.read_text(encoding="utf-8").startswith(
        "LOB_PHASE2_POOL_ALLOCATION_V4 smoke"
    ):
        raise RuntimeError("allocation sidecar schema/configuration mismatch")
    completed = subprocess.run(
        (LATENCY, *COMMON, "--allocation-input", str(SIDECAR)),
        check=True,
        stdout=subprocess.PIPE,
        text=True,
    )
    result = json.loads(completed.stdout)
    if result["schema"] != "lob.phase2.pool.performance.v2":
        raise RuntimeError("latency smoke schema mismatch")
    if not result["allocation_audit_attached"]:
        raise RuntimeError("latency smoke did not consume allocation evidence")
    if (
        result["timed_process_allocation_count"] != 0
        or result["timed_process_allocated_bytes"] != 0
        or result["timed_process_deallocation_count"] != 0
    ):
        raise RuntimeError("latency smoke timed allocation gate failed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
