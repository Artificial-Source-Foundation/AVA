# AVA v2 Migration (Sprints 2-10) Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Deliver Sprints 2-10 of AVA's v2 migration with story-order implementation, mandatory pre-implementation competitor scraping, reviewer gating, and one commit per sprint.

**Architecture:** Build on the current Sprint 2 incremental branch state. Use `dispatchCompute()` as the Rust/TS bridge for compute and safety hotpaths, preserve TS fallbacks for non-Tauri runtimes, and integrate middleware by explicit priority ordering. Keep desktop updates in SolidJS and enforce test/review gates at story and sprint boundaries.

**Tech Stack:** TypeScript (strict), SolidJS, Tauri 2 + Rust commands, Vitest, Cargo test, Lefthook lint/format/type tooling.

---

### Task 1: Sprint 2 - Wire Rust Hotpaths

**Files:**
- Modify: `packages/core-v2/src/platform-dispatch.ts`
- Modify: `packages/core-v2/src/tools/edit.ts`
- Modify: `packages/core-v2/src/tools/grep.ts`
- Modify: `packages/extensions/memory/src/**/*.ts`
- Modify: `packages/extensions/permissions/src/**/*.ts`
- Modify: `packages/extensions/validator/src/**/*.ts`
- Reference: `src/services/rust-bridge.ts`, `src-tauri/src/commands/**/*.rs`
- Test: `packages/core-v2/src/tools/edit.test.ts`, `packages/core-v2/src/tools/grep.test.ts`, extension tests in touched packages

**Step 1: Scrape competitor references (required before code)**

Run subagent reads against `docs/reference-code/` for edit/grep/compute-dispatch and summarize reusable patterns.

**Step 2: Write/adjust failing tests first**

Add/expand tests for Tauri dispatch path + TS fallback for edit/grep and extension command routing.

Run: `npm run test -- packages/core-v2/src/tools/edit.test.ts packages/core-v2/src/tools/grep.test.ts`
Expected: FAIL on new assertions.

**Step 3: Implement minimal production code**

Wire `dispatchCompute<T>(rustCommand, rustArgs, tsFallback)` usage to edit/grep and extension callsites.

**Step 4: Verify command names and TODO policy**

Cross-check `src-tauri/src/commands/` and keep TS-only TODO if command missing.

**Step 5: Run sprint verification**

Run: `npm run lint && npx tsc --noEmit && npm run test -- packages/core-v2/src/tools/edit.test.ts packages/core-v2/src/tools/grep.test.ts`
Expected: PASS.

**Step 6: Reviewer gate and commit**

Run reviewer subagent, fix issues, rerun reviewer until pass, then commit:

```bash
git add packages/core-v2/src/platform-dispatch.ts packages/core-v2/src/tools/edit.ts packages/core-v2/src/tools/grep.ts packages/extensions/memory packages/extensions/permissions packages/extensions/validator
git commit -m "feat(sprint-2): wire rust hotpath dispatch with ts fallbacks"
```

### Task 2: Sprint 3 - Edit Excellence

**Files:**
- Modify/Create: `packages/extensions/tools-extended/src/edit/**/*`
- Modify: `packages/extensions/tools-extended/src/index.ts`
- Modify/Create tests: `packages/extensions/tools-extended/src/**/*.test.ts`

**Step 1: Scrape required references**

Use subagents to extract 4-tier cascade, RelativeIndenter, Unicode normalization, streaming parser strategies from local reference repos.

**Step 2: Add failing tests for each tier and correction bounds**

Cover exact/flexible/structural/fuzzy flows and max-2 LLM correction behavior.

Run: `npm run test -- packages/extensions/tools-extended/src`
Expected: FAIL for new tier/correction/streaming tests.

**Step 3: Implement preprocessors and cascade**

Add `RelativeIndenter` and `normalizeForMatch()`; implement tier routing with dispatchCompute for tiers 1-3 and TS for tier 4.

**Step 4: Implement streaming edit parser and bus emissions**

Support partial JSON, 80% old_text matching, live new_text apply, and incremental diff events.

**Step 5: Verify and gate**

Run: `npm run lint && npx tsc --noEmit && npm run test -- packages/extensions/tools-extended/src`
Expected: PASS.

**Step 6: Reviewer gate and commit**

```bash
git add packages/extensions/tools-extended/src
git commit -m "feat(sprint-3): implement multi-tier edit cascade and streaming edits"
```

### Task 3: Sprint 4 - Context Intelligence

**Files:**
- Modify/Create: `packages/extensions/context/src/**/*`
- Modify: prompt assembly paths in core/agent/context integration
- Modify/Create tests in `packages/extensions/context/src/**/*.test.ts`

