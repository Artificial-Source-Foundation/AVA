# Changelog

## v2.2.3 (2026-03-19)

### Added
- **`--verbose` / `-v` CLI flag** — `-v` info, `-vv` debug, `-vvv` trace to stderr; overrides `RUST_LOG`
- **JSONL session logging** — structured logs at `~/.ava/log/` (opt-in via `features.session_logging: true`)
- **Ellipsis edit strategy** — handles `...` placeholder lines in `old_text`; 15 total strategies (was 14)
- **Rich edit error feedback** — on failure, reports most similar lines with line numbers and "did you mean?" hints

### Changed
- **OpenAI OAuth account routing** — derive `ChatGPT-Account-ID` from JWT claims when credentials do not already store it, covering older/device-code logins and token refreshes

#### God File Splits
- `crates/ava-praxis/src/lib.rs` 1,479 LOC split into 5 files: `lib`, `director`, `lead`, `worker`, `routing` (23 source files total)
- `crates/ava-agent/src/stack.rs` 1,763 LOC split into 4 files: `mod`, `stack_config`, `stack_tools`, `stack_run`
- `src-tauri/src/commands/agent_commands.rs` 1,044 LOC split into 3 files: `agent_commands`, `praxis_commands`, `helpers`

#### Quality & Security
- 6 production `unwrap()` calls replaced with proper error handling
- Extension native loader test fixed for cross-platform safety
- 0 clippy warnings, 0 dead code warnings across workspace
- Safe testing: nextest with 6 threads, `just check` tests 3 core crates (ava-agent, ava-tools, ava-praxis)
- 1,712 tests passing (was 1,692)

## v2.2.2 (2026-03-19)

### Added

#### Batch 1: Reliability & UX
- **Tool schema pre-validation** — catches malformed tool calls before execution, surfaces actionable errors
- **Stream silence timeout** — 90s configurable timeout with per-chunk reset, prevents hung streams
- **Auto-compaction toggle + threshold slider** — Settings-configurable compaction behavior

#### Batch 2: Cost & Accuracy
- **Anthropic prompt caching** — `cache_control` on system prompt + tool definitions, ~25% cost savings on cache hits
- **Auto-retry middleware for read-only tools** — 2x retry with exponential backoff for transient failures
- **tiktoken-rs BPE token counting** — accurate token counts replacing character-based heuristic

#### Batch 3: Edit Quality & Audit
- **Edit reliability cascade: 14 strategies** — added 3-way merge + diff-match-patch (was 12 strategies; later expanded to 15 with ellipsis strategy in v2.2.3)
- **Persistent audit log** — SQLite-backed, opt-out, queryable by session/tool

#### Praxis v2 (Phases 1-6 complete)
- **Phase 1**: Director brain + structured prompts + scout module
- **Phase 2**: Board of Directors — multi-model consensus with 3 distinct analytical personalities
- **Phase 3**: Plan tool with PlanBridge for agent-to-TUI communication, inline plan editing, step management
- **Phase 4**: Structured Praxis event system + Tauri event forwarding
- **Phase 5**: Scout implementation — lightweight agents for pre-planning codebase reconnaissance
- **Phase 6**: Integration + polish — plan tool registered in tool registry, 91 tests

#### Desktop UI
- **InlinePlanCard** + **PlanCard** + **PlanDock** components for Plannotator-style plan display
- **Plan persistence** service (`plan-persistence.ts`) for saving/loading plans

### Changed
- 1,692 tests passing (0 failures), up from 1,641 (further increased to 1,712 in v2.2.3)
- 91 Praxis tests (74 unit + 11 integration + 6 doc-tests)
- 3 Playwright e2e tests passing
- 0 clippy warnings, 0 TypeScript errors
- Praxis module count: 19 source files (was 15 pre-v2)

## v2.2.1 (2026-03-18)

### Added

