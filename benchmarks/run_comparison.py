#!/usr/bin/env python3
"""Benchmark NaN boxing against original/compact VMs, tree-walk, CPython and V8."""

import argparse
import hashlib
import json
import math
import os
import platform
import random
import re
import shutil
import shlex
import statistics
import subprocess
import sys
import time
from dataclasses import replace
from datetime import datetime, timezone
from pathlib import Path

from comparison_workloads import workloads
from run_benchmarks import get_cpu_info


def sources(workload, warmup, iterations):
    n = workload.size
    scsa_warmup = f"""
FOR warmup = 1 TO {warmup}
    benchmark({n})
END FOR
""" if warmup else ""
    scsa = workload.scsa + scsa_warmup + f"""
checksums = []
started = TIME()
FOR sample = 1 TO {iterations}
    checksum = benchmark({n})
    checksums.append(checksum)
END FOR
elapsed = TIME() - started
FOR checksum IN checksums
    PRINT("Checksum: " + STRING(checksum))
END FOR
PRINT("BatchKernel: " + STRING(elapsed))
"""
    python = "import time\n" + workload.python + f"""
for _ in range({warmup}):
    benchmark({n})
checksums = []
started = time.perf_counter()
for _ in range({iterations}):
    checksum = benchmark({n})
    checksums.append(checksum)
elapsed = time.perf_counter() - started
for checksum in checksums:
    print("Checksum:", checksum)
print("BatchKernel:", elapsed)
"""
    javascript = workload.javascript + f"""
for (let i = 0; i < {warmup}; i++) benchmark({n});
const checksums = [];
const started = process.hrtime.bigint();
for (let i = 0; i < {iterations}; i++) {{
    const checksum = benchmark({n});
    checksums.push(checksum);
}}
const elapsed = Number(process.hrtime.bigint() - started) / 1e9;
for (const checksum of checksums) console.log("Checksum:", checksum);
console.log("BatchKernel:", elapsed);
"""
    return {"scsa": scsa, "py": python, "js": javascript}


def run(command, expected, iterations, environment, memory=False):
    if memory:
        command = ["/usr/bin/time", "-f", "PeakRSS: %M", *command]
    started = time.perf_counter()
    result = subprocess.run(command, capture_output=True, text=True, env=environment, timeout=300)
    wall = time.perf_counter() - started
    if result.returncode:
        raise RuntimeError(f"{command} failed:\n{result.stderr}\n{result.stdout}")
    checksums = [int(value) for value in re.findall(r"Checksum:\s*(-?\d+)", result.stdout)]
    if checksums != [expected] * iterations:
        raise RuntimeError(f"Invalid checksums: expected {expected}, got {checksums}: {command}")
    kernel = [float(value) for value in re.findall(r"BatchKernel:\s*([0-9.eE+-]+)", result.stdout)]
    if (len(kernel) != 1 or kernel[0] < 0 or not math.isfinite(kernel[0])
            or kernel[0] == 0 and not memory):
        raise RuntimeError(f"Invalid timings: {result.stdout}")
    sample = {"wall_seconds": wall, "kernel_seconds": kernel[0] / iterations,
              "batch_seconds": kernel[0], "checksums": checksums}
    if memory:
        match = re.search(r"PeakRSS:\s*(\d+)", result.stderr)
        sample["peak_rss_kib"] = int(match.group(1))
        gc = re.search(r"GC: (.*)", result.stderr)
        if gc:
            sample["gc"] = {key: int(value) for key, value in
                            re.findall(r"(\w+)=(\d+)", gc.group(1))}
    return sample


def summarize(samples):
    # Each fresh process is an independent observation. Aggregate its measured iterations.
    kernel = [sample["kernel_seconds"] for sample in samples]
    wall = [sample["wall_seconds"] for sample in samples]
    return {
        "kernel_median": statistics.median(kernel),
        "kernel_min": min(kernel), "kernel_max": max(kernel),
        "kernel_mean": statistics.mean(kernel),
        "kernel_stdev": statistics.stdev(kernel) if len(kernel) > 1 else 0,
        "wall_median": statistics.median(wall),
        "wall_min": min(wall), "wall_max": max(wall),
        "samples": samples,
    }


