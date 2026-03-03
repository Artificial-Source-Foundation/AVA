# Backend Backlog

> Systematic work plan for core-v2 + extensions — organized by priority tier.
>
> Gap analysis based on feature comparison with **8 competitors** (OpenCode, Gemini CLI, Aider, Goose, OpenHands, Plandex, Cline, Pi). Full competitive analysis: [`docs/research/competitive-analysis-2026-03.md`](../research/competitive-analysis-2026-03.md)

**Current snapshot:** 55+ tools, 30+ extensions, 16 providers. Detailed status: 55+ tools registered (7 core + 48+ extended), 34+ extensions active, 16 providers (all real implementations), ~5,350+ tests passing across 340+ files. CLI `ava agent-v2` works end-to-end with real LLMs — 5 E2E smoke tests passing (flat mode, Praxis mode, memory, git, background shell). All Praxis fixes landed (child CWD scoping, auto-detect flat vs hierarchy). **Desktop app fully integrated with core-v2:** chat uses AgentExecutor (unified middleware chain), bidirectional settings sync, extension event bridge hooks, context budget sync, model status hook. Full competitive parity + differentiation achieved (Sprint 23): session DAG/branching, token-efficient results, Azure provider, ambient terminal, Tauri PTY/watcher, dual-stack toggle, team delegation UI, client-server API (ACP), plugin marketplace reviews, recipe/workflow system, chat recall (FTS5), GitHub bot.

**AVA's unique advantages:** 3-tier Praxis hierarchy (no competitor has this), 50+ built-in tools (more than any competitor), Tauri desktop (native, not Electron), extension-first architecture, Obsidian-style plugin ecosystem.

---

## ~~Tier 0: Doc Accuracy~~ ✓ DONE

## ~~Tier 1: Critical Missing Tools~~ ✓ DONE

## ~~Tier 2: Agent Intelligence Tools~~ ✓ DONE

---

## Tier 3: Parity Essentials

Features both OpenCode and Goose have that users expect. Highest-impact gaps.

| # | Task | Competitor ref | What it does |
|---|------|----------------|-------------|
| ~~B-030~~ | ~~**LSP client** — spawn + JSON-RPC stdio~~ | ~~OpenCode has `lsp` tool~~ | **DONE** (Sprint 17: `LSPClient` with initialize, shutdown, hover, definition, references, diagnostics. Content-Length framed transport.) |
| ~~B-031~~ | ~~**LSP tool** — register callable tool~~ | ~~OpenCode (experimental)~~ | **DONE** (Sprint 17: 3 tools — `lsp_diagnostics`, `lsp_hover`, `lsp_definition`) |
| ~~B-032~~ | ~~**LSP server lifecycle** — lazy spawn, auto-restart~~ | ~~OpenCode~~ | **DONE** (Sprint 17: `LSPServerManager` with per-language lifecycle, detect/spawn/stop/restart) |
| ~~B-033~~ | ~~**Undo/Redo file changes**~~ | ~~OpenCode `/undo` `/redo`~~ | **DONE** (Sprint 23: `diff:undo` removes assistant message, `diff:redo` restores it. `removedMessages` map in diff extension.) |
| ~~B-034~~ | ~~**Parallel tool execution**~~ | ~~Both~~ | **DONE** (Sprint 19: `parallelToolExecution` config, `Promise.all()` for independent tools, `--sequential` flag) |
| ~~B-035~~ | ~~**Granular permission system**~~ | ~~OpenCode (per-tool allow/ask/deny + globs), Goose (4 modes)~~ | **DONE** (Sprint 19: 5 modes — suggest/ask/auto-edit/auto-safe/yolo, 6 tool categories, `isToolAutoApproved()`) |
| ~~B-036~~ | ~~**File @mentions in input**~~ | ~~OpenCode (`@` fuzzy file search)~~ | **DONE** (Sprint 20: `expandAtMentions()` in CLI, replaces `@path/to/file` with `<file>` content blocks) |
| ~~B-037~~ | ~~**Session export**~~ | ~~OpenCode (export + share links), Goose (markdown/JSON/YAML)~~ | **DONE** (Sprint 20: `exportSessionToMarkdown()` + `exportSessionToJSON()`, `/export` command) |