#### Praxis v2 Architecture Design
- **LLM-powered Director** — replaces code-driven `pick_domain()` router with LLM-based task analysis and adaptive orchestration (3 intelligence levels)
- **Scout system** — lightweight agents (Haiku/Flash/Mercury) for pre-planning codebase intelligence gathering
- **Board of Directors** — multi-model consensus (3 SOTA models with distinct personalities) for complex architectural decisions
- **Plannotator-style plan system** — inline plan editing in chat (clickable steps, comments, reorder, budget per step) for both solo and Director modes
- **Sequential execution model** — Lead manages worker order (replaces self-claiming), parallel only when safe
- **Smart model routing** — automatic tier selection by role (scouts=cheap, workers=mid, leads=strong, director=strongest, board=top per provider)
- **Team configuration** — Settings → Agents page for presets, per-lead model/tool selection, board model selection
- **SOTA competitive analysis** — documented Claude Code, Cursor, Devin, Codex approaches; Google/MIT research findings on coordinator effectiveness

#### Desktop UI
- **Soft Zinc design system** — 12 reusable components (Toggle, Select, Badge, Card, etc.), CSS token alignment across all screens
- **5-step onboarding flow** — Welcome → Connect → Theme → Workspace → Ready
- **Project Hub** — time-based greeting, project cards with recent activity
- **Settings consolidation** — reduced from 15 → 11 tabs, full-screen layout with grouped sidebar
- **Welcome screen** with suggestion cards for new users
- **Loading screen** and **error screen** redesigned
- **Model browser** redesigned as grouped list (by provider)
- **Question dock** for agent questions during execution
- **Tool list dialog** accessible via command palette
- **Checkpoint save + restore dialog**
- **ThinkingDisplay setting** — Bubble/Preview/Hidden modes
- **LLM/Generation settings tab** wired to config
- **Progress + budget_warning events** handled in frontend

#### TUI Parity
- Keyboard shortcuts aligned with TUI (Ctrl+S/L/M/R, Tab mode cycle)
- Mid-stream messaging UI (queue badges, send during processing)
- Question modal for agent questions

#### Praxis Multi-Agent (Phases 1-6 Complete)
- **Phase 1: Director brain + prompts + scouts** — LLM-powered Director with 3 intelligence levels, scout system for pre-planning codebase intelligence, structured prompts module
- **Phase 2: Board of Directors** — multi-model consensus (3 SOTA models with distinct analytical personalities), vote synthesis for complex architectural decisions
- **Phase 3: Plan system** — `plan` tool with `PlanBridge` for agent-to-TUI communication, inline plan editing, step management (add/update/reorder), plan persistence to `.ava/plans/`
- **Phase 4: Events + Tauri bridge** — structured Praxis event system, Director/Lead/Worker/Scout lifecycle events, Tauri event forwarding
- **Phase 5: Scout implementation** — lightweight agents (Haiku/Flash/Mercury) for codebase reconnaissance, scout reports fed to Director for planning
- **Phase 6: Integration + polish** — 91 tests (74 unit + 11 integration + 6 doc-tests), plan tool registered in tool registry
- **Praxis UI design finalized** — Director Chat, Team Panel, Lead Chat screens designed in ava-ui.pen
- **Praxis design decisions documented** — naming convention (professional leads, fun worker names), tiered error handling, worktree-per-lead strategy, Solo/Team mode switching, budget delegation chain, session persistence
- TeamPanel wired to team store with stop buttons + metrics
- Worker naming pool (Pedro, Sofia, Luna, Kai, Mira, Rio, Ash, Nico, Ivy, Juno, Zara, Leo)
- Domain colors as constants, Team tab in RightPanel
- Agent-team-bridge event forwarding

### Changed
- Deduplicated 4 inline Toggles → shared a11y component
- Extracted format-time, ids, elapsed timer utilities (-800 LOC)
- Fixed CSS variable typo (`--alpha-white-05` → `--alpha-white-5`)
- Fixed tool call error indicators (findIndex ordering)
- Fixed thinking display (lastProcessedEventIdx reset)
- 38 Playwright e2e tests
- 0 TypeScript errors, 0 `as any` casts