def markdown(report):
    metadata = report["metadata"]
    names = list(report["commands"])
    lines = [
        "# NaN-boxing comparison",
        "",
        f"Measured {metadata['date']} on {metadata['cpu']}, {metadata['platform']}.",
        f"CPython: {metadata['python_version']}; Node/V8: {metadata['node_versions']}.",
        f"Compiler: {metadata['compiler']}. SCSA builds: Release (-O3 -DNDEBUG).",
        f"CPU affinity: {metadata['affinity']}. Baselines: {metadata['references']}.",
        f"NaN-boxing source revision: {metadata['candidate_revision']}.",
        "",
        f"{metadata['runs']} fresh processes per engine/workload, each with "
        f"{metadata['warmup']} untimed in-process warm-ups and "
        f"{metadata['iterations']} measured iterations. Engine order is shuffled with a fixed seed.",
        "Every measured checksum is validated. No forced CPython/V8 GC or JIT flags.",
        "Kernel results exclude loading/parsing and include allocation and automatic GC. "
        "Each process times one batch and divides by its iteration count, then the report takes "
        "the median across processes. Batch timing includes storing checksums in an array, "
        "and excludes printing them.",
        "SCSA TIME() has 1 ms resolution per batch; small differences within that error are not significant.",
        "",
        "## Kernel time (milliseconds per iteration)",
        "",
        "| Workload | " + " | ".join(names) + " | VM / NaN-box | Tree / NaN-box |",
        "| --- | " + " | ".join("---:" for _ in names) + " | ---: | ---: |",
    ]
    for workload, engines in report["results"].items():
        values = [f"{engines[name]['kernel_median'] * 1000:.3f}" for name in names]
        candidate = engines["NaN-box"]["kernel_median"]
        speedups = [engines[name]["kernel_median"] / candidate for name in ("Original VM", "Tree-walk")]
        lines.append("| " + workload + " | " + " | ".join(values) +
                     f" | {speedups[0]:.2f}x | {speedups[1]:.2f}x |")
    for baseline in names:
        if baseline == "NaN-box":
            continue
        ratios = [engines[baseline]["kernel_median"] / engines["NaN-box"]["kernel_median"]
                  for engines in report["results"].values()]
        geometric = math.exp(statistics.mean(math.log(value) for value in ratios))
        lines.extend(["", f"Equal-weight geometric mean of {baseline} / NaN-box: {geometric:.2f}x."])
    lines.extend([
        "",
        "Ratios above 1 mean NaN boxing is faster. These are workload-specific comparisons, "
        "not estimates of performance on arbitrary programs. V8 may inline calls and "
        "eliminate allocations; that is part of its production execution model.",
        "",
        "## Whole-process wall time (milliseconds)",
        "",
        "Includes startup, parsing/compilation, warm-ups, measured work, output, and teardown. "
        "It is the time for the entire batch, not one workload iteration.",
        "",
        "| Workload | " + " | ".join(names) + " |",
        "| --- | " + " | ".join("---:" for _ in names) + " |",
    ])
    for workload, engines in report["results"].items():
        lines.append("| " + workload + " | " +
                     " | ".join(f"{engines[name]['wall_median'] * 1000:.3f}" for name in names) + " |")
    lines.extend([
        "", "## Cold script wall time (milliseconds)", "",
        "One workload iteration per fresh process, with no warm-up. Includes startup, parsing, "
        "execution, output and teardown. Median over the same number of processes as the hot runs.",
        "",
        "| Workload | " + " | ".join(names) + " |",
        "| --- | " + " | ".join("---:" for _ in names) + " |",
    ])
    for workload, engines in report["cold"].items():
        lines.append("| " + workload + " | " +
                     " | ".join(f"{engines[name]['wall_median'] * 1000:.3f}" for name in names) + " |")
    lines.extend([
        "", "## Kernel variability (min–max milliseconds across process averages)", "",
        "| Workload | " + " | ".join(names) + " |",
        "| --- | " + " | ".join("---:" for _ in names) + " |",
    ])
    for workload, engines in report["results"].items():
        lines.append("| " + workload + " | " + " | ".join(
            f"{engines[name]['kernel_min'] * 1000:.3f}–{engines[name]['kernel_max'] * 1000:.3f}"
            for name in names) + " |")
    if report["memory"]:
        lines.extend([
            "", "## Peak resident memory (MiB)", "",
            "Median of three separate GNU time runs using the same warm-up/iteration counts. "
            "Includes interpreter/JIT and allocator overhead. Timing tables do not use GNU time.",
            "",
            "| Workload | " + " | ".join(names) + " |",
            "| --- | " + " | ".join("---:" for _ in names) + " |",
        ])
        for workload, engines in report["memory"].items():
            lines.append("| " + workload + " | " + " | ".join(
                f"{statistics.median(sample['peak_rss_kib'] for sample in engines[name]) / 1024:.2f}"
                for name in names) + " |")
    if report["cycle_scaling"]:
        lines.extend([
            "", "## Cyclic garbage memory scaling (MiB)", "",
            "One workload iteration without warm-up; median peak RSS over three fresh processes. "
            "Each iteration creates an unreachable array/dictionary cycle. "
            "Reference-counted historical builds retain those cycles until process exit.",
            "",
            "| Cycles | " + " | ".join(names) + " | NaN-box collections | NaN-box reclaimed objects |",
            "| ---: | " + " | ".join("---:" for _ in names) + " | ---: | ---: |",
        ])
        for size, engines in report["cycle_scaling"].items():
            candidate = engines["NaN-box"][0]["gc"]
            lines.append("| " + str(size) + " | " + " | ".join(
                f"{statistics.median(sample['peak_rss_kib'] for sample in engines[name]) / 1024:.2f}"
                for name in names) + f" | {candidate['collections']} | {candidate['reclaimed']} |")
    lines.extend([
        "", "## Reproducibility", "",
        "The adjacent JSON includes every raw timing, checksum expectation, memory sample, "
        "command, version, source hash, and GC statistics. Generated workload sources are in "
        "the adjacent sources directory.",
        "", "Run command:", "", "```sh",
        metadata["invocation"], "```", "",
    ])
    return "\n".join(lines)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--nanbox", required=True)
    parser.add_argument("--vm", required=True)
    parser.add_argument("--treewalk", required=True)
    parser.add_argument("--compact")
    parser.add_argument("--python", default=sys.executable)
    parser.add_argument("--node", default=shutil.which("node"))
    parser.add_argument("--runs", type=int, default=9)
    parser.add_argument("--warmup", type=int, default=2)
    parser.add_argument("--iterations", type=int, default=5)
    parser.add_argument("--cpu", type=int)
    parser.add_argument("--only", nargs="+", help="Select workload names for a smoke run")
    parser.add_argument("--skip-memory", action="store_true")
    parser.add_argument("--output", type=Path, default=Path(__file__).parent / "results" / "nan_boxing")
    args = parser.parse_args()
    if args.runs < 1 or args.iterations < 1 or args.warmup < 0:
        parser.error("runs/iterations must be positive and warmup nonnegative")
    if not args.node:
        parser.error("Node is required; pass --node /path/to/node")
    if args.cpu is not None:
        os.sched_setaffinity(0, {args.cpu})
    environment = os.environ.copy()
    for key in ("SCSA_GC_STRESS", "SCSA_GC_STATS", "SCSA_GC_THRESHOLD"):
        environment.pop(key, None)
    commands = {
        "NaN-box": [str(Path(args.nanbox).resolve())],
        "Original VM": [str(Path(args.vm).resolve())],
        "Tree-walk": [str(Path(args.treewalk).resolve())],
    }
    if args.compact:
        commands["Compact VM"] = [str(Path(args.compact).resolve())]
    commands["CPython"] = [args.python]
    commands["V8"] = [args.node]
    report = {
        "metadata": {
            "date": datetime.now(timezone.utc).isoformat(),
            "cpu": get_cpu_info(), "platform": platform.platform(),
            "python_version": subprocess.check_output([args.python, "--version"], text=True).strip(),
            "node_versions": subprocess.check_output(
                [args.node, "-p", "JSON.stringify({node:process.versions.node,v8:process.versions.v8})"],
                text=True).strip(),
            "compiler": subprocess.check_output(["c++", "--version"], text=True).splitlines()[0],
            "affinity": sorted(os.sched_getaffinity(0)) if hasattr(os, "sched_getaffinity") else None,
            "references": {"Original VM": "b0fc8e6", "Tree-walk": "c65a03d", "Compact VM": "803e698"},
            "candidate_revision": subprocess.check_output(
                ["git", "-C", str(Path(__file__).parent.parent), "rev-parse", "HEAD"],
                text=True).strip(),
            "runs": args.runs, "warmup": args.warmup, "iterations": args.iterations,
            "invocation": shlex.join(sys.argv),
            "candidate_source_sha256": {
                str(path.relative_to(Path(__file__).parent.parent)):
                hashlib.sha256(path.read_bytes()).hexdigest()
                for path in sorted((Path(__file__).parent.parent / "src").glob("*"))
                if path.is_file()
            },
            "binary_sha256": {name: hashlib.sha256(Path(command[0]).read_bytes()).hexdigest()
                              for name, command in commands.items()
                              if Path(command[0]).is_file()},
        },
        "commands": commands, "workloads": {}, "results": {}, "cold": {},
        "memory": {}, "cycle_scaling": {},
    }
    args.output.mkdir(parents=True, exist_ok=True)
    source_dir = args.output / "sources"
    source_dir.mkdir(exist_ok=True)
    rng = random.Random(20260930)
    selected = [workload for workload in workloads() if not args.only or workload.name in args.only]
    if not selected or args.only and set(args.only) - {workload.name for workload in selected}:
        parser.error("Unknown workload name")

    def write_sources(workload, warmup, iterations):
        files = {}
        for extension, source in sources(workload, warmup, iterations).items():
            path = source_dir / f"{workload.name}.{extension}"
            path.write_text(source)
            files[extension] = str(path.resolve())
        return files

    def command_for(name, files):
        extension = "py" if name == "CPython" else "js" if name == "V8" else "scsa"
        return [*commands[name], files[extension]]

    def memory_samples(workload, files, iterations):
        measurements = {name: [] for name in commands}
        for _ in range(3):
            order = list(commands)
            rng.shuffle(order)
            for name in order:
                memory_env = environment.copy()
                if name == "NaN-box":
                    memory_env["SCSA_GC_STATS"] = "1"
                measurements[name].append(run(command_for(name, files), workload.expected,
                                              iterations, memory_env, memory=True))
        return measurements

    for workload in selected:
        print(f"{workload.name}: size={workload.size}, checksum={workload.expected}", flush=True)
        files = write_sources(workload, args.warmup, args.iterations)
        report["workloads"][workload.name] = {
            "size": workload.size, "checksum": workload.expected,
            "source_sha256": {ext: hashlib.sha256(Path(path).read_bytes()).hexdigest()
                              for ext, path in files.items()},
        }
        samples = {name: [] for name in commands}
        # One untimed process also warms filesystem caches and validates all engines.
        for name in commands:
            run(command_for(name, files), workload.expected, args.iterations, environment)
        for index in range(args.runs):
            order = list(commands)
            rng.shuffle(order)
            for name in order:
                samples[name].append(run(command_for(name, files), workload.expected,
                                         args.iterations, environment))
            print(f"  measured process round {index + 1}/{args.runs}", flush=True)
        report["results"][workload.name] = {name: summarize(value) for name, value in samples.items()}
        for name, result in report["results"][workload.name].items():
            print(f"  {name}: kernel {result['kernel_median'] * 1000:.3f} ms; "
                  f"process {result['wall_median'] * 1000:.3f} ms", flush=True)
        cold_files = write_sources(replace(workload, name=workload.name + "_cold"), 0, 1)
        cold_samples = {name: [] for name in commands}
        for _ in range(args.runs):
            order = list(commands)
            rng.shuffle(order)
            for name in order:
                cold_samples[name].append(run(command_for(name, cold_files), workload.expected,
                                              1, environment))
        report["cold"][workload.name] = {name: summarize(value)
                                        for name, value in cold_samples.items()}
        if not args.skip_memory:
            report["memory"][workload.name] = memory_samples(workload, files, args.iterations)
        (args.output / "results.json").write_text(json.dumps(report, indent=2) + "\n")

    if not args.skip_memory:
        cyclic = next(workload for workload in workloads() if workload.name == "cyclic_garbage")
        for size in (2000, 20000, 200000):
            workload = replace(cyclic, name=f"cyclic_scaling_{size}", size=size, expected=2 * size)
            print(f"Cyclic memory scaling: {size}", flush=True)
            files = write_sources(workload, 0, 1)
            report["cycle_scaling"][size] = memory_samples(workload, files, 1)
    (args.output / "results.json").write_text(json.dumps(report, indent=2) + "\n")
    (args.output / "README.md").write_text(markdown(report))
    print(f"Results: {args.output / 'README.md'}", flush=True)


if __name__ == "__main__":
    main()
