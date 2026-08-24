# 2026-08-23 AVA Release-Readiness Audit

This is a dated historical audit record, not current release authority. Current status lives in the [release-readiness ledger](../product/release-readiness.md), and product decisions live in [principles](../product/principles.md).

The audit retained private raw evidence outside the repository. `$AUDIT_ROOT` below denotes that private mode-0700 audit root. Raw logs, captures, absolute checkout/home paths, credentials, provider payloads, and binaries are intentionally not committed.

## Scope, identities, and verdict

- Freeze date: 2026-08-23.
- AVA baseline: `c43794e91bb8d4f706ad4c916387f5487fca14ee`; CMake/runtime candidate `1.0.0`.
- Final verdict: **READY AFTER LISTED BLOCKERS**.
- No official release or `v1.0.0` tag existed or was created.

### Reference identities

The audit fetched detached, disposable exact-pin worktrees without moving the repository's preserved reference checkouts:

| Project | Fetched audit pin and version | Separately preserved checkout pin | Distinction |
| --- | --- | --- | --- |
| Pi.dev | `460191cfcf27d60ff81fc0178812f4ff09e8df06`; coding-agent/TUI/agent `0.84.2` | `936aff00918de1187f085f123c2812d8f2d67745`; `0.84.1` era | Source comparison used the fetched pin; installed fallback terminal checks used Pi `0.84.1`, not the target source pin |
| OpenCode | `03bba464d46f3eddf74195919b1344aa937f7b11`; CLI/TUI `1.18.21` | `38e10eb1408feb700021b8e8766fb0ab41bf84e2`; `1.18.15` era | Source comparison used the fetched pin; installed fallback terminal checks used `1.18.11`, not the target source pin |
| Grok Build | `07b2f7144fd5c5c9d3dd1966937a87852d2dbdb8`; core/pager-bin `1.0.8`, npm wrapper `0.1.220-alpha.4` | `8a14c91d88875a831a38b3a066b1683116bcb31c` | Source comparison used the fetched pin; no target or fallback product binary ran |

Reference source, docs, and tests were comparative input only. They were not AVA architecture or execution authority, and no reference code was copied.

## Classification vocabulary

- **PASS:** the exact described check completed successfully.
- **FAIL:** the check ran and established a defect or unmet condition.
- **SKIP — expected:** a declared optional/credential/capability gate did not run.
- **BLOCKED — environment:** a required toolchain, dependency, credential, isolation facility, or provider condition prevented the check.
- **NOT RUN:** the check was intentionally not attempted or was outside the authorized lane.
- **INCONCLUSIVE:** evidence exercised part of the behavior but could not decide the requested end-to-end claim.

An initial failure followed by a passing fixed-tree run remains recorded as both; it is not rewritten as an initial pass.

## Host and toolchain

The audit host was Ubuntu 24.04.4 LTS x86-64, kernel 7.0.0-29-generic, Intel Core i9-10850K with BMI2, 20 logical CPUs, 31 GiB RAM and 31 GiB swap. Primary tools were CMake/CTest 3.31.10, GCC/G++ 13.3.0, Clang 18.1.3, Ninja 1.13.0, Make 4.3, Python 3.12.3, tmux 3.4, Git 2.43.0, Node 24.13.1/npm 11.18.0, Bun 1.3.9, and Rust/Cargo 1.93.0. Podman, DotSlash, system `protoc`, clang-tidy/analyzer, and LLVM coverage tools were absent.

## Commands and classifications

Paths below use `$AUDIT_ROOT` rather than private absolute paths. `<fresh-private-build>` denotes an intentionally redacted private build path; every command-line option is otherwise preserved.