**Approach:**
- ~~B-030/031/032~~: **DONE** (Sprint 17). Full LSP client in `packages/extensions/lsp/src/`.
- B-033: New `undo` extension — snapshot file state before each write/edit/delete/apply_patch tool call, store in session. `/undo` command or `undo` tool restores + removes last assistant turn.
- B-034: Already partially designed (B-053 in old backlog). Change agent loop to `Promise.all()` independent tool calls.
- B-035: Extend existing `packages/extensions/permissions/`. Add config schema for per-tool overrides.
- ~~B-036~~: **DONE** (Sprint 20).
- ~~B-037~~: **DONE** (Sprint 20).

**Effort:** ~~2-3 sessions~~ → B-033 remaining (undo/redo), ~1 session

---

## Tier 4: Ecosystem Access

MCP unlocks 3,000+ tool integrations. Provider coverage removes onboarding friction.

### MCP Server Support — ✓ DONE

| # | Task | Status |
|---|------|--------|
| ~~B-040~~ | ~~**MCP local servers** — spawn stdio subprocess~~ | **DONE** (Sprint 13: `StdioTransport`, JSON-RPC 2.0) |
| ~~B-041~~ | ~~**MCP remote servers** — HTTP + SSE~~ | **DONE** (Sprint 13: `SSETransport` + Sprint 17: OAuth, reconnection) |
| ~~B-042~~ | ~~**MCP tool integration** — permission + namespacing~~ | **DONE** (Sprint 13: tools registered with `api.registerTool()` + Sprint 17: resources, prompts, sampling) |

MCP is feature-complete: tools, resources, prompts, sampling, OAuth, reconnection with backoff, transport error/close handling.

### Provider Coverage

| # | Provider | Base URL | Priority | Notes |
|---|----------|----------|----------|-------|
| B-043 | google (Gemini) | `generativelanguage.googleapis.com` | High | Custom API, needs own client (~200 lines). |
| B-044 | deepseek | `api.deepseek.com` | High | OpenAI-compat. Quick win. |
| B-045 | groq | `api.groq.com` | High | OpenAI-compat. Quick win. |
| B-046 | mistral | `api.mistral.ai` | Medium | OpenAI-compat. Quick win. |
| B-047 | ollama | `localhost:11434` | Medium | OpenAI-compat. Local models. No API key. |
| B-048 | xai (Grok) | `api.x.ai` | Low | OpenAI-compat. |
| B-049 | cohere | `api.cohere.ai` | Low | Custom API, needs own client. |
| B-050 | kimi | `api.moonshot.cn` | Low | OpenAI-compat. |
| B-051 | glm | `open.bigmodel.cn` | Low | OpenAI-compat. |
| B-052 | **AWS Bedrock** | regional endpoints | Medium | Goose + OpenCode both have it. Sig v4 auth. |
| ~~B-053~~ | ~~**Azure OpenAI**~~ | ~~`*.openai.azure.com`~~ | ~~Medium~~ | **DONE** (Sprint 23: `providers/azure/` — `api-key` header, deployment endpoint, reuses shared ToolCallBuffer/SSE. 16th provider.) |

**Effort:** ~2-3 sessions (1 for OpenAI-compat batch, 1 for Google/Cohere/custom, 1 for Bedrock/Azure)

---

## Tier 5: Agent Loop Hardening

