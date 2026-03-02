# Backend Backlog

> Systematic work plan for core-v2 + extensions — organized by priority tier.
>
> Gap analysis based on feature comparison with **8 competitors** (OpenCode, Gemini CLI, Aider, Goose, OpenHands, Plandex, Cline, Pi). Full competitive analysis: [`docs/research/competitive-analysis-2026-03.md`](../research/competitive-analysis-2026-03.md)

**Current state:** 43 tools registered (6 core + 37 extended), 28 extensions active, 14 providers (all real implementations), ~4,280 tests passing. CLI `ava agent-v2` works end-to-end with real LLMs. AGENTS.md + CLAUDE.md instruction injection confirmed working via smoke test. Parallel tool execution, image/vision, MCP HTTP streaming, auto-learning memory, model availability/fallback, plugin install backend all implemented.

**AVA's unique advantages:** 3-tier Praxis hierarchy (no competitor has this), 35 built-in tools (more than any competitor), Tauri desktop (native, not Electron), extension-first architecture, Obsidian-style plugin ecosystem.

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
| B-033 | **Undo/Redo file changes** | OpenCode `/undo` `/redo` | Track file snapshots per tool call. Undo reverts files + removes message. Redo restores both. |
| ~~B-034~~ | ~~**Parallel tool execution**~~ | ~~Both~~ | **DONE** (Sprint 19: `parallelToolExecution` config, `Promise.all()` for independent tools, `--sequential` flag) |
| ~~B-035~~ | ~~**Granular permission system**~~ | ~~OpenCode (per-tool allow/ask/deny + globs), Goose (4 modes)~~ | **DONE** (Sprint 19: 5 modes — suggest/ask/auto-edit/auto-safe/yolo, 6 tool categories, `isToolAutoApproved()`) |
| B-036 | **File @mentions in input** | OpenCode (`@` fuzzy file search) | Fuzzy file picker in CLI input. Selected file contents injected into context. |
| B-037 | **Session export** | OpenCode (export + share links), Goose (markdown/JSON/YAML) | Export conversations to markdown/JSON. Optional shareable links. |

**Approach:**
- ~~B-030/031/032~~: **DONE** (Sprint 17). Full LSP client in `packages/extensions/lsp/src/`.
- B-033: New `undo` extension — snapshot file state before each write/edit/delete/apply_patch tool call, store in session. `/undo` command or `undo` tool restores + removes last assistant turn.
- B-034: Already partially designed (B-053 in old backlog). Change agent loop to `Promise.all()` independent tool calls.
- B-035: Extend existing `packages/extensions/permissions/`. Add config schema for per-tool overrides.
- B-036: CLI readline hook that intercepts `@` and runs fuzzy glob search.
- B-037: New function in `packages/core-v2/src/session/` — serialize session messages to markdown/JSON.

**Effort:** ~2-3 sessions (was 4-5, LSP done)

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
| B-053 | **Azure OpenAI** | `*.openai.azure.com` | Medium | OpenCode has it. Custom auth + deployment IDs. |

**Effort:** ~2-3 sessions (1 for OpenAI-compat batch, 1 for Google/Cohere/custom, 1 for Bedrock/Azure)

---

## Tier 5: Agent Loop Hardening

| # | Task | Competitor ref | What it does |
|---|------|----------------|-------------|
| ~~B-060~~ | ~~**Background shell management**~~ | ~~Neither fully~~ | **DONE** (Sprint 19: `bash_background`, `bash_output`, `bash_kill` tools + `ProcessRegistry` singleton) |
| B-061 | **Streaming tool output** | Both stream | Stream bash output to UI as it happens. Needs `onProgress` callback in ToolContext. |
| ~~B-062~~ | ~~**Tool result truncation**~~ | ~~Both do this~~ | **DONE** (Sprint 19: `truncateToolResults()` — 50KB per result, 200KB total, `[...truncated]` marker) |
| B-063 | **Token-efficient tool results** | Both optimize | Strip redundant whitespace, structured summaries for large outputs. |
| B-064 | **Auto-formatter after edits** | OpenCode (per-extension formatters) | Hook into tool middleware `afterExecute`. Run configured formatter on written files. |
| ~~B-065~~ | ~~**Image/vision support**~~ | ~~Both~~ | **DONE** (Sprint 19: `ImageBlock` type, vision in agent loop, OpenAI-compat image conversion) |

**Effort:** ~2-3 sessions

---

## Tier 6: Extension Wiring + Intelligence

