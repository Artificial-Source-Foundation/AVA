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
| M0.2 | Reproducible startup, RSS, dispatch, plugin, catalog, session, cancellation, cleanup, and memory harness | Performance Engineer | active | benchmark sources/docs pending | Baseline commands pending | pending | Stable cross-platform RSS/timing collection |
| M0.3 | Establish honest machine-recorded baseline and calibrated budgets | Performance Engineer + coordinator | active | `docs/engineering/performance-baseline.md` (planned) | JSON/CSV evidence pending | pending | Host variability; no claims before measurement |
| M0.4 | Add lightweight CI performance/reliability smoke | Performance Engineer + coordinator | not started | pending | CI smoke pending | pending | Avoid noisy blocking thresholds |
| M1.1 | Define process ownership identifiers and supervisor API | Process Lifecycle Engineer | not started | pending | pending | Cross-subsystem ownership compatibility |
| M1.2 | Implement cross-platform process groups/tree cleanup | Process Lifecycle Engineer | not started | pending | pending | Windows Job Objects and POSIX race handling |
| M1.3 | Migrate every process-spawning subsystem | Process Lifecycle Engineer + subsystem owners | not started | pending | pending | Existing dirty app changes may overlap |
| M1.4 | Add bounded, secret-safe process diagnostics | Process Lifecycle Engineer | not started | pending | pending | Diagnostic cardinality and redaction |
| M1.5 | Add leak, cancellation, timeout, descendant, and shutdown tests | Process Lifecycle Engineer | not started | pending | pending | Platform-specific coverage |
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

No milestone review has run yet. Material findings will be recorded here with stable IDs
and one of: `open`, `fixed`, `rejected`, or `deferred`, plus evidence and rationale.

| Finding | Milestone | Status | Evidence / resolution |
|---|---|---|---|
| — | — | — | — |

## Commit ledger

| Milestone | Commit | Scope |
|---|---|---|
| M0 | pending | Mapping, benchmark harness, baseline, and CI smoke |
| M1 | pending | Unified process supervisor |
| M2 | pending | Persistent plugin workers |
| M3 | pending | Selective tool catalog and router |
| M4 | pending | Bounded sessions, index, payloads, receipts |
| M5 | pending | Resource-aware durable scheduler |
| M6 | pending | Multi-language plugin SDKs |
| M7 | pending | Experimental trusted-native interface |
| M8 | pending | Fault injection, hardening, performance gates |
| M9 | pending | Documentation, migration, release readiness |