## v2.2.0 (2026-03-17)

### Added
- **Web browser mode** — `ava serve --port 8080` starts HTTP API + WebSocket server
  - Session CRUD API endpoints (list, create, get, delete)
  - Async agent streaming via WebSocket
  - Mid-stream messaging API endpoints (steering, follow-up, post-complete)
  - Web DB fallback (routes SQL operations to HTTP API when no local SQLite)
  - Auto-titling for sessions
- **Power plugin system** — subprocess-isolated plugins via JSON-RPC
  - `ava-plugin` crate (manifest, discovery, hooks, runtime, manager)
  - 12 hook types (auth, tool.before/after, agent.before/after, session, etc.)
  - Plugin wired into AgentStack (hooks fire at all lifecycle points)
  - `@ava-ai/plugin` TypeScript SDK (zero dependencies)
  - 4 example plugins (hello-plugin, env-guard, request-logger, tool-timer)
  - CLI: `ava plugin list/add/remove/info` with npm install for deps
  - TUI: `/plugin` slash command
  - Plugin stderr inherited for dev visibility
  - Smoke tested end-to-end with gpt-5.3-codex
- Plugin system design doc with OpenCode 11-flaw analysis
- **Complete codebase documentation library** (`docs/codebase/`)
  - Documentation for all 21 Rust crates (public API, module maps, dependencies)
  - Frontend documentation (SolidJS, hooks, Tauri IPC, state management)
  - Tauri commands documentation (70+ commands across 29 source files)
  - Plugin system documentation (SDKs, hooks, examples, quick reference)
  - Dependency graph and "where to find things" quick reference
  - Index at `docs/codebase/README.md` with navigation table
- **Frontend wired to HTTP API + WebSocket** — SolidJS frontend connects to web server
- **Desktop parity**: Ctrl+T thinking toggle, Ctrl+Y copy response, 29 theme presets, `/later` and `/queue` slash commands

### Removed
- 30 unwired modules (~10.5K LOC dead code) moved to `docs/ideas/`
- Architect, build-race, reviewer, scheduler, guardian, and 25 more

### Changed
- Default model changed from gpt-4 to gpt-5.3-codex
- Console noise cleanup (reduced debug logging)
- Vite file watcher fix (`target/` directory excluded)
- CI consolidated (11 jobs down to 4)
- Documentation overhaul (README, crate-map, plugin guide, changelog)
- Updated CLAUDE.md and AGENTS.md with mandatory doc-update rules
- Development folder rebuilt (roadmap, backlog, epics, test-matrix)
- Codebase reduced from ~50K to ~40K LOC (20% leaner)

## v2.1.1 (2026-03-16)

### Added
- Conversation tree/branching (BG-10)
- Session bookmarks (BG-13)
- LiteLLM proxy compatibility (BG-14)
- Named configurable agents with build, plan, explore templates (BG-11)
- Tool output disk fallback for large outputs (BG-3, BG-4)
- Tool output pruning (BG-5)
- Ghost snapshot revert system (BG-6)
- Iterative compaction summaries (BG-7)
- Smart cut-point selection (BG-8)
- Branch summarization for `/btw` side conversations (BG-9)
- Direction-aware truncation (BG-12)
- Secret redaction in tool output
- Repetition inspector for stuck loop detection
- Turn diff tracker
- Focus chain for context tracking
- Tool call repair and retry-after header parsing
- OpenAI Responses API support for ChatGPT OAuth (gpt-5.x-codex models)
- Subscription cost hiding for ChatGPT OAuth and Copilot providers
- 20 new Tauri IPC commands closing 6 frontend gaps
- 8+ backend capabilities from competitive deep scrape (BG series)
- Equalizer bars spinner animation
- Session resume fidelity improvements
- Error recovery actions in TUI
- Colored inline diffs for edit/write/patch tool results
- Improved markdown rendering (headers, code blocks, links, lists)