| # | Task | Competitor ref | What it does |
|---|------|----------------|-------------|
| ~~B-060~~ | ~~**Background shell management**~~ | ~~Neither fully~~ | **DONE** (Sprint 19: `bash_background`, `bash_output`, `bash_kill` tools + `ProcessRegistry` singleton) |
| ~~B-061~~ | ~~**Streaming tool output**~~ | ~~Both stream~~ | **DONE** (Sprint 20: `onProgress` callback in ToolContext, `tool:progress` event, bash stdout wired) |
| ~~B-062~~ | ~~**Tool result truncation**~~ | ~~Both do this~~ | **DONE** (Sprint 19: `truncateToolResults()` — 50KB per result, 200KB total, `[...truncated]` marker) |
| ~~B-063~~ | ~~**Token-efficient tool results**~~ | ~~Both optimize~~ | **DONE** (Sprint 23: `efficient-results.ts` — normalizeWhitespace, stripAnsi, smartSummarize, groupGrepResults, efficientToolResult dispatcher. Applied in agent loop.) |
| ~~B-064~~ | ~~**Auto-formatter after edits**~~ | ~~OpenCode (per-extension formatters)~~ | **DONE** (Sprint 20: formatter middleware at priority 50, auto-detects biome/prettier/deno, runs after write/edit/create/apply_patch) |
| ~~B-065~~ | ~~**Image/vision support**~~ | ~~Both~~ | **DONE** (Sprint 19: `ImageBlock` type, vision in agent loop, OpenAI-compat image conversion) |

**Effort:** ~2-3 sessions

---

## Tier 6: Extension Wiring + Intelligence

| # | Task | Extension | Current gap |
|---|------|-----------|-------------|
| ~~B-070~~ | ~~Wire validator to agent loop~~ | ~~validator~~ | **DONE** (Sprint 17: `enabledByDefault: true`, `agent:completing` handler wired, runs validation pipeline) |
| ~~B-071~~ | ~~Wire git snapshots to CLI~~ | ~~git~~ | **DONE** (Sprint 20: integration test verifying snapshot middleware, 10 tests) |
| ~~B-072~~ | ~~Wire instructions auto-inject~~ | ~~instructions~~ | **DONE** (Sprint 19: AGENTS.md + CLAUDE.md discovery, Promise-based wait, getSettings crash fix) |
| ~~B-073~~ | ~~Wire focus-chain tracking~~ | ~~focus-chain~~ | **DONE** (Sprint 17: event names fixed to `turn:start`/`turn:end`/`agent:finish`, payload casts fixed) |
| ~~B-074~~ | ~~Wire diff tracking~~ | ~~diff~~ | **DONE** (Sprint 20: already fully implemented with before/after middleware, undo/redo stacks, event handlers) |
| ~~B-075~~ | ~~Wire scheduler~~ | ~~scheduler~~ | **DONE** (Sprint 20: memory extension emits `scheduler:register` for periodic flush, `store.flush()` method) |
| ~~B-076~~ | ~~Add `skill` tool~~ | ~~skills~~ | **DONE** (Sprint 20: `load_skill` tool — loads skill by name, registered in skills extension) |
| ~~B-077~~ | ~~**Custom user tools**~~ | ~~tools-extended~~ | **DONE** (Sprint 20: `loadCustomTools()` discovers from `.ava/tools/` + `~/.ava/tools/`, dynamic import, auto-register) |
| ~~B-078~~ | ~~**Persistent memory**~~ | ~~new extension~~ | **DONE** (Sprint 17: `MemoryStore` with 4 tools — `memory_write`/`memory_read`/`memory_list`/`memory_delete`, system prompt injection) |
| ~~B-079~~ | ~~**Auto-generate project rules**~~ | ~~instructions~~ | **DONE** (Sprint 20: `/init` command scans project, detects language/framework/test-runner/formatter/linter, generates `CLAUDE.md`) |

**Effort:** ~~3 sessions~~ → **ALL DONE**

---

## Tier 6.5: Praxis Agent Quality (E2E Smoke Test Findings)

Issues discovered during full E2E smoke test (2026-03-01). The 3-tier Praxis hierarchy works but has rough edges.

