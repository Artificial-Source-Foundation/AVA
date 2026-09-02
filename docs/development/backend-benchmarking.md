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

## M1 process-supervision extension

M1 extends the M0 harness; it does not reinterpret M0 artifacts. `smoke`, `baseline`, and `stress` still emit `ava.backend-benchmark.v2`, and the historical M0 JSON remains validated by the harness self-test. The additional suites are:

- `process-smoke`: the default registered catastrophic-correctness CTest, with one helper invocation per run;
- `process-baseline`: opt-in raw distributions suitable for a same-host family migration pair.

They emit `ava.backend-benchmark.v3`. New helper cases emit one `ava.backend-benchmark-helper.v2` object. Every helper observation is retained as `{run, observation, value, metrics, checks}`; observations in one helper invocation remain visibly correlated and are not described as independent repetitions. Primary and numeric metric summaries use median, nearest-rank p95, and maximum.

The ordered result set always contains application startup/RSS, scope construction, first and warm spawn commit, concurrent 1/8/64 records, natural/leader-first/TERM-refusal settlement, shared-budget shutdown64, pidfd and forced POSIX fallback monitor 1/8/64, and Curl/Plugin/MCP/LSP/Bash lifecycle results. Unsupported entries stay in place with a closed reason such as `source_architecture_absent`, `caller_not_migrated`, `pidfd_unavailable`, or `fixture_unavailable`; the harness never substitutes zero or synthetic measurements.

### Build and quick process smoke

Use one build recipe for both members of a pair. For example:

```sh
cmake -S . -B /tmp/ava-process-build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/ava-process-build --target \
  ava ava_backend_benchmark_helper ava_fake_process_child \
  ava_fake_mcp_server ava_fake_lsp_server
ctest --test-dir /tmp/ava-process-build --output-on-failure \
  -R '^ava_benchmark\.(process_harness_self_test|process_smoke)$'
```

The equivalent direct command is:

```sh
runtime_reference=$(git rev-parse --verify \
  'dd4460348658c4127606db679da873cbf3dba274^{commit}')
git diff --exit-code "$runtime_reference" -- \
  src CMakeLists.txt cmake config.h.in
scripts/benchmark-backend.py \
  --ava /tmp/ava-process-build/ava \
  --benchmark-helper /tmp/ava-process-build/tests/ava_backend_benchmark_helper \
  --fake-process-child /tmp/ava-process-build/tests/ava_fake_process_child \
  --fake-mcp-server /tmp/ava-process-build/tests/ava_fake_mcp_server \
  --fake-lsp-server /tmp/ava-process-build/tests/ava_fake_lsp_server \
  --memory-helper scripts/benchmark-memory.py \
  --sample-plugin examples/plugins/todo \
  --runtime-reference "$runtime_reference" \
  --suite process-smoke --runs 1 \
  --output /tmp/ava-process-smoke.json \
  --report /tmp/ava-process-smoke.md
```

A build that contains the M1 process target but omits its fake child cannot pass process smoke: process cases return `fixture_unavailable`, and required-case checks fail. A source tree that genuinely predates the process architecture instead returns `source_architecture_absent` for every neutral supervisor/monitor result; this is the explicit portability state, not a performance result. Family lifecycle drivers still compile and measure there.

### Fresh-worktree carrier and paired baseline recipe

`971327fb66fc372f5828c5f5967e118d9374f9da` is an **instrumentation carrier**, not an M1 runtime. Its production runtime paths are byte-for-byte the source baseline `c94ac863141975806bbab52e950a2f2499108b65`. The following commands create an honest carrier cohort and prove that invariant. Builds stay outside both worktrees.

Resolve every benchmark instrumentation commit to a reviewed full object ID before leaving the benchmark branch. Never derive an instrumentation commit from `HEAD` after checking out a later production branch. Both integrated instrumentation commits are pinned below:

```sh
instrumentation_base=dd7cb260d58beb6f2d69bc07dc0bb604d65bd3ef
instrumentation_integrity_commit=789b100c728bd9f95a68324e8eaa8012d9b09cdb
test "$(git rev-parse --verify "$instrumentation_base^{commit}")" = \
  "$instrumentation_base"
test "$(git rev-parse --verify "$instrumentation_integrity_commit^{commit}")" = \
  "$instrumentation_integrity_commit"

: "${AVA_FAMILY_MIGRATION_COMMIT:?set the reviewed full family-migration commit ID}"
family_migration_commit=$AVA_FAMILY_MIGRATION_COMMIT
test "$(printf %s "$family_migration_commit" | wc -c)" -eq 40
test "$(git rev-parse --verify "$family_migration_commit^{commit}")" = \
  "$family_migration_commit"
after_src=/tmp/ava-process-after-src
git worktree add --detach "$after_src" "$family_migration_commit"
test "$(git -C "$after_src" rev-parse HEAD)" = "$family_migration_commit"

before_src=/tmp/ava-process-before-src
before_build=/tmp/ava-process-before-build
rm -rf "$before_build"
git worktree add --detach "$before_src" 971327fb66fc372f5828c5f5967e118d9374f9da
git -C "$before_src" cherry-pick \
  "$instrumentation_base" "$instrumentation_integrity_commit"
git -C "$before_src" diff --exit-code \
  c94ac863141975806bbab52e950a2f2499108b65 HEAD -- \
  src CMakeLists.txt cmake config.h.in
test -z "$(git -C "$before_src" status --porcelain --untracked-files=normal)"

cmake -S "$before_src" -B "$before_build" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release
cmake --build "$before_build" --target \
  ava ava_backend_benchmark_helper ava_fake_mcp_server ava_fake_lsp_server

"$after_src/scripts/benchmark-backend.py" \
  --measured-source-root "$before_src" \
  --ava "$before_build/ava" \
  --benchmark-helper "$before_build/tests/ava_backend_benchmark_helper" \
  --fake-mcp-server "$before_build/tests/ava_fake_mcp_server" \
  --fake-lsp-server "$before_build/tests/ava_fake_lsp_server" \
  --memory-helper "$before_src/scripts/benchmark-memory.py" \
  --sample-plugin "$before_src/examples/plugins/todo" \
  --runtime-reference c94ac863141975806bbab52e950a2f2499108b65 \
  --run-order before_then_after \
  --suite process-baseline --runs 5 \
  --output /tmp/ava-process-before.json \
  --report /tmp/ava-process-before.md
```

The carrier intentionally has no `ava_fake_process_child` target, so no such argument appears in its command. Its neutral process cases are structured unsupported; its five family cases are real `legacy_local` lifecycle measurements. Keep this worktree and build until the after cohort is complete: their fake MCP/LSP executables and sample plugin are the common fixture bytes for both runs.

The fresh after worktree is created and pinned before either cohort is collected so one reviewed harness executes both runs. Using identical harness bytes is required to keep the benchmark contract and fixture hash fixed; passing distinct `--measured-source-root` values is independently required so checkout, runtime-reference, family-source, and CMake provenance describe the binary under measurement rather than the harness checkout.

For the after cohort, use the identical generator, build type, compiler, feature flags, run count, and host boot. The migration commit changes only that family's declaration in `tests/backend_benchmark_authorities.cmake` to `supervised` while adapting its driver; authority is never supplied on the CMake command line. Build the application, helper, and process child. Although helper dependencies may also rebuild fake servers, do not use those independently built copies for evidence:

```sh
after_build=/tmp/ava-process-after-build
rm -rf "$after_build"
cmake -S "$after_src" -B "$after_build" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release
cmake --build "$after_build" --target \
  ava ava_backend_benchmark_helper ava_fake_process_child

"$after_src/scripts/benchmark-backend.py" \
  --measured-source-root "$after_src" \
  --ava "$after_build/ava" \
  --benchmark-helper "$after_build/tests/ava_backend_benchmark_helper" \
  --fake-process-child "$after_build/tests/ava_fake_process_child" \
  --fake-mcp-server "$before_build/tests/ava_fake_mcp_server" \
  --fake-lsp-server "$before_build/tests/ava_fake_lsp_server" \
  --memory-helper "$before_src/scripts/benchmark-memory.py" \
  --sample-plugin "$before_src/examples/plugins/todo" \
  --runtime-reference "$family_migration_commit" \
  --run-order before_then_after \
  --suite process-baseline --runs 5 \
  --output /tmp/ava-process-after.json \
  --report /tmp/ava-process-after.md \
  --compare-to /tmp/ava-process-before.json \
  --comparison-output /tmp/ava-process-comparison.json

rm -rf "$after_build" "$before_build"
git worktree remove "$after_src"
git worktree remove "$before_src"
```