**Step 1: Scrape references for repomap/compaction/LSP diagnostics**

Extract weighting/threshold/post-edit diagnostics patterns.

**Step 2: Add failing tests**

Test PageRank weighting, threshold transitions, and diagnostics append behavior.

**Step 3: Implement repo-map + dispatch compute path**

Integrate `compute_repo_map` with TS fallback and enforce 20% prompt budget consumption.

**Step 4: Implement 3-tier compaction and cheap summarizer path**

Apply thresholds: `64K->37K`, `128K->98K`, `200K->160K`.

**Step 5: Add LSP diagnostics middleware (priority 20)**

Append diagnostics after edit/write/create, skip when unavailable.

**Step 6: Verify, review, commit**

Run: `npm run lint && npx tsc --noEmit && npm run test -- packages/extensions/context/src`

```bash
git add packages/extensions/context/src packages/core-v2/src
git commit -m "feat(sprint-4): add repomap compaction tiers and lsp diagnostics middleware"
```

### Task 4: Sprint 5 - Agent Reliability

**Files:**
- Modify/Create: `packages/extensions/agent-modes/src/**/*`
- Modify/Create: `packages/extensions/hooks/src/**/*`
- Modify tests in both packages

**Step 1: Scrape repetition/error recovery references**

Capture stuck detection and error classification schemes.

**Step 2: Add failing middleware tests**

Repeat detection, no-change spinning, token budget threshold, retry bounds, completion heuristics.

**Step 3: Implement stuck detection middleware (priority 5)**

Detect repeat/spinning/budget, inject steering, escalate to user when unresolved.

**Step 4: Implement error recovery middleware (priority 15)**

Classify recoverable/fatal; retry edits with dispatch validation up to 3 attempts.

**Step 5: Implement completion detection**

If no tool calls + summary-like response, validate file state then auto-complete or steer.

**Step 6: Verify, review, commit**

Run: `npm run test -- packages/extensions/agent-modes/src packages/extensions/hooks/src`

```bash
git add packages/extensions/agent-modes/src packages/extensions/hooks/src
git commit -m "feat(sprint-5): add reliability middleware for stuck loops and recovery"
```

### Task 5: Sprint 6 - Sandbox and Safety

**Files:**
- Modify/Create: sandbox middleware in core/extension safety paths
- Modify/Create: git checkpoint modules under git/extension modules
- Modify/Create: permissions learning modules under `packages/extensions/permissions/src/**/*`
- Add tests for sandbox/checkpoints/permissions learning

**Step 1: Scrape sandbox/checkpoint/permission references**

Extract OS-policy shapes, checkpoint ref strategy, and safe generalization constraints.

**Step 2: Add failing tests for policy behavior**

Assert sandbox command matrix and dangerous pattern non-generalization.

**Step 3: Implement sandbox middleware (priority 3)**

Route install commands through `dispatchCompute('sandbox_run')`; keep `git status`/`ls`/`cat` unsandboxed.

**Step 4: Implement checkpoint refs**

Create ghost checkpoints at `refs/ava/checkpoints/` before agent runs and destructive operations, excluding generated/large files.

**Step 5: Implement dynamic session permissions**

Learn "always allow" patterns per session, with strict deny for dangerous auto-generalization.

**Step 6: Verify, review, commit**

Run: `npm run test -- packages/extensions/permissions/src`

```bash
git add packages/extensions packages/core-v2/src src-tauri/src
git commit -m "feat(sprint-6): enforce sandbox checkpoints and dynamic session permissions"
```

### Task 6: Sprint 7 - Desktop UX Polish

**Files:**
- Modify/Create: `src/components/chat/**/*`
- Modify/Create: `src/components/panels/DiffReview.tsx` (or existing diff review panel)
- Modify/Create: `src/components/settings/**/*`
- Modify/Create tests in `src/**/*.test.tsx`

**Step 1: Scrape UI references**

Extract event batching, streaming diff visualization, and design-system conventions.

**Step 2: Add failing UI tests**

Test streaming debounce, tool spinner rendering, collapsible tool results, hunk actions, and settings persistence.

**Step 3: Implement token/tool streaming UX**

Character streaming with 16ms debounce; tool call progress + collapsible outputs.

**Step 4: Implement DiffReview UX**

Per-hunk accept/reject + bulk actions + red/green highlight using existing glass design patterns.

**Step 5: Implement settings enhancements**

Provider-grouped model picker (16 providers), keychain-backed API keys, presets Strict/Balanced/YOLO.

**Step 6: Verify, review, commit**

Run: `npm run test -- src/components`

```bash
git add src
git commit -m "feat(sprint-7): polish desktop streaming diff review and settings ux"
```

