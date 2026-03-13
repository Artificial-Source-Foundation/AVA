# AVA Sprint Roadmap

> Last updated: 2026-03-13 (Sprint 62V closeout complete; Sprints 60-62 archived; Sprint 63-66 remain active planning lanes on master)

## Completed (Sprints 11–59)

| Sprint | Focus |
|--------|-------|
| 11–15 | Foundation (TypeScript core, extensions, desktop) |
| 16a | Rust agent stack (core tools, AgentStack, sandbox) |
| 16b | Ratatui TUI (8 features, 41 files) |
| 16c | Credential store + provider auth |
| 17 | Praxis v2 multi-agent hierarchy |
| 18 | CLI agent providers |
| 19 | Send-safe AgentStack + headless CLI |
| 20 | Agent loop intelligence (system prompts, native tool calling, streaming) |
| 21 | TUI polish (streaming render, tool approval, markdown, sessions) |
| 22 | Praxis Director E2E (SharedProvider, --multi-agent CLI) |
| 23 | Code quality (runner split, stuck detection, /model switching) |
| 24 | Competitive Rust architecture analysis |
| 25 | Structured error types + RetryBudget |
| 26 | Hybrid context compaction (3-stage pipeline) |
| 27 | Two-layer client (ConnectionPool + send_with_retry) |
| 28 | Tool monitoring (usage patterns, loop detection) |
| 29 | Permission inspector (SafetyTag, RiskLevel, 3 policy presets) |
| 30 | TUI token buffer (60fps) + structured tracing |
| 31 | MCP Extension System (transport, client, config, manager, bridge) |
| 32 | Integration testing + performance benchmark vs OpenCode |
| 33 | Critical bug fixes (TUI 400, large file crash, empty turns, default config) |
| 34a | TUI research — OpenCode & Codex CLI frontend deep-dive |
| 34 | TUI parity (welcome screen, rich status bar, keyboard hints, command palette, model selector, ! shell, TTL messages) |
| 35 | Agent intelligence mega (smart completion, token counting, chunk-aware truncation, prompt caching, circuit breaker, self-correction) |
| 37 | Developer workflow mega (multiedit, apply-patch, test runner, lint, diagnostics, LSP client, word-level diffs) |
| 41 | Context & memory mega (relevance-aware context, codebase auto-index, memory/session/codebase tools, auto-context injection) |
| 39 | MCP expansion + TOML plugin system + tool discovery UI + hot-reload |
| 43 | Performance (connection pre-warming, request pipelining, streaming tool results, cost tracker, provider fallback) |
| 45 | Safety (command classifier, path safety, risk-aware approval UI, audit log, extended tool profiles) |
| 46 | Multi-agent workflows (planner→coder→reviewer pipeline, feedback loops, workflow CLI) |
| 47 | Code review agent (`ava review`, structured output, CI exit codes, env var config) |
| 48 | Voice input (Whisper API + local, silence detection, Ctrl+V hotkey, --voice mode) |
| 50a | Bug hunt (15 edge-case tests, error message audit) |
| 50b | Headless E2E tests (12 real-provider integration tests) |
| 50c | TUI interaction tests (10 TestBackend UI tests) |
| 50d | Performance regression check + release build verification |
| 50e | Documentation audit (CLAUDE.md, README, docs index, --help) |
| 50f | DX hardening (split oversized files, eliminate unwraps, doc comments, workspace config) |
| 99 | Codebase housekeeping (docs cleanup, crate doc comments, archives organized) |
| 100 | v2.1 release documentation, test matrix, version bump |

| 51a | TUI visual rework (capybara mascot, dark theme, status bar) |
| 51b | TUI commands + auth (slash commands, command palette, keybinds) |
| 52 | OAuth providers (OpenRouter OAuth, provider connect modal) |
| 53 | Dynamic model catalog (models.dev API, curated whitelist, cache, ID mapping) |
| 54 | Thinking/reasoning modes (per-provider variants, /think command, Ctrl+T cycle) |
| 55 | Coding plan providers (Alibaba, ZAI, ZhipuAI, Kimi, MiniMax — 7 new providers) |
| 56 | Codebase quality audit (6 parallel sub-agents, read-only, structured reports) |
| 57 | Quality fixes — P0 critical (panics, tests, dead_code) + P1 high (docs, modularity) |
| 58 | Modal system revamp — shared SelectList widget, scroll fix, visual redesign |
| 59 | Provider mega — Copilot provider, provider verification, retry jitter ±20%, circuit breaker wiring, compiled-in model registry, rich StreamChunk, Alibaba Coding Plan fixes, context window display, error text wrapping, dedup guard fix |

## Recently Completed (Sprints 60-62 + 62V)

| Sprint | Focus | Status |
|--------|-------|--------|
| 60 | Streaming tool calls + session/context UX + project instructions + TUI workflow polish | Implemented and archived |
| 61 | Reliable edit loop (`B67`, `B54`, `B37`, `B66`) | Implemented and archived |
| 62 | Cost + runtime foundations (`B64`, `B63`, `B47`, `B40`) | Implemented, validated, and archived via Sprint 62V |
| 62V | Validation + archive closeout for Sprint 62 | Complete |

### Sprint 60 Completed Items

