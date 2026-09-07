# Backend Performance Baseline

This document is the Milestone M0.3 evidence snapshot for AVA's backend
modernization. It records the architecture before performance-sensitive runtime
changes. Results are observations of one exact host, build, source tree, and set of
artifact bytes; they are not portable claims.

The complete machine-readable result is
[`backend-performance-baseline-2026-08-30.json`](backend-performance-baseline-2026-08-30.json).
Harness methodology and interpretation rules are in
[Backend benchmarking](../development/backend-benchmarking.md).

## Evidence identity

| Field | Value |
| --- | --- |
| Schema | `ava.backend-benchmark.v2` |
| Date | 2026-08-30 (`2026-08-30T20:53:16.694084+00:00` in the artifact) |
| Runtime-equivalent commit | `971327fb66fc372f5828c5f5967e118d9374f9da` |
| Committed tree | `80006e97c46767768532fd9f3941d110ec7941d7` |
| Git state | clean |
| Operating system | Linux x86-64, Ubuntu 24.04 userland, kernel `7.0.0-29-generic`, glibc 2.39 |
| CPU | Intel Core i9-10850K at 3.60 GHz, 20 logical CPUs |
| Memory | 33,533,427,712 bytes (31.23 GiB) |
| Compiler | GCC `13.3.0` (`/usr/bin/c++`) |
| Build | CMake `Release`; sanitizers, TSan, debug, and libcwd off |
| Python | 3.12.3 |
| AVA binary | 19,996,328 bytes (19.07 MiB) |
| AVA SHA-256 | `9f22ae812dd8fc2dd578d596d6d553e429b7941b6f22983abd06c1b3e1b3fe8d` |
| Benchmark helper SHA-256 | `4d7c8fe1214e422cd3cbcb5b054401cbbdbd237fad97cccdc0a9994a1b51865a` |
| Repetitions | 5 measured runs per result; harness-specific warm-up where documented |
| Isolation | Empty project; fixed allowlisted HOME/XDG/TMPDIR/PATH/locale environment; AVA offline |
| Clocks | Python `time.monotonic_ns`; C++ `std::chrono::steady_clock` |

The JSON also records SHA-256, size, path, mode, and mtime for the fake provider,
memory helper, benchmark script, and sample plugin manifest/entrypoint, plus bounded
AVA version output and plugin manifest identity.

`971327fb` contains documentation and benchmark-only code but no production runtime
change relative to the starting source commit `c94ac8631419`. Build provenance remains
explicitly **best-effort and unverified**: the CMake source root matches the recorded
repository, the executable resides in that build tree, and cache/artifact mtimes are
recorded, but AVA does not embed a verified Git commit. Artifact hashes identify the
exact measured bytes without claiming more.

## Reproduction

```sh
cmake --preset release
scripts/build.sh --build-dir build-release \
  --target ava ava_backend_benchmark_helper ava_fake_provider_server

scripts/benchmark-backend.py \
  --ava build-release/ava \
  --benchmark-helper build-release/tests/ava_backend_benchmark_helper \
  --fake-provider build-release/tests/ava_fake_provider_server \
  --suite baseline --runs 5 \
  --output /tmp/ava-backend-m0-baseline-2026-08-30-v2.json \
  --report /tmp/ava-backend-m0-baseline-2026-08-30-v2.md
```

## Baseline results

Values below are converted for readability. The JSON retains raw units, every result
sample, every underlying idle-RSS snapshot, exact subcommands, details, p95, and
maxima. Idle RSS uses each run's maximum observed `/proc` snapshot, so the result-level
maximum is an observed sample rather than a maximum of reduced medians.

| Measurement | Median | p95 | Maximum | Interpretation |
| --- | ---: | ---: | ---: | --- |
| Warm offline RPC startup and EOF shutdown | 2.023 ms | 2.094 ms | 2.094 ms | One unrecorded warm-up; no provider request |
| Idle AVA RSS | 15.21 MiB | 15.39 MiB | 15.39 MiB | Maximum process-tree RSS snapshot per run; one AVA process, no optional worker |
| Native `read_file` dispatch | 29.57 µs/call | 30.99 µs/call | 30.99 µs/call | Actual built-in dispatcher and a small local file |
| Native built-in registry lookup | 14.30 ns/lookup | 16.77 ns/lookup | 16.77 ns/lookup | Exact lookup of the final registered built-in; registry construction excluded |
| First plugin call | 11.270 ms/call | 11.356 ms/call | 11.356 ms/call | Current one-shot process, not a persistent worker |
| Repeated plugin calls | 11.258 ms/call | 11.281 ms/call | 11.281 ms/call | Every call starts a fresh process |
| Discover 100 unused plugin manifests | 2.906 ms | 2.944 ms | 2.944 ms | Zero waitable immediate children before and after discovery |
| Current catalog, 100 entries | 0.094 ms | 0.098 ms | 0.098 ms | Composite registry construction, full schemas, and last lookup |
| Current catalog, 500 entries | 0.586 ms | 0.594 ms | 0.594 ms | Same current nonselective composite path |
| Current catalog, 1,000 entries | 1.902 ms | 2.052 ms | 2.052 ms | Same current nonselective composite path |
| 10,000 short test-tool calls | 30.43 ns/call | 30.55 ns/call | 30.55 ns/call | Direct registered executor; not end-to-end model dispatch |
| 10,000 pre-cancelled file calls | 8.142 µs/call | 8.295 µs/call | 8.295 µs/call | Cancellation checked by the built-in dispatch path |
| Open and fully load 1,000 events | 3.623 ms | 3.709 ms | 3.709 ms | Fixture creation excluded from timed interval |
| Open and fully load 10,000 events | 35.691 ms | 36.244 ms | 36.244 ms | Current full JSONL materialization |
| Open and fully load 100,000 events | 363.087 ms | 367.658 ms | 367.658 ms | Current full JSONL materialization |
| Browse metadata for 100 one-entry sessions | 1.831 ms | 1.989 ms | 1.989 ms | Current list path; not a lazy long-history index |
| Internal pre-cancel acknowledgement | 10.426 µs | 10.822 µs | 10.822 µs | Pre-cancelled bounded session read, not UI/provider round trip |
| Plugin child cleanup | 10.742 ms | 10.772 ms | 10.772 ms | Start then shutdown one leader; descendants not exercised |
| 10,000 short-call current RSS delta | 64 KiB | 64 KiB | 64 KiB | `/proc/self/statm` after-minus-before; peak high-water delta was 0 KiB |