### Task 7: Sprint 8 - Plugin Ecosystem

**Files:**
- Modify: plugin SDK and loader paths (`docs/plugins`, `packages/extensions/plugins`, `src` plugin UI)
- Modify: `docs/examples/plugins/**/*`
- Add tests for SDK compatibility and MCP plugin lifecycle

**Step 1: Scrape plugin SDK and MCP-first reference code**

Collect compatibility expectations and lifecycle patterns.

**Step 2: Add failing tests**

SDK post-migration compatibility, MCP namespacing, uninstall cleanup.

**Step 3: Implement SDK compatibility fixes and example plugin updates**

Repair breakages in all 5 example plugins.

**Step 4: Implement plugin catalog UI**

Search/filter/details with Installed/Available tabs and persisted state via Tauri.

**Step 5: Implement MCP-as-plugin support**

Namespace tools as `server:tool_name`; enforce permissions and cleanup on uninstall.

**Step 6: Verify, review, commit**

Run: `npm run test -- packages/extensions/plugins src/components/plugins`

```bash
git add packages/extensions/plugins docs/examples/plugins src
git commit -m "feat(sprint-8): stabilize plugin sdk and add mcp-backed plugin catalog"
```

### Task 8: Sprint 9 - Testing and QA

**Files:**
- Modify/Create: `tests/e2e/**/*`
- Modify flaky tests: `src/components/chat/ChatView.integration.test.tsx`, extension loader tests
- Potential fixes across touched modules

**Step 1: Define and codify 10 real-backend e2e scenarios**

Add scenarios for edit cascade, grep, delegation, plugins, memory, permissions, sandbox, checkpoints, compaction, MCP.

**Step 2: Switch harness from mock to real backend**

Update setup/bootstrap and fixtures for real runtime execution.

**Step 3: Fix known flaky tests first**

Stabilize `ChatView.integration.test.tsx` and `extension-loader.test.ts` with deterministic waits/mocks where appropriate.

**Step 4: Run full validation matrix**

Run: `npm run test && cargo test --workspace`
Expected: PASS, no console errors, memory/startup goals validated by test harness metrics.

**Step 5: Reviewer gate and commit**

```bash
git add tests/e2e src/components/chat packages/extensions
git commit -m "test(sprint-9): move e2e to real backend and close flaky regressions"
```

### Task 9: Sprint 10 - Docs and Ship

**Files:**
- Modify: `CLAUDE.md`, `docs/backend.md`, architecture docs, troubleshooting docs, `docs/archives/project-history/memory-snapshot-v2.md`
- Delete: `BACKEND-SPRINT-BACKLOG-2026.md`, `BACKEND-SPRINT-BACKLOG-2026-AGGRESSIVE.md`, `rust-migration-boundaries.md`, `rust-backend-epic4-architecture.md`
- Modify: all `package.json` and `Cargo.toml` version fields
- Create/Modify: `CHANGELOG.md`

**Step 1: Add failing doc consistency checks (if available) and enumerate stale references**

Identify old dual-stack/migration references and stale paths.

**Step 2: Update canonical docs**

Reflect hybrid architecture, 20 extensions, ~39 tools, `dispatchCompute` pattern, and updated troubleshooting.

**Step 3: Remove stale docs and update SDK examples**

Delete obsolete files and verify plugin SDK examples still compile/run.

**Step 4: Bump versions to 2.0.0**

Apply across JS manifests and Cargo manifests.

**Step 5: Write changelog and run release verification**

Run: `npm run tauri build`
Expected: Successful desktop production build.

**Step 6: Reviewer gate and final commit**

```bash
git add CLAUDE.md docs CHANGELOG.md package.json Cargo.toml
git rm BACKEND-SPRINT-BACKLOG-2026.md BACKEND-SPRINT-BACKLOG-2026-AGGRESSIVE.md rust-migration-boundaries.md rust-backend-epic4-architecture.md
git commit -m "chore(sprint-10): finalize docs, version 2.0.0, and ship artifacts"
```

### Task 10: End-to-End Closeout

**Files:**
- Modify as needed for final fixes from review and verification

**Step 1: Run final full checks**

Run: `npm run lint && npm run format:check && npx tsc --noEmit && npm run test && cargo test --workspace`
Expected: PASS.

**Step 2: Confirm sprint commit chain exists and clean status**

Run: `git --no-pager log --oneline -n 12 && git status --short --branch`
Expected: sprint commits visible; clean working tree (or only intentional uncommitted follow-up).

**Step 3: Prepare ship summary**

Document sprint outcomes, reviewer findings resolved, and remaining known risks (if any).
