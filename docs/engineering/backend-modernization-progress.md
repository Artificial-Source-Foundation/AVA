# Backend Modernization Progress

This ledger is the source of truth for AVA's backend modernization. A task is marked
`completed` only when its acceptance evidence is present in the repository and the
recorded commands have passed.

## Project context

- Implementation branch: `backend-modernization`
- Starting commit: `c94ac8631419`
- Started: 2026-08-30
- Coordinator: principal implementation agent
- Working-tree note: the branch was created while unrelated, pre-existing changes were
  present under `docs/core/`, `src/ava/agent/`, `src/ava/app/`, and existing tests.
  Those changes were preserved separately on `primary-agent-selection`; they are not
  owned by this project and are not included in its milestone commits.
- Dependency policy: no new production dependency without the review required by the
  project brief.

## Status legend

`not started` · `active` · `blocked` · `completed`

## Milestones

| Task | Description | Owner | Status | Files changed | Tests / benchmark evidence | Commit | Remaining risks |
|---|---|---|---|---|---|---|---|
| M0.1 | Map backend ownership, process sites, lifecycles, cancellation, and retained data | Architecture Cartographer + coordinator | completed | `docs/engineering/backend-current-state.md` | Static inventory plus source-link and structure checks | `3d4349ef` | Re-verify if excluded concurrent work is later integrated |
| M0.2 | Reproducible startup, RSS, dispatch, plugin, catalog, session, cancellation, cleanup, and memory harness | Performance Engineer | completed | `scripts/benchmark-backend.py`; `tests/backend_benchmark_helper.cpp`; `tests/backend_benchmark_test.py`; `tests/CMakeLists.txt`; `docs/development/backend-benchmarking.md` | Release and BetaTest `ava_benchmark.harness_self_test` + `ava_benchmark.smoke`; five-run baseline schema v2 | `8fab13fe`, `971327fb` | Machine-cold startup, persistent-worker warmth, selective routing, and indexed sessions remain explicitly unsupported before their owning milestones |
| M0.3 | Establish honest machine-recorded baseline and calibrated budgets | Performance Engineer + coordinator | completed | `docs/engineering/performance-baseline.md`; `docs/engineering/backend-performance-baseline-2026-08-30.json` | Release baseline, 5 runs, exact artifacts/hashes/source/build/host identity, raw RSS snapshots, median/p95/max | `6999441f` | Same-host 20% investigation trigger is non-gating; provenance is best-effort and cold-cache control unavailable |
| M0.4 | Add lightweight CI performance/reliability smoke | Performance Engineer + coordinator | completed | `tests/CMakeLists.txt`; harness smoke checks | Default CTest registration; Release and BetaTest smoke pass; helper omission has a negative test | `8fab13fe`, `971327fb` | Coarse catastrophic ceilings intentionally do not gate microbenchmark deltas |
| M1.1 | Define process ownership identifiers and supervisor API | Process Lifecycle Architect + coordinator | completed | [`docs/engineering/process-supervisor-adr.md`](process-supervisor-adr.md) | Accepted ADR; source Markdown links, documentation structure, and textual diff checks passed | pending | Implementation evidence remains in M1.2–M1.5 |
| M1.2 | Implement managed Linux/POSIX process groups and tree cleanup; retain the future Windows contract without a support claim | Process Lifecycle Engineer | active | Design: [`docs/engineering/process-supervisor-adr.md`](process-supervisor-adr.md) | Reservation/gate/group/reap contract accepted; implementation and platform tests active | pending | `setsid` escape and unkillable-process limits are explicit; Windows Job Objects remain deferred |
| M1.3 | Migrate every process-spawning subsystem | Process Lifecycle Engineer + subsystem owners | not started | Migration matrix: [`docs/engineering/process-supervisor-adr.md`](process-supervisor-adr.md#api-and-authority-contract) | Five-wave migration and compatibility plan accepted; implementation evidence pending | pending | Browser/editor and secure bash/Mermaid adoption require dedicated migration tests |
| M1.4 | Add bounded, secret-safe process diagnostics | Process Lifecycle Engineer | not started | Diagnostic contract: [`docs/engineering/process-supervisor-adr.md`](process-supervisor-adr.md#diagnostics-output-and-deadline-ownership) | Closed content-free schema and bounds accepted; implementation evidence pending | pending | Trace adaptation must preserve aliases and aggregate-only support export |
| M1.5 | Add leak, cancellation, timeout, descendant, and shutdown tests | Process Test Engineer | active | Verification plan: [`docs/engineering/process-supervisor-adr.md`](process-supervisor-adr.md#migration-and-verification) | Exact-reap, descendant, environment-canary, PTY, platform, and benchmark plan accepted; fixtures/tests active | pending | POSIX CI and PTY coverage remain required; Windows tests follow future support |
| M2.1 | Document and compatibility-test existing plugin protocol | Plugin Runtime Engineer | not started | pending | pending | Legacy protocol ambiguity |
| M2.2 | Implement lazy, persistent, scoped plugin worker manager | Plugin Runtime Engineer | not started | pending | pending | Concurrency, restart, and trust-scope isolation |
| M2.3 | Preserve one-shot plugin compatibility | Plugin Runtime Engineer | not started | pending | pending | Legacy behavior parity |
| M2.4 | Add versioned lifecycle/resource manifest settings | Plugin Runtime Engineer | not started | pending | pending | Manifest compatibility and safe defaults |
| M2.5 | Add worker health/restart/failure lifecycle tests | Plugin Runtime Engineer | not started | pending | pending | Process leakage and malformed protocol handling |
| M2.6 | Benchmark cold/warm workers, eviction, crashes, and runtime cost | Performance Engineer | not started | pending | pending | Runtime availability on test host |
| M3.1 | Build normalized installed/available/selected tool catalog | Tool Routing Engineer | not started | pending | pending | Stable IDs and source-version invalidation |
| M3.2 | Implement deterministic native lexical/fuzzy search | Tool Routing Engineer | not started | pending | pending | Recall versus bounded prompt cost |
| M3.3 | Define always-visible base tool set | Tool Routing Engineer + coordinator | not started | pending | pending | Workflow compatibility |
| M3.4 | Add bounded built-in tool-discovery tool | Tool Routing Engineer | not started | pending | pending | Permission-safe advertising |
| M3.5 | Add request-specific selection and catalog budgets | Tool Routing Engineer | not started | pending | pending | Provider request compatibility |
| M3.6 | Preserve normal direct-call fallback | Tool Routing Engineer | not started | pending | pending | Multi-turn discovery reliability |
| M3.7 | Add deterministic routing evaluation/scale suite | Performance Engineer + Tool Routing Engineer | not started | pending | pending | Representative query corpus |
| M4.1 | Audit storage, compaction, recovery, migrations, and RAM behavior | Session Engineer | not started | pending | pending | Existing format invariants |
| M4.2 | Define bounded active session state | Session Engineer | not started | pending | pending | Context assembly compatibility |
| M4.3 | Add versioned on-disk session index | Session Engineer | not started | pending | pending | Crash consistency and migration |
| M4.4 | Add pagination, lazy loading, metadata search, and rebuild | Session Engineer | not started | pending | pending | Large-branch behavior |
| M4.5 | Add content-addressed large-payload storage | Session Engineer | not started | pending | pending | Reference-safe GC and path safety |
| M4.6 | Persist compact versioned turn receipts | Session Engineer | not started | pending | pending | Reproducibility without secrets |
| M4.7 | Strengthen recovery, migration, and writer authority | Session Engineer | not started | pending | pending | Partial records and stale indexes |
| M4.8 | Benchmark large sessions and bounded RAM | Performance Engineer + Session Engineer | not started | pending | pending | 100k-event fixture cost |
| M5.1 | Extend tool capability/resource metadata | Scheduler Engineer | not started | pending | pending | Dynamic resource declaration fidelity |
| M5.2 | Implement symlink-safe conflict detection | Scheduler Engineer | not started | pending | pending | TOCTOU and conservative ordering |
| M5.3 | Add fair bounded execution pool and limits | Scheduler Engineer | not started | pending | pending | Cancellation and starvation |
| M5.4 | Durably persist each completed parallel result | Scheduler + Session Engineers | not started | pending | pending | Provider ordering and stale generations |
| M5.5 | Guarantee exactly-once tool-call settlement | Scheduler Engineer | not started | pending | pending | Crash/replay semantics |
| M5.6 | Add bounded loop/repetition protection | Scheduler Engineer | not started | pending | pending | False positives |
| M6.1 | Formalize versioned machine-readable plugin protocol schema | SDK Engineer + Plugin Runtime Engineer | not started | pending | pending | Single-source generation strategy |
| M6.2 | Implement TypeScript, Python, Go, and C++ SDKs | SDK Engineer | not started | pending | pending | Packaging without startup runtimes |
| M6.3 | Add plugin init/test/validate/package commands | SDK Engineer | not started | pending | pending | Existing CLI conventions |
| M6.4 | Add six examples for each required language | SDK Engineer | not started | pending | pending | Runtime/toolchain availability |
| M6.5 | Run common protocol conformance vectors across SDKs | SDK Engineer | not started | pending | pending | Cross-language cancellation behavior |
| M6.6 | Measure shared-host value; implement or document adapter/ADR | SDK + Performance Engineers | not started | pending | pending | Trust/isolation complexity |
| M7.1 | ADR comparing trusted-native interface options | Architecture + Security Reviewers | not started | pending | pending | ABI and trust risk |
| M7.2 | Define narrow versioned C ABI | Native Plugin Engineer | not started | pending | pending | Ownership, threading, allocator contract |
| M7.3 | Add disabled-by-default explicit trust gates | Native Plugin Engineer | not started | pending | pending | Project-controlled activation prevention |
| M7.4 | Add reference native plugin and compatibility tests | Native Plugin Engineer | not started | pending | pending | Crash containment is impossible in-process |
| M7.5 | Benchmark native versus persistent out-of-process paths | Performance Engineer | not started | pending | pending | Honest significance threshold |
| M8.1 | Add cross-subsystem fault-injection suite | Reliability Engineer | not started | pending | pending | Determinism and runtime cost |
| M8.2 | Add parser/path fuzz targets or adversarial equivalents | Reliability Engineer | not started | pending | pending | CI fuzz duration |
| M8.3 | Run ASan/UBSan/TSan/LSan as supported | Coordinator | not started | pending | pending | Existing unrelated findings |
| M8.4 | Establish smoke and scheduled CI performance budgets | Performance Engineer | not started | pending | pending | Host noise and baseline updates |
| M8.5 | Record dependency, license, binary, startup, memory, security deltas | Coordinator | not started | pending | pending | Platform-specific binary measurements |
| M9.1 | Update final architecture and trust-boundary documentation | Documentation Owner | not started | pending | pending | Documentation drift |
| M9.2 | Write manifest/session/tool/native migration guide | Documentation Owner | not started | pending | pending | Legacy fixture coverage |
| M9.3 | Write operations and diagnostics guide | Documentation Owner | not started | pending | pending | Command/API stability |
| M9.4 | Produce evidence-backed final technical report | Coordinator | not started | `docs/engineering/backend-modernization-final-report.md` (planned) | pending | pending | No unsupported performance claims |
| M9.5 | Add non-technical outcome summary | Documentation Owner | not started | pending | pending | Clarity |
| M9.6 | Clean full integration, conformance, migration, fault, sanitizer, performance, docs, and examples gate | Integration Reviewer + coordinator | not started | pending | pending | Cross-platform checks depend on CI |

## Review finding ledger

The integrated M0 review used the default reviewer on the complete mapping, harness,
and baseline delta. M1 planning findings record accepted design gates; `fixed` means the
design blocker is resolved, not that implementation is complete. Findings keep stable
IDs and one of: `open`, `fixed`, `rejected`, or `deferred`.

| Finding | Milestone | Status | Evidence / resolution |
|---|---|---|---|
| M0-R1 | M0 | fixed | Schema v2 smoke requires an executable helper, unsupported required seams fail checks, and negative harness tests cover omission/non-executable helpers. |
| M0-R2 | M0 | fixed | Benchmark children now receive a fixed minimal environment; tests prove provider/base-URL/database/loader/askpass/Kerberos/Docker/Python variables do not survive; docs explicitly deny OS network containment. |
| M0-R3 | M0 | fixed | `backend-current-state.md` now records that plugin/MCP normal shutdown reaps only the leader and can leave a same-group descendant. |
| M0-R4 | M0 | fixed | Schema v2 retains the complete memory-helper output and every RSS snapshot, aggregates per-run maxima, and measures Linux current RSS delta with peak details separate. |
| M0-R5 | M0 | fixed | Schema v2 records SHA-256/size/mode/mtime for all artifacts, source commit/tree/dirty state, CMake/compiler/feature metadata, and explicitly unverified provenance. |
| M0-R6 | M0 | fixed | Native registry timing now performs exact lookup of the final built-in and emits `ns_per_lookup`; catalog timing is labeled composite. |
| M0-R7 | M0 | fixed | M0 rows now identify final files, tests, benchmark evidence, commits, and calibrated residual risks. |
| M0-D1 | M0 | fixed | A full-suite-only `ESRCH` was traced to `/proc/<pid>/stat` disappearing between open and read in `parallel_test_runner_test.py`; the test now accepts only `ENOENT`/`ESRCH` as successful process disappearance. The focused test passed 100 consecutive runs and the full suite passed afterward. |
| M1-GATE-001 | M1 | fixed | [Managed-group lifecycle and platform scope](process-supervisor-adr.md#m1-gate-001) are accepted: strong Linux/POSIX managed-group cleanup, explicit escape/non-guarantees, and no current Windows support claim. |
| M1-GATE-002 | M1 | fixed | [Composition and adoption authority](process-supervisor-adr.md#m1-gate-002) are accepted: application-scoped injection, reservation-before-fork, gated common spawn/secure adoption, sole reap authority, and zero dependency exceptions. |
| M1-GATE-003 | M1 | fixed | [Exceptional-family and privacy policy](process-supervisor-adr.md#m1-gate-003) is accepted: exact environments, private content-free diagnostics, editor terminal handoff, direct browser supervision, and required canary/PTTY tests. |

## M0 validation evidence

- `cmake --preset release` and the Release `ava`, benchmark-helper, and fake-provider
  targets built successfully.
- `cmake --preset dev` and the complete BetaTest build succeeded.
- Release and BetaTest `ava_benchmark.harness_self_test` and
  `ava_benchmark.smoke` passed.
- The schema-v2 baseline completed five runs for every currently measurable family;
  the committed JSON passed the harness schema/identity validator.
- `ctest --repeat until-fail:100 -R '^ava_build\.parallel_test_runner$'` passed
  after the procfs-disappearance race fix.
- The complete BetaTest CTest run passed all 147 registered tests with zero failures;
  credential/live-provider and opt-in terminal tests reported their expected skips.
- Source Markdown links, documentation structure, Python compilation, C++ formatting,
  and `git diff --check` passed.
- No production runtime source or production dependency changed in M0.

## Commit ledger

| Milestone | Commit | Scope |
|---|---|---|
| M0 | `3d4349ef`, `8fab13fe`, `971327fb`, `6999441f` | Current-state map, benchmark harness/smoke, review fixes, baseline evidence, and reliability-test fix |
| M1 | pending | Accepted process-supervisor ADR; implementation and migration evidence pending |
| M2 | pending | Persistent plugin workers |
| M3 | pending | Selective tool catalog and router |
| M4 | pending | Bounded sessions, index, payloads, receipts |
| M5 | pending | Resource-aware durable scheduler |
| M6 | pending | Multi-language plugin SDKs |
| M7 | pending | Experimental trusted-native interface |
| M8 | pending | Fault injection, hardening, performance gates |
| M9 | pending | Documentation, migration, release readiness |