| # | Task | Severity | What's wrong |
|---|------|----------|-------------|
| ~~P-01~~ | ~~**Child agent cwd scoping**~~ | ~~HIGH~~ | **DONE** (Sprint 20: git/diff extensions prefer `ctx.workingDirectory` over cached cwd, delegate emits `session:child-opened` event) |
| ~~P-02~~ | ~~**`create_file` fails in workers**~~ | ~~HIGH~~ | **DONE** (Sprint 20: `create_file` catch block only ignores EEXIST/EISDIR, no longer swallows permission errors) |
| ~~P-03~~ | ~~**Auto-detect flat vs hierarchy**~~ | ~~MEDIUM~~ | **DONE** (Sprint 20: `selectAgentMode()` heuristic in agent-modes extension, wired to CLI via dynamic import) |
| ~~P-04~~ | ~~**Redundant file reads across tiers**~~ | ~~MEDIUM~~ | **DONE** (Sprint 20: `files` param on delegate tools injects `<file>` content blocks into child goal, avoiding re-reads) |
| ~~P-05~~ | ~~**Worker tool failures cascade**~~ | ~~LOW~~ | **DONE** (Sprint 20: budget awareness instruction in child system prompt — "You have N turns. If >50% used without progress, call attempt_completion with partial result.") |

**Approach:**
- ~~P-01/P-02~~: **DONE** — git/diff `cwd` preference fixed + `session:child-opened` event + `create_file` error handling.
- ~~P-03~~: **DONE** — `selectAgentMode()` complexity heuristic.
- ~~P-04~~: **DONE** — Shared file cache via `files` param on delegate tools.
- ~~P-05~~: **DONE** — Budget instruction appended to child system prompts.

**Effort:** ~~1-2 sessions~~ → **ALL DONE**

---

## Tier 7: Desktop App Integration

Connect core-v2 to the desktop app (currently uses legacy `packages/core/`).

| # | Task | What it does |
|---|------|-------------|
| ~~B-080~~ | ~~Create Tauri bridge for core-v2~~ | **DONE** (Sprint 23: `TauriPTY` via `@tauri-apps/plugin-shell`, `TauriFileWatcher` via `@tauri-apps/plugin-fs`, wired into `createTauriPlatform()`) |
| ~~B-081~~ | ~~Stream events to SolidJS UI~~ | **DONE** (Sprint 21: `useExtensionEvents.ts` bridges `onEvent()` → SolidJS signals. `stream-lifecycle.ts` maps `AgentEvent` → chat UI. `useModelStatus.ts` for model events. Context budget sync via reactive `budgetTick`.) |
| ~~B-082~~ | ~~Tool approval dialog~~ | **DONE** (Sprint 21: Chat now uses `AgentExecutor` which runs through full middleware chain including permission middleware. Tool approval already wired from prior work.) |
| ~~B-083~~ | ~~Session persistence with core-v2~~ | **DONE** (Sprint 21: `App.tsx` uses shared `getCoreSessionManager()` instead of duplicate. Bidirectional settings sync via `settings-sync.ts`.) |
| ~~B-084~~ | ~~Dual-stack toggle~~ | **DONE** (Sprint 23: `agentBackend: 'core' | 'core-v2'` in settings with default `'core-v2'`. CLI `ava run --backend` flag.) |

**Effort:** ~~3-4 sessions~~ → B-080 (Tauri bridge) remaining, B-084 wontfix

---

## Tier 7.5: Competitive Gaps (2026-03 Analysis)

Gaps identified from comprehensive analysis of 8 competitors + Pi baseline. See [`docs/research/competitive-analysis-2026-03.md`](../research/competitive-analysis-2026-03.md).