| Command/check | Classification | Result and evidence |
| --- | --- | --- |
| `cmake --preset dev -B "$AUDIT_ROOT/builds/dev-gcc"` | PASS | GCC 13 BetaTest, debug instrumentation, Unix Makefiles; `$AUDIT_ROOT/logs/dev-gcc/configure.*` |
| `scripts/build.sh --build-dir "$AUDIT_ROOT/builds/dev-gcc" --jobs 6` | PASS | Fresh developer build; `$AUDIT_ROOT/logs/dev-gcc/build.*` |
| `scripts/run-tests.sh --build-dir "$AUDIT_ROOT/builds/dev-gcc" --jobs 6 --output-on-failure` | PASS | 142/142, 30 declared expected skips; `$AUDIT_ROOT/logs/dev-gcc/tests.*` |
| `cmake -S . -B <fresh-private-build> -GNinja -DAVA_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Release -DEnableDebug=OFF -DCMAKE_CXX_COMPILER=g++-13 -DCMAKE_CXX_SCAN_FOR_MODULES=OFF -DCMAKE_EXPORT_COMPILE_COMMANDS=ON`, then locked build/full test | PASS | Configure/build/full CTest 140/140 and exact `ava 1.0.0`; `$AUDIT_ROOT/logs/release-gcc-ninja/` |
| Clang 18 Release configure with the same flags except `-DCMAKE_CXX_COMPILER=clang++`; second attempt added `-DCMAKE_CXX_SCAN_FOR_MODULES=OFF`; third also added `-DCMAKE_CXX_FLAGS=--gcc-install-dir=/usr/lib/gcc/x86_64-linux-gnu/13`, followed by locked builds when configure succeeded | BLOCKED — environment | Default scanner/toolchain conflict; GCC 16 header incompatibilities; then Clang 18/libstdc++13 lacked usable C++23 `std::expected`; `$AUDIT_ROOT/logs/release-clang*/` |
| Initial fresh canonical sanitizer default build | FAIL | Experimental/static consumers omitted sanitizer runtime; targeted build later exposed libcwd nested timeout and death-test/core handling; `$AUDIT_ROOT/logs/sanitize-gcc*/` |
| Post-integration `cmake --preset sanitize -B <fresh-private-build>`, complete build, then `scripts/run-tests.sh --build-dir <fresh-private-build> --jobs 2 --output-on-failure` | PASS | Fresh full ASan/UBSan 145/145 with 30 expected skips; static-consumer, non-recovering UB and command-contract self-tests passed; `$AUDIT_ROOT/logs/postfix-sanitize/` |
| Repeat core-mode and libcwd-sensitive sanitizer cases | PASS | 3/3 repeated groups; `$AUDIT_ROOT/logs/postfix-sanitize-repeat/` |
| Post-fix normal locked full CTest | PASS | 142/142 with 30 expected skips at the first integrated closure stage; `$AUDIT_ROOT/logs/postfix-normal/tests.log` |
| Final post-review normal, sanitizer, and Release full CTest after symlink-alias deny and CI-dependency corrections | PASS | 143/143 normal, 146/146 sanitizer, and 141/141 Release; each had 30 expected skips; `$AUDIT_ROOT/logs/final-after-review-{normal,sanitize,release}/` |
| `cmake --preset tsan -B ...`, focused build, and four configured TSan suites | PASS | 4/4 (`session_run_controller`, subagent coordinator/delivery, ACP); final current-tree rerun is `$AUDIT_ROOT/logs/final-after-review-tsan/` |
| Full opt-in tmux regex over 23 scenarios | FAIL | 22 passed; one never launched AVA because a 108-byte AF_UNIX socket path was too long; `$AUDIT_ROOT/logs/tmux/tests.log` |
| Isolated failed tmux case from a guarded short root | PASS | Effective candidate result 23/23; long-root robustness remained P2, not a product failure |
| Four direct PTY gates | PASS | 4/4 Kitty, iTerm2, lifecycle and OSC8/OSC52; final current-tree evidence is `$AUDIT_ROOT/logs/final-pty/` |
| Final current-tree tmux gate from repository build root | PASS | 23/23; `$AUDIT_ROOT/logs/final-tmux-repo-build/`. A shorter fresh root independently exposed a harness-only resize-screen-change assumption and remained classified P2 test infrastructure. |
| `scripts/package-linux.sh --require-release-qualified --output-dir "$AUDIT_ROOT/package-output"` | PASS | Local static provenance/package gates, checksum, extraction, CLI and fake-provider smoke; not official publication or complete candidate qualification |
| Same strict package command to a second empty output | PASS | Second local package passed; `$AUDIT_ROOT/logs/strict-package-2/` |
| Source Markdown link, documentation structure, assertion-comment and `git diff --check` direct gates at the baseline audit | PASS | `$AUDIT_ROOT/logs/documentation-direct/`; this predated consolidation |
| `python3 scripts/verify-markdown-links.py . --source-tree` and `python3 scripts/verify-documentation-structure.py .` | PASS | 128 Markdown files verified; 108/108 documentation nodes reachable with 16 required indexes and 28 `llms.txt` links |
| `scripts/run-tests.sh --build-dir build --jobs 4 -R '^ava_tests\.(markdown_(link_verifier|links_source)|documentation_structure_(checker|source))$' --output-on-failure` | PASS | Four focused Markdown/structure CTests passed 4/4 through the locked runner |
| `scripts/run-tests.sh --build-dir build-release --jobs 1 -R '^ava_release\.(provenance|install_component|package_linux)$' --output-on-failure` | PASS | 3/3 from the Release/libcwd-off tree, including staged artifact links and exact 32-document payload |
| Direct/registered live-dogfood launcher harness | PASS | Private-parent, cleanup, mode, rejection, environment and matrix cases |
| Release `ava`/fake-provider build after terminal warning gate | FAIL, then PASS | Newly enabled terminal warnings first exposed ncurses result variables used only by debug assertions; explicit `[[maybe_unused]]` annotations preserved calls/assertions, then the Release build and focused `ava_tests.tui_composer` passed |
| Static analysis, fuzzing, coverage, MemorySanitizer | NOT RUN | Tools/targets or instrumented dependency closure absent; retained as P2/P3 |
| Live provider/model intelligence trials | SKIP — expected | No approved credentials/cost/network lane; deterministic fake-provider checks remained release evidence |
| Official tag/release/publication, remote artifact retention, changelog/release-body approval, withdrawal drill | NOT RUN | These are the final listed publication blockers; the audit explicitly did not publish |
| MSVC, Windows, macOS, AArch64 native candidate, and multi-config release qualification | NOT RUN | No native exact-candidate evidence; none was accepted as supported |

