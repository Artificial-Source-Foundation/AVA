# Backend Benchmarking

AVA's M0 backend harness records the pre-modernization architecture without credentials, network access, or optional dependencies. Results are observations of one exact machine, build, commit, and binary. They are not portable performance claims and must not be compared as if different hosts or build types were equivalent.

## Build and run

Build the application and test-only helper first:

```sh
cmake --preset release
cmake --build build-release --target ava ava_backend_benchmark_helper
```

Run the quick suite used by CTest:

```sh
scripts/benchmark-backend.py \
  --ava build-release/ava \
  --benchmark-helper build-release/tests/ava_backend_benchmark_helper \
  --suite smoke --runs 1 \
  --output /tmp/ava-backend-smoke.json \
  --report /tmp/ava-backend-smoke.md
```

Use `--suite baseline --runs 5` for machine-specific baseline evidence. Use `--suite stress --runs 10` only for opt-in detailed work; it includes larger catalog/session fixtures and more one-shot plugin calls. `--fake-provider` may record the local fake-provider binary alongside a run, although the current harness does not need a provider. `--memory-helper` defaults to the existing [`scripts/benchmark-memory.py`](../../scripts/benchmark-memory.py), and `--sample-plugin` defaults to the repository's todo plugin.

The C++ helper is optional. When omitted, every helper-dependent family remains in JSON with a structured `unsupported` result rather than disappearing. Supplying it is required for deterministic dispatch, registry, plugin, and session measurements.

## Methodology

- Python uses `time.monotonic_ns`; the C++ helper uses `std::chrono::steady_clock`.
- Each process gets an isolated empty project plus isolated `HOME`, XDG directories, and `TMPDIR`. Credential-looking and proxy environment variables are removed, and AVA runs with `--offline`.
- JSON records the UTC date, exact commit and dirty state, host OS/kernel/CPU/RAM, compiler and CMake build type when discoverable, binary path and size, exact commands, every sample, repetitions, median, nearest-rank p95, and maximum.
- Linux idle RSS reuses the PTY and `/proc/*/smaps_rollup` process-tree logic in `benchmark-memory.py`. Other platforms report this family as unsupported.
- Session fixtures are generated through `SessionStore` with current valid records. Plugin measurements reuse the sample todo plugin. Manifest discovery asserts that no child becomes waitable.
- The current plugin application path is one-shot. First and repeated plugin results are labeled `current_one_shot`; they are not claims about persistent worker warmth.
- The current registry materializes full schemas and uses linear lookup. Catalog measurements say so, while the nonexistent selective router is explicitly unsupported.
- A reproducible machine-cold start would require cache controls the harness does not assume. `cold_startup` is therefore unsupported; `warm_startup` records full offline RPC initialization after one warm-up.

## Interpretation and CI

The JSON is authoritative; Markdown is only a readable rendering. Compare distributions and inspect all samples before discussing a delta. Keep the exact commit, dirty state, host, compiler, CMake build type, and binary identity with any claim. Re-run on the same quiet host and build configuration.

The registered `ava_benchmark.smoke` CTest uses broad reliability ceilings only: completion within a practical timeout, no plugin child left waitable, no child from manifest discovery, and no catastrophic RSS or repeated-call growth. It does **not** gate pull requests on microbenchmark deltas. Detailed baseline and stress suites are intentionally opt-in and unregistered.

On non-Linux hosts, timing/helper measurements can still run, but procfs idle RSS is explicitly unsupported. `getrusage` peak-RSS units are normalized by the helper on Linux and macOS; other ports should verify platform semantics before making memory claims.