## Requested results that do not exist yet

The harness keeps these entries in the versioned result schema rather than silently
omitting or inventing values:

- **Machine-cold startup:** `unsupported`. Reproducibly evicting executable and
  filesystem page cache requires host controls the harness does not assume. A fresh
  HOME is not mislabeled as a machine-cold start.
- **Built-in no-op:** `unsupported`. AVA has no production no-op tool; the direct
  10,000-call test tool is the closest current seam.
- **Warm persistent plugin communication:** unavailable before M2. The first and
  repeated values above both measure one-shot startup, initialization, call, and
  shutdown.
- **Selective tool routing:** unavailable before M3. Catalog values measure current
  full schema materialization and lookup, not relevance ranking.
- **Indexed/lazy large-session opening:** unavailable before M4. The baseline
  deliberately measures the current full-load path.
- **End-to-end cancellation and descendant cleanup latency:** not yet isolated by the
  M0 micro-harness. Existing whole-process cancellation and bash process-group cleanup
  CTests remain the correctness evidence; M1 must add supervisor-level timing.

## Evaluation against initial aspirations

| Aspiration | M0 evidence | Evaluation |
| --- | --- | --- |
| Cold startup under 100–150 ms | No reproducible machine-cold result | Not evaluated; do not substitute warm startup |
| Idle RSS under 40–60 MiB | 15.39 MiB maximum observed snapshot | Met on this Release host with no optional workers |
| Native routing overhead under 1 ms p95 | File dispatch 30.99 µs; exact registry lookup 16.77 ns | Current native seams are below 1 ms; M3 ranking remains unimplemented |
| Warm plugin communication under 2–5 ms p95 | No persistent worker; one-shot p95 11.28–11.36 ms | Not met by current one-shot architecture; M2 owns the comparison |
| Unused plugins use zero processes | Discovery source has no spawn; zero waitable immediate children in the 100-manifest run | Met for manifest discovery; the check is not a system-wide process census |
| Unused MCP/LSP use zero processes | Static map shows LSP lazy; MCP discovery starts configured servers | Not generally met for configured MCP discovery |
| Internal cancellation acknowledgement under 50 ms | 10.822 µs p95 for a pre-cancelled session read | Micro seam met; end-to-end claim not established |
| Child cleanup under 250 ms | 10.772 ms p95 for one plugin leader | Micro seam met; descendants and all spawn families not established |
| Stable memory after 10,000 short calls | 64 KiB current-RSS delta in every direct helper run | No unbounded growth observed in this narrow seam; product-wide stability is not established |
| Large session opens index/window, not full transcript | 100,000-event full load p95 367.658 ms | Not implemented; time scales approximately with record count |

## Calibrated regression policy

M0 does not make noisy microbenchmarks block pull requests. The registered smoke
requires its helper-backed seams and checks only correctness plus catastrophic ceilings:

- startup completes within 30 seconds;
- Linux idle RSS remains below 4 GiB;
- direct-call current-RSS delta remains below 512 MiB;
- each helper repetition finishes within 30 seconds;
- plugin cleanup leaves no waitable immediate child; and
- manifest discovery starts no waitable immediate child.

For local same-host comparisons, use the five-run baseline distributions above and
investigate a repeatable regression greater than 20% in median or p95. That 20% is an
investigation trigger, not an automatic compatibility failure. Each later milestone
must record both its absolute result and relative delta against this artifact.

## M0 limitations

- Release and BetaTest/debug results are not interchangeable.
- Shared-host load, CPU frequency, kernel cache state, and filesystem state add noise.
- The fixed environment and `--offline` make the repository-owned run deterministic;
  they are not an operating-system network sandbox for an arbitrary replacement plugin.
- `waitpid(-1, WNOHANG)` proves no waitable immediate helper child; it does not prove
  absence of detached descendants. Plugin and MCP leaders can exit before a same-group
  descendant, which current shutdown does not subsequently verify or kill.
- A current-RSS before/after delta cannot prove allocator reclamation or stability of
  complete AVA sessions, workers, or process trees.
- Session fixtures contain small messages and no large reasoning or tool payloads.
- The fake provider artifact is identified but not executed by this M0 harness;
  provider-stream cancellation remains covered by functional tests, not this timing
  table.