## Fixed safety findings

### Persistent deny bypass

A pre-fix runtime probe reproduced that persistent exact denies did not override policy auto-Allow for read/search dispatch. Evidence is `$AUDIT_ROOT/logs/p0-reproductions/ava-br-001-{valid,malformed}-summary.json`; matching fixed-tree summaries prove the four provider-visible canaries no longer reached the provider. Integrated review then found the same resource remained reachable through a contained symlink alias. The final working-tree fix applies durable deny preflight to backend auto-Allow file/search/mutation operations, normalizes root path spellings, extends only Denies across physical symlink/hardlink identity (including missing targets beneath aliased parents), keeps Allows lexical-only, filters denied per-file results/previews, emits matching rule IDs, and fails closed on malformed stores or required identity errors. `ava_tests.permission_rules` and the adjacent tool/ACP/agent whole-process suites passed after the final correction.

### Max-token tool execution

Source and tests established that assembled calls could proceed when the terminal provider outcome was a length/max-token result. The fix binds bounded non-retryable failure results and continues without dispatch. `tests/agent_loop_resilience_tests.cpp` covers buffered/streamed OpenAI Responses, OpenAI-compatible, Anthropic, and Gemini vectors, including valid-looking and malformed partial arguments, and proves zero prompts, preflights, tool events, side effects, and tool-call count. Focused suite and full normal/sanitizer runs passed.

### Sanitizer/terminal instrumentation

The canonical sanitizer path now uses BetaTest plus debug instrumentation, non-recovering UBSan, sanitizer runtime propagation for static consumers, warning/sanitizer coverage on `ava_terminal`, violation/command self-tests, and intentional-death-test core suppression. The fresh 145/145 sanitizer pass is the closure evidence; the earlier failed build and timeout remain part of the record.

### Live-dogfood deletion roots

