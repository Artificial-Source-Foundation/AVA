# Changelog

## v2.2.7 (2026-03-25)

### Bug Fixes

- **Critical: Fix agent loop crash on missing tool output (#26)** — `add_tool_results()` index counter now only increments when a result is consumed, fixing index misalignment when `attempt_completion` tool calls are skipped. This caused "No tool output found for function call" 400 errors with OpenAI-format APIs.
- **Fix thinking level label 'max' → 'xhigh' (#24)** — `ThinkingLevel::Max.label()` now returns `"xhigh"` matching OpenAI API naming convention.
- **Fix ZAI provider doubled API path (#20)** — `completions_url()` now detects base URLs ending with `/v4` or `/v3` and appends only `/chat/completions` instead of `/v1/chat/completions`, fixing 404 errors for ZAI/ZhipuAI providers.
- **Fix collapsed tool results expansion (#23)** — Ctrl+E now toggles both thinking blocks AND tool action groups. Truncation hints updated to mention Ctrl+E.
- **Hot-reload provider credentials (#19)** — Newly added provider credentials via `/connect` or OAuth are now hot-reloaded into the agent stack's ModelRouter, no restart required.
- **Fix TUI composer text wrapping (#21)** — Long input lines in the composer now wrap at word boundaries instead of being truncated with "...".
- **Add 500 error hint to error rendering (#17)** — Server errors now show contextual hints suggesting retry or model switch.
- **Fix `git_read` retryable tool recognition** — Added `git_read` to the retry middleware's retryable tool list.

### Features

- **Implement `/init` slash command (#25)** — Scans project directory, detects tech stack (Rust/Node/Python/Go), and creates `AGENTS.md`, `.ava/mcp.json`, and `.ava/tools/hello.toml` with project-specific configuration.
- **Add ZAI/ZhipuAI models to registry (#18)** — Added 19 models to compiled-in registry: GLM-4.7, GLM-4.6, GLM-4.5, GLM-4.5 Flash, GLM-4.7 Flash, GLM-4 Plus, GLM-4 Long, GLM-4V Plus, CodeGeeX-4 for both `zai-coding-plan` and `zhipuai-coding-plan` providers.

### Closed (Already Implemented)

- **Bash tool default timeout (#22)** — Already has 120s default timeout with optional `timeout_ms` parameter override.

## v2.2.6 (2026-03-22)

### DX / Repo Hygiene

- **CI and release package-manager alignment** — GitHub workflows now consistently use pnpm for desktop dependency installation; release jobs use the correct `dtolnay/rust-toolchain` action.
- **Plugin workspace build script restored** — root `build:packages` script plus `pnpm-workspace.yaml` entries now cover `plugins/sdk` and example plugins again; TypeScript example entrypoints are now treated as generated build output rather than committed source.
- **Artifact hygiene tightened** — root `.gitignore` now excludes `.claude/`, `.playwright-mcp/`, root-level scratch PNGs, and `package-lock.json`; stale tracked Playwright screenshots plus root/plugin npm lockfiles were removed.
- **Legacy helper scripts refreshed** — `scripts/testing/verify-mvp.sh`, `scripts/testing/rust-migration-smoke.sh`, and `.opencode/context.md` now reflect the Rust-first + pnpm workflow.
- **CLI shootout harness** — `scripts/benchmarks/cli-shootout.mjs` adds a reproducible AVA-vs-OpenCode benchmark for offline startup costs and optional matched-model online tasks, with JSON/Markdown artifacts under `.tmp/benchmarks/`.
- **Copilot auth test fixed** — `ava auth test copilot` now accepts OAuth-only Copilot credentials instead of incorrectly requiring an API key.
- **OAuth auth-test clarity** — `ava auth test` now reports when OAuth credentials exist but are already expired, avoiding false confidence from presence-only checks.
- **Benchmark reporting improved** — the CLI shootout now defaults to 5 online samples and includes per-sample failure summaries in the Markdown report, making flaky Copilot/OpenAI runs easier to diagnose.
- **Fast benchmark mode** — AVA now supports `--fast` for lower-overhead headless runs by skipping project instruction injection and eager codebase indexing; the CLI shootout can enable it with `--ava-fast`.
- **Project instruction loading narrowed** — AVA no longer injects `CLAUDE.md` as project runtime instructions; trusted project instruction loading now centers on `AGENTS.md`, `.ava/rules/*.md`, `.ava/skills/`, and other explicit repo-local rule files.
- **On-demand project rules** — `.ava/rules/*.md` now activate lazily when the agent touches matching files instead of being front-loaded into every run.
- **Leaner system prompts** — base and provider-specific system prompts were trimmed so simple online tasks spend less time and budget on repeated orchestration prose.
- **Prompt telemetry** — AVA now logs estimated system-prompt token counts and on-demand rule token activation totals, making prompt-bloat regressions easier to spot.
- **Hot-path leanups** — native-tool providers no longer get duplicated tool descriptions in the system prompt, tool-definition hooks short-circuit when unused, model registry loading is cached, and memory/plugin startup enrichment now has tighter latency bounds.
- **Model-aware prompt profiles** — stronger native-tool models now get a leaner base prompt profile, reducing orchestration text without changing the default safety/instruction path for smaller models.
- **Simple-task concurrency widened** — `git` and `web_search` now run in the read-only concurrent batch, helping research-style turns overlap more I/O instead of serializing it.
- **Lazy snapshot startup** — shadow git snapshots now initialize on the first write batch instead of every run, so read-only tasks avoid extra rollback setup cost.
- **More plugin fast paths** — `SessionStart`, `SessionEnd`, `ChatMessage`, `AgentBefore`, `AgentAfter`, `ToolBefore`, `ToolAfter`, and event broadcast paths now skip hook work when no plugin subscribes.
- **Chat hook fast paths** — `chat.params`, `chat.messages.transform`, and `text.complete` now skip cloning/serde work when no plugin subscribes.
- **Cheaper trivial requests** — memory enrichment now skips obviously tiny prompts, index-status checks run in parallel with memory enrichment, and post-hook tool definitions are cached per loop.
- **Auto-lean runtime** — simple headless goals now keep `AGENTS.md`/project instructions but automatically skip eager codebase indexing, reducing normal-mode startup cost without requiring `--fast`.
- **Capability-driven lean prompts** — prompt-profile selection now uses model-registry capabilities and context limits instead of hardcoded model-name allowlists.
- **Phase telemetry** — startup logs now capture prompt-suffix resolution time/tokens, memory-enrichment time, index-status time, and run-scoped tool-registry build time.
- **Cheaper simple-task routing** — short edit-style requests now route to the cheaper profile more often instead of defaulting to the capable model for almost everything.
- **Startup work overlaps more** — prompt suffix resolution now overlaps with index-status and memory-enrichment startup work, and MCP init is skipped entirely when no MCP config files exist.
- **Answer-only and read-only task modes** — exact-reply tasks can now avoid tool exposure and registry setup entirely, while simple repo lookups keep only read-safe tools plus `AGENTS`-level startup context.
- **Harness accuracy improved** — the CLI shootout now reconstructs streamed AVA/OpenCode text from structured JSON events so exact-reply tasks are scored correctly.

### Providers

- **Azure OpenAI provider** — `crates/ava-llm/src/providers/azure.rs`. API key + deployment-based routing, configurable API version, full streaming support.
- **AWS Bedrock provider** — `crates/ava-llm/src/providers/bedrock.rs`. Self-contained SigV4 signing (SHA-256 + HMAC-SHA256), Anthropic Messages API format, separate invoke/streaming endpoints, 12 tests including crypto test vectors.
- **xAI, Mistral, Groq, DeepSeek providers** — OpenAI-compatible providers with correct base URLs and model routing.
- **ChatGPT provider alias** — explicit `chatgpt` provider name for Responses API routing (in addition to auto-detection via OpenAI OAuth).
- **Total: 22 providers** (was 15). Full list: Anthropic, OpenAI, ChatGPT, Gemini, Ollama, OpenRouter, Copilot, Inception, Alibaba, Alibaba CN, ZAI, ZhipuAI, Kimi, MiniMax, MiniMax CN, Azure OpenAI, AWS Bedrock, xAI, Mistral, Groq, DeepSeek, Mock.

### Mid-Stream Messaging Refactor

- **Queue/Interrupt/Post-complete** — renamed from Steering/Follow-up/Post-complete for clarity.
- **New keybindings**: Enter=queue, Ctrl+Enter=interrupt, Alt+Enter=post-complete, Double-Escape=cancel.
- **MessageQueueWidget** — renders above composer showing queued messages with reorder (up/down), edit (inline), remove per message.
- **Queue badges** — QUEUED (blue), INTERRUPT (amber) badges in UI.
- **Backward compatibility** — old steering/follow-up tier names still accepted.

### Session Persistence

- **Incremental message persistence** — messages saved incrementally as they arrive, not just on session close.
- **Session context preserved across cancel/continue** — context maintained when user cancels and resumes.
- **Crash recovery** — session state survives unexpected exits.

### Security

- **100+ security patterns** — command classifier expanded with comprehensive pattern matching (`crates/ava-permissions/src/classifier/rules.rs`, 728 LOC).
- **Symlink escape detection** — path guard detects and blocks symlink traversal outside project boundaries.
- **Quota error classification** — typed error variants for rate limits, quota exceeded, retry-after parsing.

### Context Management

- **Context overflow auto-compact** — 12 context overflow patterns detected (`crates/ava-llm/src/providers/common/overflow.rs`), agent loop auto-compacts and retries on overflow.
- **Conversation repair** — typed error recovery for malformed conversation state.
- **Dual compaction visibility** — compaction events visible in both TUI and web mode.

### File Operations

- **Shadow git snapshots** — `crates/ava-tools/src/core/file_snapshot.rs` creates git snapshots before file edits, enabling `revert_file` capability.
- **File edit backups** — every edit/write operation backed by snapshot for safe rollback.

### Agent / Core

- **Retry-after header parsing** — `crates/ava-llm/src/retry.rs` extracts and respects `Retry-After` headers from provider responses.
- **Typed error expansion** — `crates/ava-types/src/error.rs` expanded with 189+ LOC of new error variants for quota, overflow, and provider-specific errors.
- **Session types** — `crates/ava-types/src/session.rs` with 344 LOC of session metadata and state types.
- **Todo panel fix** — todos now correctly visible in sidebar panel.
- **Subagent stream error fix** — subagent streaming errors no longer crash parent agent.

### UI

- **Edit-and-resend** — properly deletes old messages when editing and resending.
- **Steering message position** — fixed incorrect positioning of steering messages in chat.
- **Chat completion flash** — eliminated visual flash when agent completes response.
- **Reasoning sentinel test** — steering loop correctly handles reasoning sentinel tokens.

## v2.2.5 (2026-03-21)

### Web Mode

- **Session ID unification** — frontend-to-backend session ID mapping ensures web mode session restore loads correct messages
- **Backend-only persistence model** — web mode uses backend session IDs for message sync, eliminating frontend/backend ID mismatch
- **UUID v4 message IDs** — message ID generation switched to UUID v4 for web mode compatibility
- **Message persistence fix** — UPDATE handler + PUT endpoint for web mode message content updates
- **Session message loading API** — new endpoint for loading session messages in web mode
- **axum route params fix** — route parameters use `{name}` syntax (not `:name`) per axum conventions
- **WebSocket reuse** — single WebSocket connection reused across agent runs in web mode

### Agent / Core

- **Assistant message content persistence** — assistant message content correctly persisted on session restore
- **Tool grouping in streaming** — tool calls properly grouped during streaming; thinking scroll and duplicate display fixed
- **MCP tool name compatibility** — dots replaced with underscores in MCP tool names for OpenAI provider compatibility

### MCP & Plugins

- **MCP stdio framing fix** — MCP stdio transport uses NDJSON (not Content-Length framing)
- **MCP tool name resolution** — tool name resolution and result feedback corrected
- **MCP race condition fix** — await init completion before first tool use
- **Lazy MCP init** — 30s timeout with parallel connections for MCP server initialization
- **MCP + plugin timeouts** — initialization timeouts added to prevent hung startups
- **23 plugin hooks wired** — all plugin hooks wired into agent runtime (full OpenCode parity)
- **Plugin system upgrade** — MCP HTTP transport, hooks, and resources support
- **MCP + plugin settings** — settings UI wired to real backend configuration

### Frontend (Web)

- **UI polish** — smooth transitions, descriptions, diff browser, spacing, typography, hover states
- **Todo panel** — sidebar todo panel with live agent updates
- **Thinking fixes** — markdown rendering, context window percentage display, streaming transitions
- **CSS audit** — scrollbar-thin, content-visibility, thinking styles cleaned up
- **47 Playwright e2e tests** — end-to-end test coverage for web mode

## v2.2.4 (2026-03-20)

### Tool Surface

- **9 default tools** — `web_fetch`, `web_search`, `git_read` promoted from Extended to Default tier
- **Extended tools no longer auto-registered** — `apply_patch`, `multiedit`, `ast_ops`, `lsp_ops`, `code_search`, `lint`, `test_runner` are now plugin-only (not loaded unless explicitly configured)
- Test: `default_tools_gives_9_tools` passes (was `_6_tools`)

### Security

- **SBPL injection hardening** — bash tool scrubs shell-breaking characters from arguments before execution
- **Env scrubbing** — sensitive environment variables stripped from bash tool environment
- **rm -rf hardening** — destructive rm variants blocked even in AutoApprove mode
- **find -delete blocking** — `find ... -delete` classified as Critical, blocked by permission middleware
- **Regex compile safety** — user-supplied regex patterns caught at compile time with actionable errors

### Performance

- **Blocking I/O → async** — file reads in read/write tools converted to `tokio::fs` async equivalents
- **Trust caching** — workspace trust checks cached per-path to avoid repeated filesystem hits
- **Connection pooling** — reqwest client reuse extended; per-provider pool prevents connection storms
- **ToolCall clone elimination** — `Arc<ToolCall>` replaces repeated `.clone()` in agent loop hot path
- **CodebaseIndex sharing** — single shared `Arc<CodebaseIndex>` across agent runs (was re-indexed per run)

### Error Handling

- **`From<io::Error>` preserves ErrorKind** — error kind forwarded through AvaError instead of being lost
- **Typed errors throughout** — remaining `map_err(|e| AvaError::Other(e.to_string()))` chains replaced with specific variants
- **Deprecated legacy AvaError variants** — `Other`, `Internal` marked deprecated; callers migrated to typed variants

### Testing

- **1,798 tests** (was 1,712; +86 new tests)
- **42+ new tests** — regex compile safety, permission middleware edge cases, budget tracking, agent loop integration tests
- **Web mode parity** — 14 new HTTP endpoints verified for desktop↔web feature parity

### Frontend (Desktop)

- **Debug log cleanup** — `console.log` / `console.error` calls removed from production paths
- **Dead code removed** — unused imports, unreachable branches, and stale state slices cleaned
- **Async prop fixed** — async accessor passed to SolidJS reactive context correctly

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
