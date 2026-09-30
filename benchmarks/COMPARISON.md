# Cross-runtime comparison

`run_comparison.py` compares unchanged historical SCSA builds against this
branch, CPython and Node/V8. It carries forward the four original runtime-value
workloads and the expanded matrix/sieve/Fibonacci/Quicksort benchmark work.
The original runners and workload files are retained.

The comprehensive runner adds equivalent implementations across the engines,
variable short strings, floating-point arithmetic, class construction/method
calls, and cyclic garbage. Its sources avoid MOD and % because the tree-walk
revision predates modulo support. Positive remainders use arithmetic and
truncation, or a conditional subtraction, in all three languages.

| Workload | Input | Exercises |
| --- | ---: | --- |
| Matrix | 80 × 80 | Nested arrays, indexing, integer arithmetic |
| Sieve | 100,000 | Array replication, boolean slots, mutation |
| Fibonacci | 25 | Recursion and frame/call overhead |
| Quicksort | 8,000 | Array mutation, branches, recursion |
| Integer loop | 500,000 | Arithmetic, local slots, conditional subtraction |
| Float loop | 500,000 | Inline doubles and numeric type checks |
| String churn | 50,000 | Conversion, variable string creation and lengths |
| Dictionary operations | 40,000 | Allocation, integer keys, membership, lookup |
| Function calls | 100,000 | Argument conversion/arithmetic and user calls |
| Object methods | 30,000 | Instances, field defaults, constructors, bound calls |
| Cyclic garbage | 20,000 | Unreachable array/dictionary cycles and automatic GC |

All integer intermediates fit within SCSA's original signed 32-bit domain.
Quicksort checks a weighted checksum of its sorted values, reducing it with
conditional subtraction to stay within that domain. Every measured iteration
must return the expected checksum; failures abort the report.

## Reproducing

Build these revisions with the same compiler and Release flags:

- `b0fc8e6`: original bytecode VM and original variant RuntimeValue.
- `c65a03d`: last tree-walk revision before the VM was added.
- `803e698`: previous compact-value experiment (optional extra comparison).
- `experiment/nan-boxing-mark-sweep`: this implementation.

Use separate worktrees/build directories so the baseline source is not modified.
The checked-in report records the exact compiler, CPU, runtime versions, hashes
and invocation. Node was provided through the existing locally downloaded
Node 24.21.0 distribution, whose archive had been SHA-256 verified.

```sh
python3 benchmarks/run_comparison.py \
  --nanbox /path/to/nanbox/scsa \
  --vm /path/to/original-vm/scsa \
  --treewalk /path/to/treewalk/scsa \
  --compact /path/to/compact/scsa \
  --python /path/to/python3 \
  --node /path/to/node \
  --cpu 4 --runs 9 --warmup 2 --iterations 5 \
  --output benchmarks/results/nan_boxing
```

`--cpu` pins the runner and its children on Linux. Omit it on other platforms.
Peak RSS collection requires GNU `/usr/bin/time`; use `--skip-memory` when it is
unavailable. `--only <names...>` selects workloads for a smoke run.

The runner produces Markdown, raw JSON and the generated sources. Hot results
use a batch timer around five invocations after two in-process warm-ups, excluding
output. The batch includes storing checksums in an array. Medians, min/max and
standard deviations use nine independent processes rather than treating
iterations inside one process as independent observations. The 1 ms SCSA timer
resolution applies to the batch, making very small differences uncertain.

Cold results launch one workload iteration with no warm-up and measure the
whole process. A separate whole-process hot table includes the full batch,
warm-ups, startup, parsing, output and teardown.

Three separate runs per workload measure peak RSS with the same batch settings.
An additional 2,000 / 20,000 / 200,000-cycle experiment uses one iteration without
warm-up to distinguish bounded GC memory from historical reference-count cycles.
GC diagnostics are enabled only in those memory runs.

CPython uses its normal allocator and cyclic collector. V8 uses its normal JIT
and collector, and can inline calls or eliminate allocations. Python dictionaries
and JS Maps implement the same benchmark key/value operations without being
identical language data types. No NumPy, typed arrays or native sorting shortcuts
are used. Results are specific to these algorithms, sizes, versions and machine.

See [the measured report](results/nan_boxing/README.md) for all tables and
[runtime design notes](../docs/RUNTIME_VALUES.md) for encoding and GC details.
