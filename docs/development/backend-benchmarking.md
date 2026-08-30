# Backend Benchmarking

AVA's M0 backend harness records the pre-modernization architecture with AVA in offline mode, an allowlisted child environment, and repository-owned local fixtures. Results are observations of one exact machine, build, source tree, and set of artifact bytes. They are not portable performance claims.

The harness is not an operating-system network sandbox. AVA runs with `--offline`, and the repository-owned todo plugin fixture performs no network I/O, but an arbitrary replacement plugin is not thereby prevented from using the network.

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

`--suite smoke` requires an executable `--benchmark-helper`; argument validation fails before any benchmark runs if it is absent. The registered smoke therefore exercises native dispatch, plugin cleanup, manifest discovery without process spawn, cancellation acknowledgement, session opening, and repeated-call memory. An unsupported result for one of those required helper seams fails the smoke checks rather than passing silently.

Use `--suite baseline --runs 5` for machine-specific baseline evidence. Use `--suite stress --runs 10` only for opt-in detailed work; it includes larger catalog/session fixtures and more one-shot plugin calls. Baseline and stress may omit the C++ helper, in which case helper-backed families remain present as structured `unsupported` entries and smoke checks are not applied. `--fake-provider` records a supplied local fake-provider artifact, although the harness does not execute a provider. `--memory-helper` defaults to [`scripts/benchmark-memory.py`](../../scripts/benchmark-memory.py), and `--sample-plugin` defaults to the repository's todo plugin.

## Methodology

- Python uses `time.monotonic_ns`; the C++ helper uses `std::chrono::steady_clock`.
- Processes launched directly by the backend harness receive a fresh environment rather than a filtered copy of the host environment. Its complete allowlist is a fixed `PATH` (`/usr/local/bin:/usr/bin:/bin`), `LANG`/`LC_ALL=C.UTF-8`, isolated `HOME`/XDG/`TMPDIR`, `TERM=dumb`, `NO_COLOR=1`, disabled Git prompting, AVA offline/debug-suppression controls, and `AVA_SESSION_TITLES=off`. Host `PYTHONPATH`, loader variables, askpass/Kerberos/Docker/database/provider/base-URL/credential variables, proxies, and arbitrary variables do not survive. The memory helper derives its nested AVA environment from that already-allowlisted map and applies only its fixed PTY terminal settings.
- SHA-256 (computed with Python's standard library), byte size, path, mtime, and executable mode identify AVA, the benchmark helper, a supplied fake provider, the memory helper, the sample plugin manifest and entrypoint, and the benchmark script. AVA also gets a bounded `--version` probe; plugin manifest identity records its ID/version/API and entrypoint declaration.
- Source identity records repository path, exact commit, committed tree, and dirty state separately from artifact byte identity. Build metadata records the nearest CMake cache/source root, build type, compiler ID/version, sanitizer/TSan/debug/libcwd settings when present, and relevant C++ flags.
- Build provenance and freshness remain best effort. Cache paths and mtimes can show a mismatch, but the harness does not claim that an executable embeds the recorded Git commit or prove that the executable was produced from that source state. Build the requested targets immediately before collecting evidence.
- Linux idle RSS reuses the PTY and `/proc/*/smaps_rollup` process-tree logic in `benchmark-memory.py`. Each backend run value is the maximum RSS among that run's raw snapshots, so the result-level maximum is supported by retained observations. The backend JSON retains the complete memory-helper output, including warm-up, every measured snapshot, process names, PSS/USS/swap/process/thread details, and the helper summaries.
- The repeated-call C++ seam reads current Linux resident RSS from `/proc/self/statm` before and after the direct no-op call loop, converting resident pages with the runtime page size. Peak `getrusage` high-water readings remain separate details. macOS uses an explicitly named peak-high-water delta fallback; other platforms report that seam as unsupported.
- The repeated-call RSS delta is a narrow direct-call seam. It is not evidence of product-wide memory stability, long-lived worker behavior, allocator reclamation, or process-tree stability.
- Session fixtures are generated through `SessionStore` with current valid records. Plugin measurements reuse the sample todo plugin. Manifest discovery asserts that no child becomes waitable.
- The current plugin application path is one-shot. First and repeated plugin results are labeled `current_one_shot`; they are not claims about persistent worker warmth.
- Native registry lookup builds the current built-in registry before timing, chooses its final registered entry, and times exact linear lookup for the requested iterations. It emits `ns_per_lookup`, target name, and entry count; schema materialization is not part of that measurement.
- Synthetic catalog measurements use a deliberately composite timed boundary: registry construction, full schema materialization, and lookup of the final synthetic entry. The nonexistent selective router remains explicitly unsupported.
- A reproducible machine-cold start would require cache controls the harness does not assume. `cold_startup` is therefore unsupported; `warm_startup` records full offline RPC initialization after one warm-up.

## Interpretation and CI

The JSON is the authoritative harness output; Markdown is only a readable rendering. Compare distributions and inspect all raw samples before discussing a delta. Keep source identity, artifact hashes, host, compiler, CMake source root, and build settings with any claim, and rerun on the same quiet host.

`parameters.exact_command` records the benchmark script's `sys.argv`. An external wrapper such as `/usr/bin/time` is outside that argument vector, so wrapper-only observations are not represented by the JSON and must not be described as part of its recorded evidence.

The registered `ava_benchmark.smoke` CTest uses broad reliability ceilings only: completion within a practical timeout, required helper seams measured, no plugin child left waitable, no child from manifest discovery, and no catastrophic RSS delta. It does **not** gate pull requests on microbenchmark deltas. Detailed baseline and stress suites are intentionally opt-in and unregistered.

On non-Linux hosts, helper timing measurements can still run, but procfs idle process-tree RSS is explicitly unsupported. The macOS repeated-call result is a labeled peak-high-water fallback rather than current RSS; a platform with neither supported behavior reports `unsupported`, which intentionally does not satisfy the required smoke seam.