The audit found caller path overrides that were recursively deleted. The working-tree launchers now validate private parents, create unpredictable mode-0700 invocation children, remove only those children, and retain a deterministic negative harness. No live provider was required to verify root ownership/cleanup behavior; broader environment minimization remains non-blocking future hardening.

## Benchmark and dogfood scope

The benchmark design preregistered a tiny C++23/CMake fixture and 14 tasks: orientation, plan-only diagnosis, failure explanation, defect fix, feature addition, diff explanation, denied operation, cancellation, failed-tool recovery, resume, transcript search, bounded subagent research, resize, and terminal exit. The fixture built; its duplicate-ID test failed intentionally as the seeded defect.

No overall product leaderboard was produced. Scripted/fake-provider protocol evidence, same-model comparative trials, and native-product live dogfood were defined as separate lanes. No model-backed comparative lane ran.

### Limits

The proposed trial envelope was finite: five minutes, 25 provider turns, 80 tool calls, three compactions, 512 MiB RSS excluding compiler work, and 50 MiB artifacts. Child work was capped at six turns/20 tools with concurrency two per parent/four globally. Provider retry was at most three attempts/60 seconds and pre-output only. Cancellation acknowledgment was 250 ms, graceful cleanup two seconds, terminal cleanup five seconds, and terminal capture 2 MiB. The orientation task additionally allowed at most 12 read/search calls and 512 KiB read. Hard stop conditions included workspace escape, unauthorized effect, secret/canary exposure, duplicate dispatch, session corruption, uncontrolled output, process explosion, and a fourth identical no-progress call.

These are benchmark-design limits, not AVA runtime promises, and the complete harness was not implemented or run in this audit.

### Product outcomes

| Product/evidence | Classification | Scope |
| --- | --- | --- |
| AVA deterministic CLI/backend | PASS for denial, cancellation, plan write denial, session persistence, malformed RPC recovery, fake-provider print/RPC, resize and terminal exit | Later TUI pass also established transcript search; model-dependent orientation/fix/feature/subagent tasks stayed blocked or inconclusive |
| AVA terminal tasks 8, 11, 13, 14 | PASS | Cancellation, transcript search, resize, terminal exit under deterministic/private roots |
| Pi.dev exact `0.84.2` source build/runtime | BLOCKED — environment | Offline `npm ci --ignore-scripts` lacked cached Anthropic SDK; installed `0.84.1` version/help/private PTY resize/cleanup passed only as non-target analogue |
| Pi.dev subagent task | SKIP — expected | Native package has no built-in subagent; an example extension would change the cohort |
| OpenCode exact `1.18.21` source build/runtime | BLOCKED — environment | Required Bun 1.3.14/dependencies absent and lifecycle/network-generating build steps were not authorized; installed `1.18.11` version/help/session/private PTY analogue passed |
| Grok Build exact source/runtime | BLOCKED — environment | Rust 1.94, DotSlash/protoc, reviewed dependency closure and binary absent; downloads were prohibited |
| All reference model-backed tasks | BLOCKED — environment | No approved credentials/model cohort |

Installed older-version terminal analogues were never scored as exact-pin comparative results.

## Product-fit comparison

Reference claims in these tables are source/documentation/test inspection unless the benchmark section explicitly records an installed fallback run. They are not runtime or model-quality scores.

### Backend and safety

