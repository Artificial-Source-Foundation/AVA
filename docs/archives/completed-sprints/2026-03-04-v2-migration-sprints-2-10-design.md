# AVA v2 Migration (Sprints 2-10) Design

## Goal

Complete AVA's post-Sprint-1 migration by delivering Sprints 2-10 in order, with sprint-scoped commits, mandatory competitor pattern scraping before each story, and a reviewer gate after each sprint.

## Constraints and Non-Negotiables

- Work in current git worktree and continue incrementally from existing Sprint 2 edits.
- SolidJS only for desktop UI (`createSignal`, `For`, `Show`, `onCleanup`) and no React patterns.
- Use `dispatchCompute()` for Rust hotpaths and verify command names against `src-tauri/src/commands/`.
- Keep Node/CLI compatibility via TypeScript fallback where Tauri invoke is unavailable.
- Per sprint: implement stories -> run verification -> run reviewer subagent gate -> fix issues -> re-review -> commit.

## Delivery Architecture

### Execution Loop

1. Story kickoff: scrape competitor references from `docs/reference-code/` via haiku subagents.
2. Implement only the current story scope.
3. Run targeted tests/typecheck for touched modules.
4. Mark story complete and proceed until sprint scope is done.
5. Run reviewer subagent gate (Opus-equivalent review pass using available reviewer subagent).
6. Resolve findings and re-run review until pass.
7. Commit sprint with sprint-specific message.

### Cross-Sprint Technical Spine

- `platform-dispatch` is the central Rust/TS routing primitive for compute-heavy hotpaths.
- Tool and extension middleware remains priority-driven; new middleware slots must preserve ordering guarantees.
- Context management combines repo-map weighting, token-threshold compaction, and low-cost summarization.
- Safety stack layers sandbox policy, checkpointing, and dynamic approvals without widening dangerous permission patterns.
- Desktop UX upgrades stream tokens/tool states, support human-in-the-loop diff acceptance, and expose provider/key/permission controls.

## Sprint Design Notes

### Sprint 2: Rust Hotpaths

- Finalize `dispatchCompute<T>(rustCommand, rustArgs, tsFallback)` contract and typing alignment with bridge shapes.
- Route edit (`compute_fuzzy_replace`) and grep (`compute_grep`) through dispatch path.
- Wire memory/permissions/validator extension calls to Rust commands where present; leave TS-only path with TODO where absent.

### Sprint 3: Edit Excellence

- Build a 4-tier edit cascade in `packages/extensions/tools-extended/src/`:
  1. Exact
  2. Flexible/Indent/Token
  3. Structural/Regex
  4. Fuzzy + bounded LLM self-correction (max 2 correction rounds)
- Add RelativeIndenter and Unicode-normalized matching preprocessor.
- Add streaming edit parser for partial JSON and live diff emission via bus.

### Sprint 4: Context Intelligence

- Add repo-map PageRank integration with Aider-style weighting and 20% prompt budget targeting.
- Implement 3-tier compaction thresholds (`64K->37K`, `128K->98K`, `200K->160K`) and fallback summarization using cheapest model.
- Add LSP diagnostics middleware after write/edit/create operations when LSP is available.

### Sprint 5: Agent Reliability

- Add stuck detection middleware (repeat/spinning/budget) with steering then escalation.
- Add error classification + bounded retry recovery path (max 3 retries) for recoverable tool errors.
- Add completion detection for summary-like no-tool responses, with modified-file validation before auto-complete.

### Sprint 6: Sandbox and Safety

- Enforce command-level sandbox policy (`sandbox_run`) for package installs, while allowing harmless read/status commands unsandboxed.
- Add ghost checkpoint refs under `refs/ava/checkpoints/`, excluding large/generated artifacts.
- Implement dynamic per-session permission learning with strict no-generalization for dangerous patterns.

### Sprint 7: Desktop UX Polish

- Add character streaming with debounce and tool-call progress affordances.
- Build `DiffReview` hunk-level accept/reject with bulk actions and clear visual diff semantics.
- Expand Settings with provider-grouped model picker, keychain-backed API key management, and permission presets.

### Sprint 8: Plugin Ecosystem

- Validate SDK compatibility with post-Sprint-1 structure; fix breakages in example plugins.
- Add catalog UI with search/filter/details and persisted installed/available state.
- Treat MCP servers as plugin-backed namespaced tools (`server:tool_name`) with proper lifecycle cleanup.

### Sprint 9: Testing and QA

- Move e2e harness to real backend for 10 migration-critical scenarios.
- Achieve green TS and Rust test suites and performance/console baselines.
- Fix known flaky tests (`ChatView.integration.test.tsx`, `extension-loader.test.ts`).

### Sprint 10: Docs and Ship

- Update architecture/docs to hybrid v2 reality and remove stale migration-era docs.
- Update memory docs and versioning to `2.0.0` across JS + Cargo manifests.
- Write changelog and confirm production Tauri build success.

## Risk Management

- Keep changes sprint-scoped to reduce cross-sprint coupling and rollback cost.
- Verify command availability before wiring to Rust path and preserve TS fallback behavior.
- Use reviewer gate every sprint to keep technical debt from compounding.
- Run targeted tests per story and broader validation per sprint to keep cycle time stable.

## Validation Strategy

- Story-level: focused tests around changed modules plus typecheck where needed.
- Sprint-level: lint/type/test subsets plus reviewer pass/re-pass cycle.
- Release-level (Sprint 10): full build, final regression checks, and docs sanity review.