At this source state Curl is source-owned `supervised`; Plugin, MCP, LSP, and Bash remain `legacy_local`. Stale cache entries and command-line values are removed during configuration and cannot assert migration. Curl's driver owns an explicit Supervisor/application scope, destroys the transport, performs bounded shutdown, and emits `supervisor_record_finished=true`, `supervisor_settlement_once=true`, and `cleanup_scope=managed_group` only after verifying exactly one finished Curl record with complete cleanup and `live_records == 0`. Changing any remaining declaration to `supervised` without adapting its driver still reaches `refuse_false_supervised_claim` and returns structured `caller_not_migrated`.

### Driver boundaries and cleanup evidence

Neutral process modes use only the public Supervisor and narrow test telemetry APIs:

- idle scope constructs `Supervisor` plus application scope, proving no monitor, no live record, no Linux thread/immediate-child delta, and recording current RSS delta;
- spawn commit includes exact-environment mint, reservation, launch, and confirmed exec; warm and concurrent cases retain every child observation;
- natural, leader-first descendant, TERM refusal, and shutdown64 time their stated settlement boundaries and verify expected reason, cleanup, endpoint EOF where applicable, and settlement count one;
- monitor modes use ready idle children and `CLOCK_PROCESS_CPUTIME_ID`, normalized to `cpu_ns_per_wall_second`. Automatic mode requires actual pidfd selection and zero periodic fallback probes. Forced fallback reaches the logarithmic buckets and one-second cap before its fixed hold.

After every measured process driver, endpoints and consumers are closed before Supervisor destruction, retained handles are waited, records are Finished with settlement count one, `live_records` is zero, shutdown is complete, and monitor resources are gone. Only then does the helper use `waitpid(-1, WNOHANG) == ECHILD` as an **immediate-child guard**. Descendant cleanup is evidenced by Supervisor settlement and inherited-endpoint EOF; the immediate-child guard is never presented as descendant evidence. Legacy family checks are explicitly labeled `immediate_children_only`.

The fixed family boundaries are one stdlib-Python loopback Curl request; todo sample plugin initialize/call/shutdown; fake MCP initialize/tools-list/shutdown; fake LSP initialize/diagnostics/destruction; and a benign direct-argv command through normal sealed Bash planning and execution. Curl now measures the supervised managed-group lifecycle; the other four retain their legacy immediate-child boundaries. All retain content-free compatibility checks only.

### Provenance, redaction, and comparison

V3 records the measured source root and checkout commit/tree/dirty state; runtime reference and production-path equality; the independent harness repository, commit/tree/dirty state, contract, and script hash; family tree/blob IDs from the measured root; CMake generator/version/cache hash/source root/build type/features; compiler path/hash/ID/version/flags; every used binary/script/fixture/plugin hash, size, mode, and mtime; OS/kernel/machine/CPU/count/RAM/page/Python; hashed boot ID; limits; monotonic resolution; start/end load; and exact driver commands and scale parameters. Paths occur only under provenance or artifacts. Provenance remains best effort: binaries do not embed a verified source commit.

Results and samples contain no PID, PGID, raw owner ID, descriptor, argv/command, executable/cwd path, URL, environment value, child output, protocol frame, prompt, or tool content. Redaction checks tokenize composite sample keys, so names such as `child_pid`, `request_url`, and `command_argv` are rejected without rejecting closed aggregates such as `pidfd_successes`, `stdout_bytes`, `record_count`, and `endpoint_eof`. Primary samples must be non-negative. Helper checks and metrics accept only numbers, booleans, and validated closed labels. Malformed, truncated, multi-object, dynamically reasoned, or content-bearing helper output is rejected.

Optional comparison output uses `ava.backend-benchmark-comparison.v1`. It recomputes summaries from raw samples and refuses cohorts unless host and hashed boot, build recipe, compiler, non-authority features, units/boundaries, fixture hashes, and harness contract match. A family result is comparable only for `legacy_local` to `supervised` authority when both cohorts contain every required compatibility check and every check is true. Reports never require M1 to be faster. Investigation triggers are latency greater than both 20% and 100 microseconds, RSS greater than both 20% and 4 MiB, and monitor CPU greater than both 25% and 5 ms/s. They are non-gating; a repeatable claim requires a second pair collected in reversed order.