| Area | AVA | Pi.dev | OpenCode | Grok Build | AVA disposition |
| --- | --- | --- | --- | --- | --- |
| Lifecycle/interfaces | One admitted backend serves TUI, line shell, print, RPC and ACP through typed events | Agent core plus coding-agent session/TUI layers | Shared server/API backs TUI/web/SDK/ACP clients | Pager/headless/ACP surfaces use Rust action/effect and tool-event layers | Already solved differently; no server or architecture transplant |
| Permissions/execution | Backend allow/ask/deny, durable rules, sealed commands, Linux containment and audit | Deliberately no built-in operation permission or sandbox | Structured rules but broad permissive defaults; no sandbox | Rich permission/sandbox surfaces, but reviewed hook/sandbox setup failures can continue | Preserve AVA's fail-closed model; permissionless, ambient and fail-open patterns rejected |
| Provider truncation/retry | Cross-family truncation dispatch was found and fixed; non-empty transient retry remains P2 | Explicitly suppresses calls from length-truncated messages | Normalized provider service/capabilities/errors | Explicit bounded progress/terminal tool frames | Adopted the safety regression vector, not reference architecture; retry/progress improvements stay bounded post-release |
| Sessions | Strict append-only JSONL, leases/authority, recovery, lineage, compaction, local export/import | Append-only tree/fork/clone/compact/export with weaker pathname authority | SQLite/event projection, pagination, snapshots/revert | JSONL plus rebuildable search index and orphan reconciliation | Preserve strict AVA authority; derivative search/revert/reconciliation are optional later work, lenient replay rejected |
| Subagents/jobs | Bounded child sessions and process-local jobs with inherited authority | No native built-in subagent; extension example only | Configurable primary/subagent profiles, depth and background child sessions | Broader dashboard/workflow/reconciliation surface | Current AVA slice is release-complete; durable jobs/task graphs require separate product design |
| Plugins/MCP/LSP | Inspected disabled-by-default out-of-process local plugins, local stdio MCP, installed/configured LSP | Ambient TypeScript packages/extensions and lifecycle scripts | In-process plugins/runtime installs; rich MCP/LSP including downloads | Plugin marketplace/trust, remote MCP/OAuth and richer LSP | Ambient installs/imports rejected; remote MCP and richer LSP remain P3/P2 only |
| Diagnostics/telemetry | Private bounded diagnostics, no telemetry or update checks | Extensible local diagnostics and release telemetry choices | Multi-client diagnostics and broader hosted/product surfaces | Broad terminal doctor/telemetry/cloud features | Keep privacy/local-first behavior; cloud/telemetry breadth intentionally excluded |
| Release practice | Strict local static package gates but exact-byte CI/publication still open | Staged source/npm/binary release with checksums and draft cleanup | Broad cross-platform build/sign/publish workflows | Versioned atomic activation/rollback tests, but inspected downloads lack uniform digest checks | Retain deep AVA tests; first release needs exact retained bytes, no-clobber draft and withdrawal, not cross-platform parity |

### Terminal and interaction

| Dimension | Comparative lesson and disposition |
| --- | --- |
| Startup/onboarding | AVA's executed auth-first composer and progressive disclosure fit its no-telemetry local product. Pi.dev setup breadth, OpenCode home/server UI, and Grok browser/welcome flows do not justify restoring a startup wizard. |
| Transcript/composer | AVA's executed multiline editor, paste, search, selection/copy, Markdown/code/diff/tool cards, narrow layouts and performance budgets meet the release need. Pi.dev virtual-terminal components, OpenCode responsive web/TUI components, and Grok block viewers are behavioral references only; no renderer rewrite is admitted. |
| Permissions/plan | AVA's backend-enforced prompts and plan write restrictions are an intentional strength. Richer Grok plan-review UX is P3 unless every side-effect path is backend-covered; visible approval without authority is rejected. |
| Sessions/models/jobs | AVA selectors, tree/fork/clone/archive, model cycles and bounded job workspace were exercised through deterministic/tmux tests. Broader session content search, undo, dashboards and task graphs remain non-blocking. |
| Terminal lifecycle | AVA has the strongest executed evidence in this audit: effective 23/23 tmux and 4/4 PTY. Pi.dev/OpenCode installed older binaries supplied limited resize/exit analogues; Grok exact execution was blocked. No model-intelligence or physical-pixel ranking was made. |

### Contributor documentation

AVA now has one task-ordered documentation spine, current architecture/codebase maps, canonical presets, focused-test paths, release ledger and publication runbook. Pi.dev's locked npm/Node workflow and staged release docs are useful release-operation references. OpenCode's exact Bun/native/generation requirements and source/doc mismatches demonstrate why generated docs are not evidence. Grok Build's pinned Rust/DotSlash/native-download closure demonstrates why a documented toolchain is not an executable offline build. AVA does not imitate their documentation layouts; it keeps current, normative, planning and historical authority classes separate.

## Build, C++, and verification review

