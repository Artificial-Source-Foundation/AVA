# Revised Migration Plan: Cut & Deepen

> Replaces the 41-sprint full-Rust rewrite with a 10-sprint hybrid approach.
> Goal: Ship a fast, maintainable app — not rewrite everything in Rust.

---

## Why This Plan Exists

The original plan (Epics 1-6, 41 sprints, 18 months) aimed to rewrite the entire TypeScript
backend in Rust. After auditing the codebase and comparing with competitors:

1. **54K LOC (core) + 25K LOC (extensions) = unmaintainable** — but the fix is deletion, not rewriting
2. **95% of agent time is waiting on LLM APIs** — Rust doesn't speed up `fetch()`
3. **Competitors ship with 4-25 tools in 12-25K LOC** — we have 55 tools in 80K LOC
4. **The Rust hotpaths are already built** — edit, grep, fuzzy, validation, permissions, memory, sandbox
5. **Rewriting orchestration (agent loop, LLM, MCP, commander) into Rust = zero user-facing speedup**

The revised approach: **Keep Rust for compute. Keep TypeScript for orchestration. Delete everything else.**

---

## Current State (as of 2026-03-04)

### Running Agents (do not conflict with)

| Agent | What it's building | Status |
|-------|-------------------|--------|
| Agent 1 | Epic 4 gaps: ava-db models, ava-extensions loading, app_state wiring, browser tool | Running |
| Agent 2 | Epic 3: ToolRegistry, ContextManager, AgentLoop, Commander, LLM, MCP, Session (Rust) | Running |
| Agent 3 | Epic 5 prep: TypeScript bridge layer, SolidJS hooks, E2E test harness | Running |

### What We'll Use From Each Agent

| Agent | Keep | Shelf (don't delete, just don't wire) |
|-------|------|---------------------------------------|
| Agent 1 | **Everything** — ava-db, app_state, extensions | — |
| Agent 2 | **ToolRegistry**, **ContextManager**, **SessionManager** | AgentLoop, Commander, LLM providers, MCP client (Rust versions) |
| Agent 3 | **Everything** — bridge layer, hooks, test harness | — |

---

## The 10-Sprint Plan

### Sprint A: The Great Deletion (1 week)
**Goal: Cut from 80K LOC to ~25K LOC**

**Kill `packages/core/` (54K lines)**
- This is the v1 monolith. core-v2 replaced it. Stop importing from it.
- The desktop app and CLI import from `@ava/core` — redirect to `@ava/core-v2`
- Delete the entire `packages/core/src/` directory
- Keep only `packages/core/package.json` as a re-export shim until all imports are updated

**Merge extensions from 37 modules to ~18**
Current 37 extension directories → proposed 18:

| Keep (18) | Merge into | Delete (reason) |
|-----------|-----------|-----------------|
| providers/ | — | Keep (16 LLM providers, well-isolated) |
| permissions/ | — | Keep (Rust handles compute, TS handles policy) |
| tools-extended/ | — | Keep but **cut from 27 to 15 tools** (see below) |
| prompts/ | — | Keep (system prompt building) |
| context/ | — | Keep (token tracking, compaction) |
| agent-modes/ | — | Keep (plan mode, minimal mode) |
| hooks/ | — | Keep (lifecycle middleware) |
| validator/ | — | Keep (QA pipeline) |
| commander/ | — | Keep (Praxis hierarchy) |
| mcp/ | — | Keep (MCP client — battle-tested) |
| git/ | — | Keep (git tools) |
| diff/ | — | Keep (diff tracking, review) |
| memory/ | — | Keep (persistent memory) |
| models/ | — | Keep (model registry) |
| plugins/ | — | Keep (plugin backend) |
| instructions/ | — | Keep (project instructions) |
| slash-commands/ | — | Keep (built-in /commands) |
| recall/ | — | Keep (session FTS5 search) |
| codebase/ | context/ | Merge repo map + symbols into context |
| focus-chain/ | agent-modes/ | Merge progress tracking into modes |
| file-watcher/ | hooks/ | Merge file watching into hooks |
| sharing/ | — | Delete (stub, never implemented) |
| scheduler/ | hooks/ | Merge into hooks lifecycle |
| skills/ | instructions/ | Merge into instructions |
| custom-commands/ | slash-commands/ | Merge (same concept) |
| integrations/ | tools-extended/ | Merge Exa into tools |
| sandbox/ | — | Delete TS version (Rust crate handles this) |
| server/ | — | Keep but extract to standalone (ACP REST) |
| recipes/ | — | Delete (unused, over-engineered) |
| profiles/ | — | Delete (low usage, settings handles this) |
| github-bot/ | — | Delete (separate service, not desktop app) |
| lsp/ | — | Keep (9 LSP tools, unique differentiator) |
| rules/ | permissions/ | Merge into permissions |