| # | Task | Source | Priority | What it does |
|---|------|--------|----------|-------------|
| ~~CG-01~~ | ~~**Auto-compaction**~~ | ~~Pi, OpenCode, Gemini, Cline~~ | ~~HIGH~~ | **DONE** (Sprint 18+19: threshold-based compaction, strategy selection summarize/truncate, `context:compacted` event) |
| ~~CG-02~~ | ~~**Parallel tool execution**~~ | ~~Gemini CLI scheduler~~ | ~~HIGH~~ | **DONE** (Sprint 19: same as B-034) |
| ~~CG-03~~ | ~~**Git snapshot/undo**~~ | ~~OpenCode, Aider~~ | ~~HIGH~~ | **DONE** (Sprint 19: per-tool-call checkpoint middleware. Sprint 20: full /undo integration verified — 8 integration tests covering write→undo→restore→redo chain) |
| ~~CG-04~~ | ~~**Cross-provider message normalization**~~ | ~~Pi `transform-messages.ts`~~ | ~~MEDIUM~~ | **DONE** (Sprint 18: `normalize.ts` message normalizer) |
| ~~CG-05~~ | ~~**Steering interrupts**~~ | ~~Pi agent loop~~ | ~~MEDIUM~~ | **DONE** (Sprint 18+19: `steer()` method, `agent:steered` event, abort remaining tools on interrupt) |
| ~~CG-06~~ | ~~**Session DAG/tree**~~ | ~~Pi session manager~~ | ~~MEDIUM~~ | **DONE** (Sprint 23: `parentSessionId`, `branchName`, `branchPoint`, `children` on SessionState. `fork()`, `getTree()`, `getBranches()`. DAG helpers: `getAncestors()`, `getDescendants()`, `flattenTree()`, `findRoot()`, `getDepth()`.) |
| ~~CG-07~~ | ~~**Plugin tool hooks**~~ | ~~OpenCode~~ | ~~MEDIUM~~ | **DONE** (Sprint 20: `tool:before-register` event emitted in registry, plugins can mutate tool definitions before registration) |
| ~~CG-08~~ | ~~**Cross-tool SKILL.md compat**~~ | ~~OpenCode, Gemini CLI~~ | ~~MEDIUM~~ | **DONE** (Sprint 18: skills loader discovers from `.ava/skills/`, `.claude/skills/`, `.agents/skills/`) |
| ~~CG-09~~ | ~~**Git worktree isolation**~~ | ~~OpenCode~~ | ~~MEDIUM~~ | **DONE** (Sprint 20: `createWorktree()`/`removeWorktree()`, `isolation` config in delegation, per-session branches) |
| ~~CG-10~~ | ~~**Model packs**~~ | ~~Plandex~~ | ~~LOW~~ | **DONE** (Sprint 20: 3 built-in packs — budget/balanced/premium, `applyModelPack()` in commander, `resolveModelForTier()`) |

**Approach:**
- ~~CG-01/04/05~~: **DONE**
- ~~CG-02/03~~: **DONE**
- CG-06: Large architectural change to session storage — deferred
- ~~CG-07/08~~: **DONE**
- ~~CG-09/10~~: **DONE**

---

## Tier 8: Differentiation (Post-Parity)

Features that go beyond parity and lean into AVA's unique strengths.

| # | Task | Inspiration | What it does |
|---|------|-------------|-------------|
| ~~B-090~~ | ~~**Plugin marketplace**~~ | ~~Obsidian community plugins~~ | **DONE** (Sprint 23: `ReviewStore` with ratings/reviews. `sortCatalog()`, `filterCatalog()`. `PluginCard.tsx`, `ReviewModal.tsx` UI.) |
| ~~B-091~~ | ~~**Client-server architecture**~~ | ~~OpenCode (OpenAPI 3.1 spec)~~ | **DONE** (Sprint 23: `server/` extension — `node:http` REST API (POST /run, GET /stream, POST /steer, GET /status, DELETE, health). Token auth, SSE streaming, session router. ACP implemented.) |
| ~~B-092~~ | ~~**Recipe/workflow system**~~ | ~~Goose (YAML + cron)~~ | **DONE** (Sprint 23: `recipes/` extension — JSON/YAML recipes with `{{param}}` substitution. Sequential + parallel steps, conditions. `.ava/recipes/` discovery.) |
| ~~B-093~~ | ~~**GitHub/GitLab bot**~~ | ~~OpenCode (`/opencode` in issues)~~ | **DONE** (Sprint 23: `github-bot/` extension — HMAC-SHA256 webhook, @ava mention extraction, PR/issue context, result posting.) |
| ~~B-094~~ | ~~**Chat recall**~~ | ~~Goose~~ | **DONE** (Sprint 23: `recall/` extension — FTS5 indexer, BM25 search, `recall` tool + `/recall` command. Cross-branch search via DAG ancestors.) |
| ~~B-095~~ | ~~**Ambient terminal**~~ | ~~Goose (`@goose` one-shots)~~ | **DONE** (Sprint 23: `cli/src/ambient/install.ts` — shell functions for bash/zsh/fish. `ava ambient install/uninstall`.) |
| ~~B-096~~ | ~~**Dev Team delegation UI**~~ | ~~AVA-unique~~ | **DONE** (Sprint 23: `WorkerDetail.tsx`, `DelegationLog.tsx`, `TeamMetrics.tsx`. `DelegationEvent` type, delegation store with team-wide memos.) |

