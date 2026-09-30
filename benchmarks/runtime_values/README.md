# Runtime value benchmarks

These workloads complement the matrix multiplication and sieve benchmarks by exercising primitive arithmetic, short-string creation, dictionaries, and callable values.

Run them against two Release builds with:

```sh
python3 benchmarks/run_runtime_value_benchmarks.py \
  --baseline /path/to/baseline/scsa \
  --candidate /path/to/candidate/scsa
```

The runner warms up both binaries, alternates run order, checks each output checksum, and reports median and mean process wall time. Process startup and parsing are included. Use identical compiler settings for both builds.