- **Project instructions system** — auto-discovers `AGENTS.md`, `CLAUDE.md`, `.cursorrules`, `.github/copilot-instructions.md`, `~/.ava/AGENTS.md`, `.ava/rules/*.md` and injects into system prompt (`crates/ava-agent/src/instructions.rs`)
- **B34: Three-tier mid-stream messaging** — `MessageQueue` with steering/follow-up/post-complete pipelines (12 tests). Agent loop polls steering between tool calls, skips remaining tools on steer. Follow-up loop after task. Post-complete grouped pipeline (G1, G2, G3...) after all work. TUI keybinds: Enter=steer, Alt+Enter=follow-up, Ctrl+Alt+Enter=post-complete. CLI flags: `--follow-up`, `--later`, `--later-group`. Slash commands: `/later`, `/queue`.
- **Sprint 60 core interaction fixes** — streaming tool-call rendering, persistent conversation history, chat scroll fixes, session sidebar/resume flow, and last-model persistence landed in code and are reflected in the Sprint 60 prompt set.
- **Sprint 60 TUI workflow additions** — `/btw`, rewind, `/export`, `/compact`, `/init`, custom slash commands, `/copy`, hooks, background agents, and Claude Code subagent support are implemented in code and remain tracked in `docs/development/backlog.md` as pending manual testing.
- **Sprint 60 shipped backlog closures** — B81 tool surface rationalization, B62 cross-provider message normalization, B60 Rust CI, B43 image input, B51 hash-anchored edits, B35 mention scoping, B36 diff preview, B70 Plan mode file writes, and B31 praxis rename/director are all recorded as completed in the backlog.

## Active v3 Delivery Lanes

v3 is the point where the current active backlog is intentionally burned down, validated, and reorganized into a cleaner release story.

Sprints 60-62 are now archived as completed delivery history. Sprint 63-66 remain planning docs and active delivery lanes.

### Backend Lane (Rust-first)

| Sprint | Focus | Status |
|--------|-------|--------|
| 63 | Execution + ecosystem foundations (`B65`, `B39`, `B61`, `B71`, `B45`) | Planned |
| 64 | Knowledge + context foundations (`B38`, `B57`, `B58`, `B48`) | Planned |
| 65 | Agent coordination backend (`B50`, `B59`, `B49`, `B76`) | Planned |
| 66 | Optional capability backends (`B44`, `B52`, `B53`, `B69`) | Planned (Extended/plugin-first) |

### Frontend / UX Lane (paired with the backend lane)

| Track | Focus | Paired Backend Sprint |
|-------|-------|-----------------------|
| FE-A | Ambient awareness (`context %`, modular footer, duration/cost visibility) | 62 |
| FE-B | Conversation clarity (tool grouping, inline diffs, quieter streaming) | 62 |
| FE-C | Session and history UX (session search, rewind preview, session stats) | 63 |
| FE-D | Praxis chat UX (`B26` composer entry, worker visibility, task status) | 65 |
| FE-E | Input + discoverability (shortcut help, richer command discovery, long-input polish) | 64 |
| FE-F | Desktop parity follow-through for proven TUI patterns | 66 |

### Ordering Notes

- `B26` remains the highest-priority open UI-facing item and should land before or together with Sprint 65's coordination work.
- Sprints 60, 61, and 62 are treated as done-for-planning and live in the sprint archive.
- The backend lane stays Rust-first; the frontend lane is limited to TUI and desktop surfaces for already-approved backend capabilities.

Detailed sprint planning docs live in:

- `docs/development/sprints/sprint-63/overview.md`
- `docs/development/sprints/sprint-64/overview.md`
- `docs/development/sprints/sprint-65/overview.md`
- `docs/development/sprints/sprint-66/overview.md`

Archived implemented sprint docs live in:

- `docs/development/sprints/archive/sprint-60/overview.md`
- `docs/development/sprints/archive/sprint-61/overview.md`
- `docs/development/sprints/archive/sprint-62/overview.md`
- `docs/development/sprints/archive/sprint-62v/overview.md`

## Status: v2.1 Released + Active Development

v2.1.0 released on 2026-03-08. Sprints 51-62 materially improved model intelligence, provider coverage, runtime reliability, streaming UX, workflow safety, and cost visibility. Sprint 62V closed remaining Sprint 62 validation/archival work. Current focus is backlog cleanup plus paired backend/frontend planning toward v3 while Sprint 63-66 remain roadmap targets rather than completed implementation on `master`.

## Codebase Stats (lightweight repo snapshot)

| Metric | Value |
|--------|-------|
| Rust crates | 20 |
| Rust source files | 268 |
| Lines of Rust | ~77,000 |
| Tracked test files | 338 |
| Clippy | Clean |
| Built-in tools | 6 |
| Extended tools | 7 |

## Parallelism Guide (Historical)

Sprints that ran simultaneously:
- **34 + 35** (TUI vs agent core — zero overlap)
- **37 + 41** (workflow tools vs context system)
- **39 + 43 + 45** (extensibility vs performance vs safety — zero overlap)
- **47 + 48** (code review vs voice — independent)
- **50b + 50c + 50d + 50e + 50f** (all independent stabilization tracks)

## Combined Sprints

| Original | Combined Into | Reason |
|----------|--------------|--------|
| 35 + 36 + 43 (partial) | **Sprint 35** | All agent intelligence / ava-llm / ava-context — no file overlap |
| 37 + 38 | **Sprint 37** | Developer workflow tools — all in ava-tools + ava-lsp |
| 41 + 42 | **Sprint 41** | Context + memory — ava-codebase + ava-context + ava-memory + ava-session |
| 39 + 40 | **Sprint 39** | Extensibility — MCP expansion + TOML plugins |
| 43 + 44 | **Sprint 43** | Performance — connection, pipelining, streaming tools, fallback |

## Milestones

All milestones completed on **2026-03-07** (single-day sprint blitz).

| Sprint Range | Focus |
|-------------|-------|
| 11–33 | Foundation → bug fixes |
| 34–35 | TUI parity + agent intelligence |
| 37–45 | Workflow, context, extensibility, performance, safety |
| 46–48 | Differentiators (multi-agent, code review, voice) |
| 50a–50f | Stabilization (tests, bugs, performance, docs, DX) |
| 99–100 | Housekeeping + v2.1 release |