**Cut tools-extended from 27 to 15 tools**

Keep (15 — covers 95% of use cases):
- create_file, delete_file, apply_patch, multiedit, ls
- batch, question, todoread, todowrite, task
- websearch, webfetch, attempt_completion
- plan_enter, plan_exit

Drop (12 — niche or replaceable):
- codesearch → websearch covers this
- repo_map → Rust crate handles this, expose via invoke()
- bash_background/bash_output/bash_kill → pty tool handles this
- view_image → inline in chat rendering
- voice_transcribe → plugin, not core
- inline_suggest → plugin, not core
- edit_benchmark → dev-only, not a user tool
- session_cost → UI widget, not a tool
- create_rule → settings UI, not a tool
- create_skill → plugin CLI, not a tool

**Acceptance criteria:**
- [ ] `packages/core/` deleted (or reduced to re-export shim)
- [ ] Extensions: 37 → 18 directories
- [ ] Tools: 55+ → ~30 (7 core + 15 extended + 4 git + 4 memory)
- [ ] `pnpm build:all` succeeds
- [ ] `npm run test:run` passes (some tests will need updating)

---

### Sprint B: Wire Rust Hotpaths (1 week)
**Goal: Frontend calls Rust for all compute-heavy operations**

Using Agent 3's bridge layer (`src/services/rust-bridge.ts`), wire these paths:

| Operation | Before (TS) | After (Rust via invoke) |
|-----------|-------------|----------------------|
| Edit fuzzy match | core-v2 edit tool | `compute_fuzzy_replace` |
| File search (grep) | core-v2 grep tool | `compute_grep` |
| Memory CRUD | core-v2 memory extension | `memory_*` commands |
| Permissions check | core-v2 permissions ext | `evaluate_permission` |
| Validation | core-v2 validator ext | `validation_*` commands |
| Session storage | core-v2 session manager | `ava-session` crate (Agent 2) |
| PTY management | core-v2 pty tool | `pty_*` commands |

**What stays in TypeScript:**
- Agent loop (`packages/core-v2/src/agent/`) — orchestration, I/O bound
- LLM providers (`packages/extensions/providers/`) — HTTP calls
- MCP client (`packages/extensions/mcp/`) — JSON-RPC, battle-tested
- Commander/Praxis (`packages/extensions/commander/`) — delegation logic
- Context management (`packages/extensions/context/`) — strategy selection
- System prompts (`packages/extensions/prompts/`) — template building

**How the wiring works:**
```
core-v2 agent loop (TS)
  → calls edit tool
    → edit tool checks: running in Tauri?
      → YES: invoke('compute_fuzzy_replace', { old, new, content })
      → NO: use TypeScript implementation (CLI fallback)
```

Create `packages/core-v2/src/platform-dispatch.ts`:
- Detects runtime (Tauri vs Node.js)
- Routes compute calls to Rust (Tauri) or keeps in TS (Node/CLI)
- Single abstraction so tools don't need to know

