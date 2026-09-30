# Runtime benchmarks

Build the original VM (`b0fc8e6`), tree walker (`c65a03d`), and this branch in Release mode with the same compiler.

```sh
python3 benchmarks/run_comparison.py \
  --nanbox /path/to/nanbox/scsa \
  --vm /path/to/vm/scsa \
  --treewalk /path/to/treewalk/scsa \
  --node /path/to/node \
  --runs 3 --warmup 1 --iterations 2 --skip-memory
```

Use `--only matrix sieve` to limit the workloads. Add `--compact /path/to/scsa` to compare with `803e698`.

[Raw results](results/nan_boxing/results.json) contain timings, checksums, runtime versions, and source hashes. Generated Markdown reports stay local.