**Effort:** ~~6+ sessions~~ → **ALL DONE** (Sprint 23)

---

## Priority Order

```
Tier 0  ──→  ✓ DONE
Tier 1  ──→  ✓ DONE
Tier 2  ──→  ✓ DONE
Tier 3  ──→  ✓ DONE — B-033 undo/redo completed (Sprint 23)
Tier 4  ──→  ✓ DONE — MCP feature-complete, all 16 providers (Azure added Sprint 23)
Tier 5  ──→  ✓ DONE — B-063 token-efficient results completed (Sprint 23)
Tier 6  ──→  ✓ DONE — All 10 items complete
Tier 6.5──→  ✓ DONE — All 5 Praxis quality items complete
Tier 7  ──→  ✓ DONE — B-080 Tauri bridge + B-084 dual-stack toggle (Sprint 23)
Tier 7.5──→  ✓ DONE — CG-06 session DAG completed (Sprint 23)
Tier 8  ──→  ✓ DONE — All 7 differentiation items completed (Sprint 23)
```

**ALL BACKEND TIERS COMPLETE.** Only remaining work is frontend polish.

---

## Tracking

Each item has a stable ID (B-0xx) for reference in commits and PRs.

```
feat(tools): add websearch tool [B-010]
feat(agent): parallel tool execution [B-034]
```

---

## Done