**Acceptance criteria:**
- [ ] Edit uses Rust fuzzy match in desktop app
- [ ] Grep uses Rust compute_grep in desktop app
- [ ] Memory uses Rust SQLite in desktop app
- [ ] CLI still works with pure TS (no Tauri dependency)
- [ ] Benchmarks show measurable improvement on edit/grep

---

### Sprint C: Edit Excellence (1 week)
**Goal: Best-in-class edit tool — the #1 differentiator**

The Rust crate already has 9 edit strategies. Wire the 4-tier cascade:

**Tier 1: Exact match** (0ms)
- Direct string replacement — `ExactMatchStrategy`

**Tier 2: Flexible match** (1-5ms)
- Whitespace-tolerant — `FlexibleMatchStrategy`
- Indentation-aware — `IndentationAwareStrategy`
- Token boundary — `TokenBoundaryStrategy`

**Tier 3: Structural match** (5-20ms)
- Block anchor — `BlockAnchorStrategy`
- Line number — `LineNumberStrategy`
- Regex — `RegexMatchStrategy`
- Multi-occurrence — `MultiOccurrenceStrategy`

**Tier 4: Fuzzy + LLM self-correction** (20-200ms)
- Fuzzy match — `FuzzyMatchStrategy` (Levenshtein)
- If fuzzy fails → ask LLM to re-generate the edit with correct context
- Use `ava-agent` reflection loop for self-correction

**Streaming edits:**
- Apply edits as LLM tokens arrive (like Zed)
- Show per-hunk review UI (Agent 3 is building the component)
- Accept/reject individual hunks

**Acceptance criteria:**
- [ ] 4-tier cascade integrated end-to-end
- [ ] Edit success rate > 85% (test with `edit_benchmark` data)
- [ ] Streaming edit display works in UI
- [ ] Per-hunk accept/reject works

---

### Sprint D: Context Intelligence (1 week)
**Goal: Send the right code to the LLM, not all the code**

Wire Rust crate capabilities into TS context system:

**PageRank repo map** (Rust `ava-codebase`):
- `ava-codebase` already has: BM25 search, dependency graph, PageRank, file scoring
- Expose via Tauri command: `compute_repo_map(query, max_files) -> RankedFile[]`
- TS context extension uses ranked files instead of naive file listing

**Multi-strategy condensation** (Rust `ava-context`):
- Sliding window (drop oldest messages)
- Tool output truncation (keep first/last N lines of long outputs)
- Summary condensation (ask LLM to summarize old context)
- Wire Rust `ava-context` condenser as Tier 1 (fast), TS LLM summary as Tier 2 (smart)

**Token budget allocation:**
- System prompt: 15%
- Repo map: 20%
- Conversation history: 50%
- Tool results: 15%
- Enforce via TS context extension, count via Rust token tracker

**Acceptance criteria:**
- [ ] Repo map uses PageRank scoring
- [ ] Condensation uses 3+ strategies in cascade
- [ ] Token budget stays within model limits
- [ ] Context relevance measurably improved (manual QA)

---

### Sprint E: Agent Reliability (1 week)
**Goal: Agent doesn't get stuck, recovers from errors, knows when to stop**

**Stuck detection** (in TS agent loop):
- Track: same tool called with same args > 2 times = stuck
- Track: no file modifications in > 5 turns = spinning
- Track: token budget > 90% with no completion = winding down
- On stuck: inject steering message, switch strategy, or ask user

**Error recovery** (Rust `ava-validator` + `ava-agent`):
- After each edit: validate (syntax check, compilation check)
- On validation failure: auto-retry with reflection (Rust `reflect_and_fix`)
- Max 3 retries, then surface error to user
- Wire into TS agent loop as post-tool-execution hook

**Completion detection:**
- Don't rely solely on `attempt_completion` tool
- Detect implicit completion: no more tool calls, final message is a summary
- Add confidence scoring: did the agent actually achieve the goal?