| # | Task | Extension | Current gap |
|---|------|-----------|-------------|
| ~~B-070~~ | ~~Wire validator to agent loop~~ | ~~validator~~ | **DONE** (Sprint 17: `enabledByDefault: true`, `agent:completing` handler wired, runs validation pipeline) |
| B-071 | Wire git snapshots to CLI | git | Middleware registered but untested with real repos |
| ~~B-072~~ | ~~Wire instructions auto-inject~~ | ~~instructions~~ | **DONE** (Sprint 19: AGENTS.md + CLAUDE.md discovery, Promise-based wait, getSettings crash fix) |
| ~~B-073~~ | ~~Wire focus-chain tracking~~ | ~~focus-chain~~ | **DONE** (Sprint 17: event names fixed to `turn:start`/`turn:end`/`agent:finish`, payload casts fixed) |
| B-074 | Wire diff tracking | diff | No tool reports diffs back to agent |
| B-075 | Wire scheduler | scheduler | Runner exists but nothing schedules tasks |
| B-076 | Add `skill` tool | skills | Agent can't explicitly load skills. Needs tool wrapper. |
| B-077 | **Custom user tools** | tools-extended | Let users drop `.ts`/`.js` files in `.ava/tools/` or `~/.ava/tools/`. Auto-discover + register via ExtensionAPI. OpenCode and Goose both have this. |
| ~~B-078~~ | ~~**Persistent memory**~~ | ~~new extension~~ | **DONE** (Sprint 17: `MemoryStore` with 4 tools — `memory_write`/`memory_read`/`memory_list`/`memory_delete`, system prompt injection) |
| B-079 | **Auto-generate project rules** | instructions | `/init` command scans project structure and generates `CLAUDE.md` with relevant instructions. OpenCode has this. |

**Effort:** ~3 sessions

---

## Tier 6.5: Praxis Agent Quality (E2E Smoke Test Findings)

Issues discovered during full E2E smoke test (2026-03-01). The 3-tier Praxis hierarchy works but has rough edges.

| # | Task | Severity | What's wrong |
|---|------|----------|-------------|
| P-01 | **Child agent cwd scoping** | HIGH | `ls`, `glob`, and `read_file` in child agents sometimes resolve paths against AVA's project root instead of the `--cwd` passed to the CLI. Causes phantom files (e.g. `vitest.config.ts` appearing in a bare project). |
| P-02 | **`create_file` fails in workers** | HIGH | Worker agents can't use `create_file` (silently fails). Falls back to `write_file` which works. Likely same cwd/path resolution issue as P-01. |
| P-03 | **Auto-detect flat vs hierarchy** | MEDIUM | Simple tasks (single file edit, "say hello") shouldn't use 3-tier delegation. Add heuristic: if goal is short + few files, use flat agent mode. Reserve Praxis for multi-file/multi-domain tasks. Avoids the 414s-for-a-simple-task problem. |
| P-04 | **Redundant file reads across tiers** | MEDIUM | Every tier re-reads the same files independently (math.ts was read 10+ times). Parent→child context passing should include already-read file contents or a shared file cache. |
| P-05 | **Worker tool failures cascade** | LOW | When tester worker can't find `package.json`/`vitest.config.ts`, it burns through turns trying alternatives. Workers should have a "no test framework found, create minimal setup" fallback or report back quickly. |

**Approach:**
- P-01/P-02: Root cause is likely in `AgentExecutor` child agent creation — `cwd` not propagated to child's `ToolContext`. Fix in `commander/src/delegate.ts`.
- P-03: Add complexity heuristic in commander's `filterTools()` — if goal tokens < threshold and no multi-domain keywords, bypass delegation and run flat.
- P-04: Add `parentContext.files` map to delegation payload. Workers check cache before reading.
- P-05: Add turn budget awareness to workers — if >50% turns used without progress, `attempt_completion` with partial result.

**Effort:** ~1-2 sessions

---

## Tier 7: Desktop App Integration

Connect core-v2 to the desktop app (currently uses legacy `packages/core/`).

| # | Task | What it does |
|---|------|-------------|
| B-080 | Create Tauri bridge for core-v2 | Wire `platform-tauri` + core-v2 agent loop into Tauri commands |
| B-081 | Stream events to SolidJS UI | Map `AgentEvent` types to existing UI components (message bubbles, tool cards, typing indicator) |
| B-082 | Tool approval dialog | Wire permission middleware's bus events to existing `ToolApprovalDialog.tsx` |
| B-083 | Session persistence with core-v2 | Use core-v2 `SessionManager` with Tauri's SQLite database |
| B-084 | Dual-stack toggle | Allow switching between core and core-v2 in settings (for gradual migration) |

**Effort:** ~3-4 sessions (big integration milestone)

---

## Tier 7.5: Competitive Gaps (2026-03 Analysis)

Gaps identified from comprehensive analysis of 8 competitors + Pi baseline. See [`docs/research/competitive-analysis-2026-03.md`](../research/competitive-analysis-2026-03.md).

