#!/usr/bin/env python3
"""Capture build-time provenance from the commands that produced the binaries."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import shlex
import subprocess
import tempfile


SCHEMA = "LOB_BENCHMARK_PROVENANCE_V1"


def run(*args: str, cwd: Path | None = None) -> str:
    completed = subprocess.run(
        args,
        cwd=cwd,
        check=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    return completed.stdout.strip()


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def compile_entry(database: list[dict[str, object]], target: str) -> list[str]:
    marker = f"CMakeFiles/{target}.dir/"
    matches = [entry for entry in database if marker in str(entry.get("command", ""))]
    if len(matches) != 1:
        raise RuntimeError(f"expected one compile command for {target}, found {len(matches)}")
    entry = matches[0]
    if "arguments" in entry:
        return [str(value) for value in entry["arguments"]]  # type: ignore[index]
    return shlex.split(str(entry["command"]))


def normalized_argument(value: str, source_root: Path, build_root: Path) -> str:
    return value.replace(str(source_root), "<SOURCE_ROOT>").replace(
        str(build_root), "<BUILD_ROOT>"
    )


def normalized_command(
    arguments: list[str], source_root: Path, build_root: Path
) -> str:
    return shlex.join(
        normalized_argument(value, source_root, build_root) for value in arguments
    )


def compile_flags(
    arguments: list[str], source_root: Path, build_root: Path
) -> str:
    result: list[str] = []
    skip_next = False
    options_with_values = {"-o", "-MF", "-MT", "-MQ"}
    for value in arguments[1:]:
        if skip_next:
            skip_next = False
            continue
        if value in options_with_values:
            skip_next = True
            continue
        if value == "-c" or not value.startswith("-"):
            continue
        result.append(normalized_argument(value, source_root, build_root))
    return shlex.join(result)


def link_flags(arguments: list[str]) -> str:
    result: list[str] = []
    skip_next = False
    for value in arguments[1:]:
        if skip_next:
            skip_next = False
            continue
        if value == "-o":
            skip_next = True
            continue
        if value.startswith("-"):
            result.append(value)
    return shlex.join(result)


def quoted(value: str) -> str:
    return json.dumps(value, ensure_ascii=True)


def write_manifest(path: Path, values: dict[str, str]) -> None:
    lines = [SCHEMA]
    lines.extend(f"{key} {quoted(value)}" for key, value in values.items())
    path.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(
        "w", encoding="utf-8", dir=path.parent, delete=False
    ) as output:
        output.write("\n".join(lines) + "\n")
        temporary = Path(output.name)
    os.replace(temporary, path)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source-root", required=True, type=Path)
    parser.add_argument("--build-root", required=True, type=Path)
    parser.add_argument("--compile-commands", required=True, type=Path)
    parser.add_argument("--latency-target", required=True)
    parser.add_argument("--latency-executable", required=True, type=Path)
    parser.add_argument("--allocation-target", required=True)
    parser.add_argument("--allocation-executable", required=True, type=Path)
    parser.add_argument("--build-type", required=True)
    parser.add_argument("--compiler-id", required=True)
    parser.add_argument("--compiler-version", required=True)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()

    source_root = args.source_root.resolve()
    build_root = args.build_root.resolve()
    latency_executable = args.latency_executable.resolve()
    allocation_executable = args.allocation_executable.resolve()
    database = json.loads(args.compile_commands.read_text(encoding="utf-8"))
    latency_compile = compile_entry(database, args.latency_target)
    allocation_compile = compile_entry(database, args.allocation_target)
    latency_link_path = (
        build_root
        / "benchmarks"
        / "CMakeFiles"
        / f"{args.latency_target}.dir"
        / "link.txt"
    )
    allocation_link_path = (
        build_root
        / "benchmarks"
        / "CMakeFiles"
        / f"{args.allocation_target}.dir"
        / "link.txt"
    )
    latency_link = shlex.split(latency_link_path.read_text(encoding="utf-8"))
    allocation_link = shlex.split(allocation_link_path.read_text(encoding="utf-8"))
    compiler = Path(latency_compile[0]).resolve()
    compiler_banner = run(str(compiler), "--version").splitlines()[0]
    commit = run("git", "rev-parse", "HEAD", cwd=source_root)
    dirty = bool(
        run(
            "git",
            "status",
            "--porcelain",
            "--untracked-files=normal",
            cwd=source_root,
        )
    )

    values = {
        "SOURCE_ROOT": str(source_root),
        "SOURCE_COMMIT": commit,
        "SOURCE_DIRTY_AT_BUILD": "true" if dirty else "false",
        "BUILD_TYPE": args.build_type,
        "COMPILER_ID": args.compiler_id,
        "COMPILER_VERSION": args.compiler_version,
        "COMPILER_BANNER": compiler_banner,
        "LATENCY_EXECUTABLE": str(latency_executable),
        "LATENCY_SHA256": sha256(latency_executable),
        "ALLOCATION_EXECUTABLE": str(allocation_executable),
        "ALLOCATION_SHA256": sha256(allocation_executable),
        "LATENCY_COMPILE_COMMAND": normalized_command(
            latency_compile, source_root, build_root
        ),
        "LATENCY_COMPILE_FLAGS": compile_flags(
            latency_compile, source_root, build_root
        ),
        "LATENCY_LINK_COMMAND": normalized_command(
            latency_link, source_root, build_root
        ),
        "LATENCY_LINK_FLAGS": link_flags(latency_link),
        "ALLOCATION_COMPILE_COMMAND": normalized_command(
            allocation_compile, source_root, build_root
        ),
        "ALLOCATION_COMPILE_FLAGS": compile_flags(
            allocation_compile, source_root, build_root
        ),
        "ALLOCATION_LINK_COMMAND": normalized_command(
            allocation_link, source_root, build_root
        ),
        "ALLOCATION_LINK_FLAGS": link_flags(allocation_link),
    }
    write_manifest(args.output, values)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