- The project is C++23 with narrow module boundaries and target-scoped warnings. The documented CMake 3.25 floor was false because test timeout signal/grace properties require CMake 3.27.
- A bounded 2026-08-23 advisory review queried exact gitlink commits through OSV and GitHub repository advisories, both test-only npm locks through npm audit and OSV, and host glibc/ncurses through Canonical notices. No demonstrated release-relevant vulnerability or missing mandatory license evidence was found. The result remained **INCONCLUSIVE**, not PASS, for five niche gitlinks without authoritative ecosystem mappings and for destination-controlled/unsupported-PPA runtimes. Full query evidence is `$AUDIT_ROOT/extra-reports/dependency-advisory-review.txt`; empty advisory results were not treated as proof of safety.
- GCC 13 on Ubuntu 24 x64 passed both canonical BetaTest/Make and additional Release/Ninja configurations. Clang was environment-blocked, not declared failed or supported. MSVC/macOS/Windows were unsupported and untested. Multi-config behavior was not release-qualified.
- Sample-based review of 691 first-party C++ source/header files found no manual `delete`, `malloc`, `calloc`, `realloc`, or `free`; sampled subprocess/thread owners had RAII cleanup. It also found written-policy tension around explicit `new` factories/shared ownership, no process-wide exception-boundary injection test, no first-party fuzz/coverage gate, and narrow TSan coverage.
- Static module/session authority inventories and assertion-comment checks passed. Static analysis and coverage were unavailable, so no zero-diagnostic or coverage claim was made.

## Terminal results

Deterministic renderer/editor suites covered bounded layouts and performance. The real-terminal evidence was effective 23/23 tmux plus 4/4 direct PTY. Two path-sensitive harness failures were kept visible: the original long root exceeded the AF_UNIX socket limit, while a final very short root made a 100→160-column drawer capture textually identical and violated the harness's screen-change assumption; the same final current tree passed 23/23 from the ordinary repository build root. PTY checks covered alternate-screen, keyboard protocol, bracketed paste, cursor and termios restoration, protocol images/OSC, signals, and process-group cleanup. No physical-terminal pixel review or broad screen-reader certification occurred.

AVA's auth-first composer, progressive wide/narrow layout, tool/diff/permission cards, session/model/settings selectors, bounded host-rendered plugin UI, and abandoned-parent branch-summary flow had source/test/terminal evidence. The audit found no frontend P0 parity blocker and made no model-intelligence superiority claim.

## Package, platform floor, and publication

The audited local x64 package was dynamically linked and host-specific. Static/package inspection established the exact current floor:

- CPU: x86-64 with BMI2 instructions;
- glibc through `GLIBC_2.38`;
- libstdc++ through `GLIBCXX_3.4.32`;
- C++ ABI through `CXXABI_1.3.13`;
- `libncursesw.so.6`, `libtinfo.so.6`, ordinary glibc/libstdc++/libgcc dependencies, a usable terminfo database, and `curl` on `PATH`.

The audited build host was not a minimum-host qualification run. Therefore first publication stays Linux x64 and requires exact retained bytes plus minimum-floor extraction evidence. AArch64 source/package gates existed but no native exact-candidate evidence was accepted; cross-compilation was not evidence.

`--require-release-qualified` and `release_qualified:true` were found to prove only implemented source cleanliness, gitlink, license, native architecture agreement, dynamic-dependency, version, and package gates. They do not prove full CTest, native CI, terminal gates, retained bytes, or publication.

Two strict local packages passed, but they were audit artifacts, not official release assets. At the audit cut, the package contained 33 documents because a completed TUI plan was still shipped; consolidation removes that history document, leaving 32 documents (30 Markdown and two JSON) and indexes the already-packaged custom-provider guide.

## Hardening and reproducibility

Static ELF inspection found PIE, NX/non-executable stack, full RELRO with `BIND_NOW`, stack-protection/FORTIFY references, no RPATH/RUNPATH, and only allowlisted dynamic dependencies. `checksec`, `hardening-check`, and `scanelf` were unavailable, so results came from `file`/`readelf`/`objdump`/`ldd` inspection and are not a broad exploit-resistance claim.