| # | Task | Date |
|---|------|------|
| — | Core-v2 agent loop end-to-end (Phases 1-6) | 2026-02-27 |
| — | CLI build without tsx | 2026-02-27 |
| — | All 16 tools tested and working | 2026-02-27 |
| — | Provider/model inheritance for subagents | 2026-02-27 |
| — | Tool error propagation to LLM | 2026-02-27 |
| B-001 | Fix CLAUDE.md tool count + file count | 2026-02-27 |
| B-002 | Fix backend.md module/test counts | 2026-02-27 |
| B-003 | Fix plan-mode ALLOWED_TOOLS dead refs | 2026-02-27 |
| B-010 | Port `websearch` tool | 2026-02-28 |
| B-011 | Port `webfetch` tool | 2026-02-28 |
| B-012 | Port `browser` tool | 2026-02-28 |
| B-013 | Port `apply_patch` tool | 2026-02-28 |
| B-020 | Add `codesearch` tool | 2026-02-28 |
| B-021 | Add `repo_map` tool | 2026-02-28 |
| B-023 | Add `plan_enter` / `plan_exit` tools | 2026-02-28 |
| B-030 | LSP client — JSON-RPC stdio + Content-Length framing | 2026-02-28 |
| B-031 | LSP tools — lsp_diagnostics, lsp_hover, lsp_definition | 2026-02-28 |
| B-032 | LSP server lifecycle — per-language spawn/stop/restart | 2026-02-28 |
| B-040 | MCP local servers — stdio transport | 2026-02-28 |
| B-041 | MCP remote servers — SSE + OAuth | 2026-02-28 |
| B-042 | MCP tool integration + resources + prompts + sampling | 2026-02-28 |
| B-070 | Wire validator to agent loop | 2026-02-28 |
| B-073 | Wire focus-chain tracking | 2026-02-28 |
| B-078 | Persistent cross-session memory (4 tools) | 2026-02-28 |
| — | SQLite session storage (SessionStorage interface) | 2026-02-28 |
| — | Symbol extraction (regex-based, 5 languages) | 2026-02-28 |
| — | Provider tests for 10 remaining providers | 2026-02-28 |
| B-072 | Wire instructions auto-inject (AGENTS.md + CLAUDE.md) | 2026-03-01 |
| — | Fix instructions extension getSettings crash | 2026-03-01 |
| — | Replace 200ms setTimeout race with Promise-based wait | 2026-03-01 |
| — | Full E2E smoke test with Praxis hierarchy | 2026-03-01 |
| B-034 | Parallel tool execution (Promise.all, configurable) | 2026-03-02 |
| B-035 | Granular permission modes (5 modes, 6 categories) | 2026-03-02 |
| B-060 | Background shell tools (bash_background/output/kill) | 2026-03-02 |
| B-062 | Tool result truncation (50KB/result, 200KB total) | 2026-03-02 |
| B-065 | Image/vision support (ImageBlock, agent loop, OpenAI compat) | 2026-03-02 |
| CG-01 | Auto-compaction improvements (strategy selection) | 2026-03-02 |
| CG-02 | Parallel tool execution (same as B-034) | 2026-03-02 |
| CG-05 | Steering interrupts (steer method, agent:steered event) | 2026-03-02 |
| — | Tool registry change notifications (tools:registered/unregistered events) | 2026-03-02 |
| — | MCP health monitoring (configurable ping, auto-restart) | 2026-03-02 |
| — | MCP streamable HTTP transport (bidirectional, session mgmt) | 2026-03-02 |
| — | Praxis orchestrator (auto-planning, batch parallel execution) | 2026-03-02 |
| — | Task routing integration (analyzeDomain, auto-select worker) | 2026-03-02 |
| — | Per-domain tool filtering (frontend/backend/QA/fullstack leads) | 2026-03-02 |
| — | Result aggregation (files changed, tests run, issues found) | 2026-03-02 |
| — | Error recovery in delegation (retry with context, escalation) | 2026-03-02 |
| — | Parallel agent execution in Praxis (independent subtasks) | 2026-03-02 |
| — | Git tools: create_pr, create_branch, switch_branch, read_issue | 2026-03-02 |
| — | Per-tool-call checkpoints (git stash middleware, rollback) | 2026-03-02 |
| — | Toolshim for non-tool-calling models (XML parse/inject) | 2026-03-02 |
| — | Auto-learning memory (tech-stack, test-framework, language detect) | 2026-03-02 |
| — | Model availability tracking + fallback chains | 2026-03-02 |
| — | Global doom loop detection (cross-agent, registry-level) | 2026-03-02 |
| — | Plugin install/uninstall backend (local, GitHub, catalog API) | 2026-03-02 |
| P-01 | Child agent CWD scoping fix (git/diff prefer ctx.workingDirectory) | 2026-03-02 |
| P-02 | create_file silent failure fix (only ignore EEXIST/EISDIR) | 2026-03-02 |
| P-03 | Auto-detect flat vs hierarchy (selectAgentMode heuristic) | 2026-03-02 |
| P-04 | Shared file cache across tiers (files param on delegate tools) | 2026-03-02 |
| P-05 | Worker turn budget awareness (budget instruction in system prompt) | 2026-03-02 |
| B-036 | File @mentions in CLI input (expandAtMentions) | 2026-03-02 |
| B-037 | Session export (markdown/JSON + /export command) | 2026-03-02 |
| B-061 | Streaming tool output (onProgress callback, tool:progress event) | 2026-03-02 |
| B-064 | Auto-formatter after edits (biome/prettier/deno middleware) | 2026-03-02 |
| B-071 | Git snapshots integration test (10 tests) | 2026-03-02 |
| B-074 | Diff tracking verified (already fully implemented) | 2026-03-02 |
| B-075 | Scheduler consumer wired (memory flush + scheduler:register) | 2026-03-02 |
| B-076 | load_skill tool (skills extension) | 2026-03-02 |
| B-077 | Custom user tools (.ava/tools/ + ~/.ava/tools/ discovery) | 2026-03-02 |
| B-079 | /init command (project scanner + CLAUDE.md generator) | 2026-03-02 |
| CG-03 | /undo integration verified (8 integration tests, full chain) | 2026-03-02 |
| CG-07 | Plugin tool hooks (tool:before-register event) | 2026-03-02 |
| CG-08 | SKILL.md cross-tool compatibility verified | 2026-03-02 |
| CG-09 | Git worktree isolation (createWorktree/removeWorktree) | 2026-03-02 |
| CG-10 | Model packs (budget/balanced/premium + applyModelPack) | 2026-03-02 |
| — | Comprehensive smoke tests (44 tools, 23 extensions, 5 permission modes) | 2026-03-02 |
| — | 5 E2E CLI smoke tests (flat, praxis, memory, git, background) | 2026-03-02 |
| B-081 | Stream AgentEvents to SolidJS UI (useExtensionEvents hooks, budget sync) | 2026-03-02 |
| B-082 | Tool approval via AgentExecutor middleware chain (chat unified) | 2026-03-02 |
| B-083 | Session persistence with shared getCoreSessionManager() | 2026-03-02 |
| — | Bidirectional settings sync (core ↔ frontend, loop prevention) | 2026-03-02 |
| — | Chat → AgentExecutor unification (stream-lifecycle.ts refactor) | 2026-03-02 |
| — | Context budget sync (context:compacting + agent:finish event subs) | 2026-03-02 |
| — | Model status hook (useModelStatus, models:updated/ready events) | 2026-03-02 |
| — | Plugin hook system (registerHook/callHook on ExtensionAPI) | 2026-03-02 |
| — | Prompt caching (cache_control markers on Anthropic/OpenRouter) | 2026-03-02 |
| — | LSP 6 additional operations (documentSymbols, workspaceSymbols, codeActions, rename, references, completions) | 2026-03-02 |
| — | Model-family system prompts (detectModelFamily, FAMILY_PROMPT_SECTIONS) | 2026-03-02 |
| — | Session archival + slug + busy state + cursor pagination | 2026-03-02 |
| — | Tool output saved to file on truncation (output-files.ts) | 2026-03-02 |
| — | Tool call repair (repairToolName, 4 strategies) | 2026-03-02 |
| — | Agent step limits (maxSteps, MAX_STEPS terminate mode) | 2026-03-02 |
| — | Subdirectory AGENTS.md walking (subdirectory.ts, 3-layer dedup) | 2026-03-02 |
| — | URL instructions (url-loader.ts, 5s timeout, remote scope) | 2026-03-02 |
| — | Compaction prune strategy (40K token budget, protected tools) | 2026-03-02 |
| — | Session status events (session:status on status changes) | 2026-03-02 |
| — | Bash parsing + arity fingerprinting (bash-parser.ts, arity.ts) | 2026-03-02 |
| — | Provider transforms (filterEmptyContentBlocks, enforceAlternatingRoles, truncateMistralIds) | 2026-03-02 |
| — | LiteLLM provider (OpenAI-compat at localhost:4000/v1) | 2026-03-02 |
| — | File watcher extension (polling, .git/HEAD, git:branch-changed) | 2026-03-02 |
| — | Structured output (__structured_output tool, forced tool_choice) | 2026-03-02 |
| — | Per-message overrides (_system, _format, _variant on ChatMessage) | 2026-03-02 |
| — | PTY tool (getPlatform().pty, ANSI stripping, progress) | 2026-03-02 |
| — | Task resumption (task_id param for session resume) | 2026-03-02 |
| — | Explore subagent (read-only worker, deniedTools enforcement) | 2026-03-02 |
| — | Diff enhancements (toolCallIndex, messageIndex, diff:revert-to, summarizeDiffSession) | 2026-03-02 |
| — | Plan saves to file (.ava/plans/<timestamp>-<slug>.md) | 2026-03-02 |
| — | Session sharing extension stub (/share command) | 2026-03-02 |
| — | Retry-after header parsing (3 formats: ms, seconds, HTTP date) | 2026-03-02 |
| — | Well-known org config (fetchWellKnownConfig from /.well-known/ava) | 2026-03-02 |
| — | OpenAI Responses API (shouldUseResponsesAPI for GPT-5/o3/o4/Codex) | 2026-03-02 |
| — | ACP protocol stub (run/stream/steer methods) | 2026-03-02 |