**Acceptance criteria:**
- [ ] Agent breaks out of loops (stuck detection works)
- [ ] Failed edits auto-retry with validation
- [ ] Agent doesn't spin forever on impossible tasks
- [ ] Completion is reliable

---

### Sprint F: Sandbox & Safety (1 week)
**Goal: Users feel safe running the agent autonomously**

**OS-level sandbox** (Rust `ava-sandbox`):
- Already built: bwrap (Linux), sandbox-exec (macOS)
- Wire into bash tool: sandboxed execution by default for untrusted commands
- Expose via Tauri: `sandbox_run(command, policy) -> Output`

**Dynamic permissions** (Rust `ava-permissions`):
- Already built: static rules, bash classification, tree-sitter parsing
- Add learning: after user approves/denies, remember for similar commands
- Integrate with TS permissions extension for UI (approve/deny dialog)

**Git checkpoints:**
- Before each agent run: auto-commit or stash
- After each significant change: checkpoint
- On failure: offer rollback to last checkpoint
- Use existing git extension, add checkpoint logic

**Acceptance criteria:**
- [ ] Bash commands run in sandbox by default on Linux/macOS
- [ ] Permission decisions are remembered within session
- [ ] Git checkpoint created before each agent run
- [ ] Rollback works

---

### Sprint G: Desktop UX Polish (1 week)
**Goal: The app feels fast and looks good**

**Streaming tokens:**
- Real-time token display as LLM responds
- Typing animation, not batch rendering
- Wire Agent 3's `useRustAgent` hook

**Tool activity UI:**
- Show which tool is running, with spinner
- Show tool output in collapsible sections
- Show file diffs inline

**Settings & configuration:**
- LLM provider selection (16 providers)
- Model picker per task type
- Permission presets (strict / balanced / yolo)
- Plugin management UI