Two strict builds produced identical extracted regular-file hashes and the same binary build ID, but different `.tar.gz` hashes. Archive listings showed changing directory/provenance/binary timestamps and owner metadata. `SOURCE_DATE_EPOCH`, stable tar ownership/mode/time/order, and gzip metadata were not normalized. No standard SBOM, detached signature, or hosted attestation was produced.

## External primary research and application

- CMake Project, current 4.4.2 documentation accessed 2026-08-23, was applied to minimum-version, timeout properties, presets, single- versus multi-config behavior, FetchContent, generated targets, and link/interface analysis. It drove the 3.27 floor and the non-qualified multi-config classification: [CMake documentation](https://cmake.org/cmake/help/latest/).
- LLVM Project/Clang Team 24.0.0git documentation accessed 2026-08-23 for AddressSanitizer, UndefinedBehaviorSanitizer, ThreadSanitizer, MemorySanitizer, Clang Static Analyzer, clang-tidy, [libFuzzer](https://llvm.org/docs/LibFuzzer.html), and source-based coverage informed non-recovering UBSan, complete instrumentation, TSan breadth, and the non-blocking static/fuzz/coverage/MSan dispositions.
- Standard C++ Foundation's [C++ Core Guidelines](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines), living release 0.8 dated 2026-06-14, informed the sample ownership review but was not imported wholesale; AVA's written ownership policy remains the authority pending reconciliation.
- GitHub documentation current on 2026-08-23 for [workflow artifacts](https://docs.github.com/en/actions/concepts/workflows-and-actions/workflow-artifacts), [artifact attestations](https://docs.github.com/en/actions/how-tos/secure-your-work/use-artifact-attestations/use-artifact-attestations), [secure Actions use](https://docs.github.com/en/actions/reference/security/secure-use), and [immutable releases](https://docs.github.com/en/code-security/concepts/supply-chain-security/immutable-releases) informed exact-byte retention, least privilege, no-clobber promotion, optional attestation, and immutable patch correction.
- SLSA specification 1.2, Linux Foundation/OpenSSF, informed the distinction between repository-generated provenance and hosted builder identity; the audit did not claim a SLSA level: [SLSA build requirements](https://slsa.dev/spec/v1.2/build-requirements).
- Reproducible Builds Project [archive guidance](https://reproducible-builds.org/docs/archives/) accessed 2026-08-23 was applied to the failed archive-hash comparison and P2 normalization criteria.
- SPDX 3.0 and CycloneDX specification guidance current on 2026-08-23 informed deferred standard SBOM criteria: [SPDX specifications](https://spdx.dev/use/specifications/) and [CycloneDX overview](https://www.cyclonedx.org/specification/overview/).
- OSV/OpenSSF API v1, GitHub Security Advisories REST API `2022-11-28`, npm CLI 11 audit, NIST NVD CVE API 2.0, and Canonical USN/CVE notices were used for the bounded advisory review. Exact public queries, package boundaries, dates, and uncertainty are retained outside the repository.

## Frozen disposition

Independent technical and scope review fixed the release blockers to three open P1s only:

1. exact-byte native CI/retention, including exact-candidate 23 tmux and four PTY gates (`AVA-REL-011`);
2. truthful supported artifact floor and native evidence (`AVA-REL-013`);
3. official publication lifecycle and reviewed 1.0 changelog/release notes/rollback/withdrawal (`AVA-REL-012`).

Persistent deny precedence, truncated-provider tool suppression, canonical sanitizer/terminal instrumentation/death-test handling, and destructive live-dogfood roots were fixed in the working tree. Session descriptor hardening, finite ordinary session reads, provider parser parity, non-empty transient retry, XDG overlap rejection, backend grant ownership, and all broader hardening/product work remained P2/P3. Parity treadmill, source/architecture copying, fail-open/ambient patterns, telemetry/automatic downloads/public cloud sharing, and lenient authoritative replay were rejected.

The dated verdict is therefore **READY AFTER LISTED BLOCKERS**, not released and not presently qualified for publication.