| # | Task | Source | Priority | What it does |
|---|------|--------|----------|-------------|
| ~~CG-01~~ | ~~**Auto-compaction**~~ | ~~Pi, OpenCode, Gemini, Cline~~ | ~~HIGH~~ | **DONE** (Sprint 18+19: threshold-based compaction, strategy selection summarize/truncate, `context:compacted` event) |
| ~~CG-02~~ | ~~**Parallel tool execution**~~ | ~~Gemini CLI scheduler~~ | ~~HIGH~~ | **DONE** (Sprint 19: same as B-034) |
| CG-03 | **Git snapshot/undo** | OpenCode, Aider | HIGH | **PARTIAL** (Sprint 19: per-tool-call checkpoint middleware via `git stash create`. Full `/undo` command still needed.) |
| ~~CG-04~~ | ~~**Cross-provider message normalization**~~ | ~~Pi `transform-messages.ts`~~ | ~~MEDIUM~~ | **DONE** (Sprint 18: `normalize.ts` message normalizer) |
| ~~CG-05~~ | ~~**Steering interrupts**~~ | ~~Pi agent loop~~ | ~~MEDIUM~~ | **DONE** (Sprint 18+19: `steer()` method, `agent:steered` event, abort remaining tools on interrupt) |
| CG-06 | **Session DAG/tree** | Pi session manager | MEDIUM | Store sessions as DAG (parent IDs per entry) instead of flat list. Enables non-destructive branching, tree navigation, branch summaries. |
| CG-07 | **Plugin tool hooks** | OpenCode | MEDIUM | `plugin.tool.definition` event lets plugins mutate tool descriptions/parameters before execution. Small addition to ExtensionAPI. |
| CG-08 | **Cross-tool SKILL.md compat** | OpenCode, Gemini CLI | MEDIUM | Load `.claude/skills/**/SKILL.md` and `.agents/skills/**/SKILL.md` alongside AVA's own skill convention. Instant ecosystem bridge. |
| CG-09 | **Git worktree isolation** | OpenCode | MEDIUM | Per-session git worktrees so agents work on isolated branches. Critical for "yolo" autonomous mode safety. |
| CG-10 | **Model packs** | Plandex | LOW | Named model combinations per agent tier (e.g., "budget": Haiku workers, Sonnet leads, Opus commander). |

**Approach:**
- CG-01/04/05: Small changes to agent loop — can be done in 1 session
- CG-02/03: Same as existing B-034/B-033 items — medium effort
- CG-06: Large architectural change to session storage — deferred
- CG-07/08: Small extension API additions — quick wins
- CG-09/10: Medium effort, not blocking

---

## Tier 8: Differentiation (Post-Parity)

Features that go beyond parity and lean into AVA's unique strengths.

| # | Task | Inspiration | What it does |
|---|------|-------------|-------------|
| B-090 | **Plugin marketplace** | Obsidian community plugins | Browse, install, rate community plugins from within the app. Registry API + UI. |
| B-091 | **Client-server architecture** | OpenCode (OpenAPI 3.1 spec) | Run AVA as a server with REST API. Enables IDE plugins, web interface, remote control. |
| B-092 | **Recipe/workflow system** | Goose (YAML + cron) | Parameterized workflow definitions. Composable, schedulable, shareable. |
| B-093 | **GitHub/GitLab bot** | OpenCode (`/opencode` in issues) | Mention `@ava` in issues/PRs to trigger agent runs in CI. |
| B-094 | **Chat recall** | Goose | Semantic search across all session history. |
| B-095 | **Ambient terminal** | Goose (`@goose` one-shots) | `@ava "quick question"` from normal shell without entering REPL. |
| B-096 | **Dev Team delegation UI** | AVA-unique | Visual representation of Team Lead → Senior Leads → Junior Devs task delegation in desktop app. |

**Effort:** ~6+ sessions (long-term roadmap)

---

## Priority Order

```
Tier 0  ──→  ✓ DONE
Tier 1  ──→  ✓ DONE
Tier 2  ──→  ✓ DONE
Tier 3  ──→  MOSTLY DONE — LSP ✓, parallel ✓, permissions ✓. Remaining: @mentions, session export (1 session)
Tier 4  ──→  ✓ DONE — MCP feature-complete (tools, resources, prompts, sampling, OAuth, reconnect, HTTP streaming). All 14 providers tested.
Tier 5  ──→  MOSTLY DONE — Background shell ✓, truncation ✓, vision ✓. Remaining: streaming output, formatters (1 session)
Tier 6  ──→  MOSTLY DONE — Validator ✓, focus-chain ✓, memory ✓, instructions ✓, auto-learn ✓. Remaining: scheduler, custom tools, /init (1-2 sessions)
Tier 6.5──→  Praxis agent quality: cwd scoping, flat mode fallback, shared context (1-2 sessions)
Tier 7  ──→  Desktop integration: Tauri bridge, events, sessions (3-4 sessions)
Tier 8  ──→  Differentiation: marketplace, server API, recipes, GitHub bot (6+ sessions)
```

**Total to full parity:** ~4-6 sessions (remaining Tier 3 + 5 + 6 items)
**Total to desktop launch:** ~8-10 sessions (+ Tier 7)
**Total to differentiation:** ~14-16 sessions (all tiers)

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