**Performance:**
- Startup < 500ms (Rust init is fast, TS extension loading is the bottleneck)
- Lazy-load extensions (don't activate all 18 on startup)
- Profile and fix any jank in SolidJS rendering

**Acceptance criteria:**
- [ ] Streaming tokens render in real-time
- [ ] Tool execution shows progress
- [ ] Settings UI works for all providers
- [ ] Startup is perceptibly fast

---

### Sprint H: Plugin Ecosystem (1 week)
**Goal: Users can create, share, and install plugins**

**Plugin SDK** (already documented in `docs/plugins/PLUGIN_SDK.md`):
- Verify the scaffold works: `ava plugin init my-plugin`
- Test: a plugin can register a tool, a command, and a hook
- Test: a plugin can be installed from local path

**Plugin catalog:**
- Frontend catalog UI (already in `src/stores/plugins-catalog.ts`)
- Install/uninstall from UI
- Enable/disable per-plugin

**Community starter plugins** (already in `docs/examples/plugins/`):
- Verify all 5 examples work with the slimmed-down extension API
- Update examples if extension merges broke anything

**Acceptance criteria:**
- [ ] `ava plugin init` scaffolds a working plugin
- [ ] Plugin installs from local path and loads
- [ ] Catalog UI shows available plugins
- [ ] At least 3 example plugins work end-to-end

---

### Sprint I: Testing & Platform QA (1 week)
**Goal: It works on every platform**

**E2E tests** (Agent 3's harness):
- Wire real Rust backend into test harness (replace mocks)
- Test: edit file via agent
- Test: search codebase
- Test: multi-file task with delegation
- Test: plugin load/unload

**Platform testing:**
- Linux: Ubuntu 22.04+, Fedora, Arch
- macOS: Intel + Apple Silicon
- Windows: 10, 11, WSL2

**Regression tests:**
- All 5,350+ existing tests still pass
- No console errors on clean startup
- Memory usage under 100MB idle

**Acceptance criteria:**
- [ ] E2E tests pass with real backend
- [ ] Tested on 3+ Linux distros, macOS (both archs), Windows
- [ ] Zero console errors on startup
- [ ] Memory < 100MB idle

---

### Sprint J: Ship v2.0 (1 week)
**Goal: Release**

- Version bump
- CHANGELOG
- Cross-platform builds (Tauri bundler)
- Auto-updater verification
- Announcement

---

## Architecture After Migration

```
┌─────────────────────────────────────────────┐
│              SolidJS Frontend                │
│         src/ (~55K lines, unchanged)         │
│                                              │
│  hooks: useAgent, useRustTools, useBackend   │
│  stores: session, settings, team, plugins    │
│  services: rust-bridge, core-bridge          │
└──────────┬──────────────┬───────────────────┘
           │              │
    Tauri invoke()    Direct import
           │              │
┌──────────▼──────┐ ┌─────▼──────────────────┐
│   Rust Crates   │ │  core-v2 + extensions  │
│   (compute)     │ │  (orchestration)       │
│                 │ │                        │
│  ava-tools      │ │  agent loop  (~2K LOC) │
│  ava-memory     │ │  LLM providers (16)    │
│  ava-permissions│ │  MCP client            │
│  ava-validator  │ │  commander/praxis      │
│  ava-context    │ │  context strategies    │
│  ava-codebase   │ │  prompts               │
│  ava-sandbox    │ │  hooks/middleware       │
│  ava-lsp        │ │  plugins               │
│  ava-session    │ │  18 extensions total   │
│  ava-db         │ │                        │
│  ava-config     │ │  ~15K LOC (down from   │
│  ava-platform   │ │   25K + 54K = 79K)     │
│  ava-logger     │ │                        │
└─────────────────┘ └────────────────────────┘
    ~3.5K LOC              ~15K LOC
     (done)            (after deletion)
```

**Total backend: ~18.5K LOC** (down from ~79K)
**Tools: ~30** (down from 55+, but each one deeper)
**Extensions: 18** (down from 37)

---

## Timeline

| Sprint | Duration | Focus | Blocked by |
|--------|----------|-------|------------|
| A | 1 week | The Great Deletion | Agents 1-3 finishing |
| B | 1 week | Wire Rust hotpaths | Sprint A |
| C | 1 week | Edit excellence | Sprint B |
| D | 1 week | Context intelligence | Sprint B |
| E | 1 week | Agent reliability | Sprint C, D |
| F | 1 week | Sandbox & safety | Sprint B |
| G | 1 week | Desktop UX polish | Sprint E |
| H | 1 week | Plugin ecosystem | Sprint A |
| I | 1 week | Testing & QA | Sprint G, H |
| J | 1 week | Ship v2.0 | Sprint I |

**Parallelizable:** C+D (both depend only on B), E+F (independent), G+H (independent)

**With parallelization: ~7 weeks to ship** (vs 18 months in original plan)

---

## What Happens to Agent 2's Extra Rust Code

Agent 2 is building: AgentLoop, Commander, LLM providers, MCP client — all in Rust.
We're not using those for v2.0, but they're not wasted:

- **v3.0 option:** If TS orchestration proves too slow or complex, the Rust versions exist
- **CLI-only mode:** A pure-Rust CLI could use Agent 2's crates without Node.js
- **Embedded/edge:** Rust agent could run on lower-powered devices

Don't delete them. Just don't wire them into the desktop app for v2.0.

---

## Success Metrics

| Metric | Current | Target | How |
|--------|---------|--------|-----|
| Backend LOC | 79K | 18.5K | Deletion + merging |
| Tool count | 55+ | ~30 | Cut niche tools |
| Extension count | 37 | 18 | Merge related modules |
| Edit success rate | ~60% | >85% | 4-tier cascade |
| Startup time | ~3s | <500ms | Lazy extension loading |
| Memory (idle) | ~300MB | <100MB | Rust for storage, less TS |
| Agent stuck rate | ~20% | <5% | Stuck detection + steering |
