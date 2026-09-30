#!/usr/bin/env python3
"""Compare two SCSA binaries on focused RuntimeValue workloads."""

import argparse
import re
import statistics
import subprocess
import time
from pathlib import Path


BENCHMARKS = {
    "numeric_loop": ("numeric_loop.scsa", 250_000),
    "string_churn": ("string_churn.scsa", 450_000),
    "dictionary_ops": ("dictionary_ops.scsa", 31_996_000),
    "function_calls": ("function_calls.scsa", 400_000),
}
CHECKSUM_RE = re.compile(r"Checksum:\s*(-?\d+)")


def run_once(binary, source, expected):
    started = time.perf_counter()
    result = subprocess.run([binary, str(source)], capture_output=True, text=True)
    elapsed = time.perf_counter() - started
    match = CHECKSUM_RE.search(result.stdout)
    if result.returncode != 0:
        raise RuntimeError(f"{binary} failed on {source.name}: {result.stderr}")
    if match is None or int(match.group(1)) != expected:
        raise RuntimeError(
            f"{binary} produced an invalid checksum for {source.name}: {result.stdout!r}"
        )
    return elapsed


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baseline", required=True, help="Path to baseline scsa binary")
    parser.add_argument("--candidate", required=True, help="Path to candidate scsa binary")
    parser.add_argument("--runs", type=int, default=9, help="Measured runs per binary/workload")
    args = parser.parse_args()
    if args.runs < 1:
        parser.error("--runs must be at least 1")

    workloads = Path(__file__).parent / "runtime_values"
    binaries = {"baseline": args.baseline, "candidate": args.candidate}
    timings = {name: {version: [] for version in binaries} for name in BENCHMARKS}

    print(f"Runs per binary/workload: {args.runs} (plus one warm-up)")
    for name, (filename, expected) in BENCHMARKS.items():
        source = workloads / filename
        print(f"\n{name} (checksum {expected})")

        # Warm both builds before measurement and verify output correctness.
        for version, binary in binaries.items():
            run_once(binary, source, expected)

        for run in range(args.runs):
            order = ("baseline", "candidate") if run % 2 == 0 else ("candidate", "baseline")
            for version in order:
                elapsed = run_once(binaries[version], source, expected)
                timings[name][version].append(elapsed)

        medians = {}
        for version in binaries:
            samples = timings[name][version]
            medians[version] = statistics.median(samples)
            print(
                f"  {version}: median {medians[version]:.6f}s, "
                f"mean {statistics.mean(samples):.6f}s"
            )
        print(f"  baseline/candidate: {medians['baseline'] / medians['candidate']:.2f}x")


if __name__ == "__main__":
    main()