### Removed
- 30 unwired modules moved to `docs/ideas/`
- ~10.5K lines of dead code cleaned up

### Changed
- Documentation overhaul (README, crate map, plugin guide, CLAUDE.md, AGENTS.md)
- Model selector keeps section headers during search
- Tool result preview overflow fix (single line + tab normalization)
- Vertical bleed cap and inline spinner for chat
- Fixed-width spinner for zero jitter
- Recursive strict schema for OpenAI Responses API
- Bash tail truncation for long output

### Security
- Persistent permissions (user-global scope)
- Config hardening (atomic writes, catalog validation, keychain)
- Auth hardening (timeouts, endpoint validation, safe URLs)
- Permission hardening (names, paths, sources, .ava/ split)
- Sandbox hardening (process control, env scrub, read-only)
- Symlink escape fix, contextual instruction trust gates
- Tool tracing, apply_patch boundaries
- Custom tool collision detection, web search validation
- MCP environment variable filtering (OPENROUTER_API_KEY, AVA_MASTER_PASSWORD)
- Trust gates for instructions/skills, shell escaping

## v2.1.0 (2026-03-08)

### Added
- Sprints 60-66 delivered: streaming tool calls, session/context UX, project instructions, TUI workflow polish
- Three-tier mid-stream messaging (steering, follow-up, post-complete pipelines)
- Reliable edit loop (RelativeIndenter, auto lint+test, smart `/commit`, ghost snapshots)
- Cost and runtime controls (thinking budgets, dynamic API keys, cost-aware routing, budget alerts)
- Pluggable backend operations, background agents on branches, file watcher mode
- Auto-learned project memories, multi-repo context, semantic codebase indexing
- Spec-driven development, agent artifacts, agent team peer communication, ACP
- Extended tools: web_search, AST ops, LSP ops, code search
- All v3 frontend/UX lanes delivered (ambient awareness, conversation clarity, session UX, Praxis chat, input/discoverability, desktop parity)
- `packages/` TypeScript layer deleted; desktop calls Rust directly via Tauri IPC

### Sprints Included
- Sprint 60: Streaming + session/context UX + project instructions
- Sprint 61: Reliable edit loop
- Sprint 62: Cost + runtime foundations (validated via 62V)
- Sprint 63: Execution + ecosystem foundations
- Sprint 64: Knowledge + context intelligence
- Sprint 65: Agent coordination backend
- Sprint 66: Optional capability backends

## v2.0.0 (2026-03-07)

### Added
- Sprints 51-59: TUI visual rework, OAuth providers, dynamic model catalog, thinking modes, coding plan providers (7 new), quality audit, modal system revamp, provider mega
- Copilot provider with GitHub token exchange
- Inception provider (Mercury 2 models)
- Compiled-in model registry with aliases and fuzzy normalization
- Rich StreamChunk (content, tool_call, usage, thinking, done)
- Circuit breaker wired into 5 remote API providers
- Retry jitter +/-20%
- 29 built-in themes + custom TOML themes

## v1.0.0 (2026-03-07)

Initial Rust-first release. Sprints 11-50f.

### Highlights
- Pure Rust CLI/TUI binary (Ratatui + Crossterm + Tokio)
- 7 LLM providers (Anthropic, OpenAI, Gemini, Ollama, OpenRouter, Copilot, Inception)
- 6 default tools (read, write, edit, bash, glob, grep) + 7 extended
- MCP extension system with TOML plugin support
- Multi-agent orchestration (Praxis)
- Code review agent (`ava review`)
- Voice input (Whisper API + local)
- Command sandboxing (bwrap/sandbox-exec)
- Session persistence (SQLite + FTS5)
- Persistent memory and codebase indexing (BM25 + PageRank)
- 340x faster cold start vs OpenCode, 31x less memory, 9.4x smaller binary
