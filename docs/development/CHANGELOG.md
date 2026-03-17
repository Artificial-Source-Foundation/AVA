# Changelog

## v2.2.0 (2026-03-17)

### Added
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

### Removed
- 30 unwired modules (~10.5K LOC dead code) moved to `docs/ideas/`
- Architect, build-race, reviewer, scheduler, guardian, and 25 more

### Changed
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
