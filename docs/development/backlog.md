# AVA Backlog

> Items waiting for sprint assignment. Completed items moved to bottom.

## Open

| ID | Priority | Title | Notes |
|----|----------|-------|-------|
| B6 | P2 | Desktop gap: session CRUD commands | Missing from Tauri backend (desktop-only, not CLI) |
| B26 | P1 | Praxis in chat composer | Expose Praxis multi-agent orchestration from chat — `/praxis` or `/team` command spawns Director → Leads → Workers pipeline from the composer without `--praxis` CLI flag. Workers visible in sidebar, results merged into main session. Two tiers: Full (Director → Leads → Workers for complex refactors) and Lite (Director → Workers for quick parallel tasks). Builds on ava-praxis crate. |
| B37 | P2 | Smart `/commit` with LLM message generation | `/commit` currently only runs `git status`. Upgrade: stage files, generate commit message via LLM (conventional commits style), show diff + proposed message, user edits/confirms. Like Claude Code's git awareness. |
| B38 | P2 | Auto-learned project memories | Beyond manual AGENTS.md — system auto-detects patterns (preferred frameworks, coding style, common imports, test patterns) and stores them. Windsurf's praised "Memories" feature. `ava-memory` currently only has manual `remember/recall`. Add: pattern detector that observes tool results + user corrections, builds project profile over time. |
| B39 | P2 | Background agents on branches | Background agents (`/bg`) should optionally work on isolated git branches. `git worktree add` for isolation, merge back on completion. Currently background agents share the working tree. |
| B40 | P2 | Budget alerts + cost dashboard | Cost is displayed in status bar but no alerts. Add: configurable budget threshold (warn at 80%, hard stop at 100%), per-session cost breakdown in `/cost` command, cumulative daily/weekly tracking. |
| B41 | P3 | Session templates | Save conversation patterns as reusable templates — system prompt + tool set + follow-up pipeline. `/template save security-review`, `/template load security-review`. Store in `.ava/templates/`. |
| B42 | P3 | Custom agent modes/personas | Beyond Code/Plan — let users define custom modes in config with specific system prompts, tool permissions, and model overrides. `/mode create reviewer` with TOML config. agents.toml already has `prompt` field per agent but TUI only exposes Code/Plan. |
| B44 | P2 | Built-in web search tool | **Tier: Extended.** Zero-config web search for docs, APIs, error messages. Gemini CLI has this free via Google. Add `web_search` tool using DuckDuckGo/SearXNG (no API key) or configurable provider. Currently only `web_fetch` exists (needs a URL). |
| B45 | P2 | File watcher mode | Watch for file saves and comment-driven prompts (`// ava: fix this`). Aider's praised feature. Bridges TUI and editor — user edits in their IDE, AVA picks up changes automatically. `notify` crate for filesystem events. |
| B46 | P2 | Plugin/skill marketplace | `ava plugin install <name>` to add community tools, hooks, MCP configs, agent presets. Registry hosted on GitHub or custom. AVA has the extension API but no discovery/install mechanism. Claude Code has 9K+ plugins. |
| B47 | P2 | Cost-aware model routing | Route cheap tasks (grep, read) to fast/cheap models, expensive reasoning to expensive models. Novel — no competitor does this. AVA's multi-agent + multi-provider architecture is uniquely positioned. Per-tool-type model config in agents.toml. |
| B48 | P2 | Change impact analysis | Before applying edits, show blast radius: importing files, covering tests, affected CI pipelines. Leverages existing LSP integration + codebase index (PageRank). Novel — no competitor has this. |
| B49 | P3 | Spec-driven development | Generate requirements.md/design.md/tasks.md specs before coding (Kiro's approach). `/spec` command creates structured specs from prompt, specs become source of truth. Beyond simple Plan mode. |
| B50 | P3 | Agent team peer communication | Lateral mailbox between parallel agents (not just top-down Praxis hierarchy). Claude Code Agent Teams model — agents negotiate conflicts, share type definitions, flag blockers to peers. Upgrade to ava-praxis. |
| B52 | P2 | AST-aware operations | **Tier: Extended.** tree-sitter/ast-grep based code search and structural codemods. `ast_search` and `ast_edit` tools. oh-my-pi has this. More precise than regex grep for refactoring (rename symbol, extract function, change signatures). |
| B53 | P2 | Full LSP exposure to agent | **Tier: Extended.** Currently only diagnostics exposed. oh-my-pi exposes 11 LSP ops: definition, references, type_definition, implementation, hover, symbols, rename, code_actions, reload. `ava-lsp` crate was removed (dead stub) — needs fresh implementation, likely as tools in ava-tools that spawn `lsp-client` per project language server. |
| B54 | P2 | Auto lint+test after edits | Automatically run linter + test suite after every agent edit, feed failures back into conversation. Aider's workflow. AVA has lint/test_runner tools but they're manual. Make them auto-trigger via hooks (B24) or built-in post-edit pipeline. |
| B55 | P2 | Security scanning agent tool | **Tier: Plugin.** Dedicated vulnerability detection and auto-fix tool. Codex Security scanned 1.2M commits, found 10K+ high-severity issues. CodeMender (DeepMind) submitted 72 security patches. Add `security_scan` tool using semgrep/cargo-audit/npm-audit as MCP server or TOML custom tool. |
| B56 | P2 | Test generation tool | **Tier: Plugin.** Automated test generation with edge case detection and bug simulation. Qodo Gen/Cover approach. `/test-gen` command analyzes function signatures + existing tests, generates new test cases targeting uncovered paths. Implement as plugin or slash command, not built-in tool. |
| B57 | P2 | Multi-repo context | Search across repository boundaries, understand cross-repo dependencies. Amp/Augment have this. AVA's codebase index (BM25 + PageRank) is single-repo. Add workspace config with multiple repo roots. |
| B58 | P3 | Semantic codebase indexing | Beyond BM25/PageRank — embedding-based semantic search. Augment's Context Engine showed +70% agent performance improvement. Use local embeddings (all-MiniLM-L6-v2) or provider API. Upgrade ava-codebase. |
| B59 | P3 | Agent artifacts system | Agents produce tangible deliverables (plans, diffs, screenshots, reports) that users review with inline comments. Antigravity's "Google Docs-style comments on agent output" pattern. |
| B61 | P2 | Dev tooling setup | Add `cargo-nextest` (parallel tests), `cargo-llvm-cov` (coverage), `cargo-outdated`, pre-commit hooks (`fmt` + `clippy`), `clippy.toml` for custom thresholds. Research doc available from tooling audit. |
| B63 | P2 | Dynamic API key resolution | Callback to refresh expiring OAuth tokens (e.g. GitHub Copilot) mid-run. Currently credentials loaded once at startup. Add `refresh_api_key()` trait method to provider, called at request time. Pi-Mono's `getApiKey` pattern. |
| B64 | P2 | Thinking budget configuration | Per-provider and per-model configurable max token cap for extended thinking. Complements existing thinking level (Ctrl+T off/low/medium/high/xhigh) — level is qualitative, budget is quantitative hard cap. Configurable in settings/agents.toml. Pi-Mono's `thinkingBudgets`. |
| B65 | P2 | Pluggable backend operations | Trait-based tool execution — `Backend` trait with `LocalBackend` default, future `SshBackend`/`DockerBackend`. Enables Praxis workers to execute tools on remote machines/containers. Refactor bash/read/write/edit to use trait. Unblocks remote multi-agent (B26) and background agents on branches (B39). Pi-Mono's `EditOperations`/`BashOperations` pattern. |
| B66 | P2 | Ghost snapshots | Invisible git commits (detached from branch history) before every edit for crash-safe rollback. Complements B22 (in-session rewind) with persistent recovery. `git commit-tree` with refs in `.ava/snapshots/`. Codex CLI's `ghost_commits.rs` pattern. |
| B67 | P2 | RelativeIndenter for edit matching | Delta-encoded indentation — match edits by relative whitespace shape instead of absolute spaces. Handles LLM indentation mismatches in nested code. Add as new strategy in edit engine's fuzzy cascade. Prerequisite for B51 (hash-anchored edits). Aider's `RelativeIndenter` pattern. |
| B68 | P2 | Batch tool | **Tier: Extended.** Explicit parallel tool execution — LLM calls `batch` with up to 25 tool invocations, deduplicates, returns combined results. Extends beyond implicit read-only parallelism. OpenCode's `batch` tool pattern. |
| B69 | P2 | Code search tool | **Tier: Plugin.** Semantic code search across public repositories — find API usage examples, patterns, SDK usage. Beyond local `codebase_search` (BM25) and `grep` (regex). Could use Exa, Sourcegraph, or GitHub Code Search API. Pairs with B44 (web search). Implement as MCP server. OpenCode's `codesearch` pattern. |
| B71 | P2 | Skill discovery | Auto-discover skill files from `.claude/skills/`, `.agents/skills/`, `.ava/skills/`. Load as context alongside instructions. Zero-config interoperability with Claude Code's skill ecosystem. Extend existing instruction discovery walker. Related to B46 (plugin marketplace). OpenCode's `Skill.Info` pattern. |
| B72 | P2 | Browser automation (MCP/plugin) | **Tier: Plugin.** Web page interaction — navigate, click, fill forms, capture screenshots. Implement as MCP server config or plugin, NOT built-in tool. Ship bundled Playwright MCP config. OpenHands' `BrowserEnv` pattern. |
| B73 | P2 | Network proxy with SSRF protection | Managed network proxy for agent outbound requests. Block private IPs, metadata endpoints, configurable deny-lists. `web_fetch` has basic blocking but `bash` bypasses it. Proxy layer + iptables/nftables for sandboxed bash. Codex CLI's `network-proxy` pattern. |
| B74 | P3 | Custom keybindings | User-definable keybindings in JSON at `~/.ava/keybindings.json`. Remap any TUI action to any key combo. Fall back to defaults. Pi-Mono's `keybindings.json` pattern. |
| B75 | P3 | Directory listing tool | **Tier: Extended.** Simple `list` tool — tree-view directory listing respecting .gitignore, capped at 100 files. Complements glob/grep with cleaner "what's in this folder" interface. `walkdir` + tree formatting. OpenCode's `list` tool. |
| B76 | P3 | Agent Client Protocol (ACP) | Standardized RPC interface for external clients (VS Code, Zed, Neovim). Decouple agent into server process, TUI becomes one client among many. Enables editor integrations without full plugins. Major architectural change. OpenCode's `acp/` pattern. |
| B77 | P3 | PR checkout workflow (plugin) | `/pr <number>` command — auto-checkout PR branch, detect forks, import context. Implement as plugin, not built-in. Wraps `gh` CLI. OpenCode's `pr` command pattern. |
| B78 | P3 | Auto-formatting detection (opt-in) | Detect when IDE auto-formatters change files between agent write and next read. Compare file hash, warn LLM to re-read instead of failing edits. Opt-in setting, not default. Cline's auto-format detection pattern. |
| B79 | P3 | Evaluation harness enhancement | Enhance existing benchmarks with SWE-bench integration — run agent against real GitHub issues, score results, compare across models/configs. Benchmark runner + dataset loader + scoring. SWE-Agent/OpenHands pattern. |
| B80 | P3 | Trajectory recording | Record full agent decision tree — every tool call, LLM response, branch point — as structured JSONL per session. Enables replay, pattern analysis, debugging. "Agent always tries grep before read" insights. SWE-Agent's trajectory pattern. |

## Implemented (pending manual testing)

| ID | Implemented | Title | Status |
|----|-------------|-------|--------|
| B34 | Sprint 60 | Three-tier mid-stream messaging | Code complete — `MessageQueue` with steering/follow-up/post-complete pipelines (12 tests), agent loop polls steering between tool calls, skip remaining tools on steer, follow-up loop after task, post-complete group pipeline after all work. TUI: Enter=steer, Alt+Enter=follow-up, Ctrl+Alt+Enter=post-complete. `/later` + `/queue` commands. Composer shows queue items with tier badges. Status bar `[N queued]`. Ctrl+C clears steering but preserves follow-up/post-complete. Needs live testing with real agent runs. |
| B33 | Sprint 60 | Claude Code as subagent | Phase 1+2 code complete — `claude_code` tool in ava-tools (18 tests), `[CC]` sidebar badge, `ClaudeCodeConfig` in ava-config, `provider`/`allowed_tools`/`max_budget_usd` in agents.toml. `.env_remove("CLAUDECODE")` for nested invocation. Tool NOT pre-activated — requires explicit enable in providers modal. Test script: `scripts/test-claude-code-integration.sh`. Design doc: `docs/architecture/claude-code-integration.md`. Needs: live CLI testing (outside CC session), provider modal wiring (Phase 3). Stream subagent module (`claude_code_stream.rs`) was removed as dead code — streaming subagent support would need reimplementation when Phase 2 is wired in. |
| B24 | Sprint 60-03 | Hooks system — lifecycle automation | Code complete — 16 events, 3 action types (Command/HTTP/Prompt), TOML config in `.ava/hooks/` + `~/.ava/hooks/`, priority ordering, path matchers, dry-run mode. Needs live testing with real hook files. |
| B25 | Sprint 60-03 | Background agents (`Ctrl+B`) | Code complete — `Ctrl+B` to background, `/bg <goal>` to launch, `/tasks` modal, SharedBackgroundState, auto-expiring notifications. Needs live testing with real agent runs. |
| B32 | Sprint 60 | OS keychain credential storage | Code complete — `KeychainManager` with OS keychain (keyring crate) + AES-256-GCM encrypted file fallback. PBKDF2 key derivation, `redact_key_for_log()`, `/credentials` command (list/add/remove), auto-migration from plaintext. 5 tests. Needs live testing of OS keychain integration. |
| B21 | Sprint 60-03 | `/btw` side conversations | Code complete — ephemeral overlay, no-tools LLM call, dismiss with Space/Enter/Esc. Needs live testing with real LLM provider. |
| B22 | Sprint 60-03 | Rewind system (`/undo`, `Esc+Esc`) | Code complete — 5-option modal, file change tracking per checkpoint, double-Esc detection. Needs live testing of file restoration. |
| B23 | Sprint 60-03 | `/export` conversation export | Code complete — markdown/JSON formats, auto-naming. Needs live testing of file output. |
| B27 | Sprint 60-03 | `/compact` command | Code complete — wired to existing condensation, focus keyword preservation, token savings display. Needs live testing with real conversation. |
| B28 | Sprint 60-03 | `/init` project bootstrap | Code complete — detects 6 languages, 26+ frameworks, monorepos, CI. Needs live testing on various project types. |
| B29 | Sprint 60-03 | Custom slash commands | Code complete — TOML command definitions, parameter substitution, `/commands list/reload/init`. Needs live testing with real command files. |
| B30 | Sprint 60-03 | `/copy` code block picker | Code complete — numbered modal for multiple code blocks, digit keys to select. Needs live testing with real responses. |

## Completed

| ID | Completed | Title | Resolution |
|----|-----------|-------|------------|
| B81 | Sprint 60 | Tool surface rationalization — 6 default tools | Default: read, write, edit, bash, glob, grep. Extended: test_runner, lint, diagnostics, git, web_fetch, multiedit, apply_patch. `ToolTier` enum, `extended_tools` config flag, tier-aware `list_tools_for_tiers()`. Sub-agents/Praxis get full access. 3 tests. |
| B62 | Sprint 60 | Cross-provider message normalization | `normalize_messages()` with thinking block stripping, tool call ID normalization, orphaned result repair. `ProviderKind` enum, `NormalizingProvider` wrapper. 39 tests. Unblocks B47. |
| B60 | Sprint 60 | Rust CI pipeline | Added cargo fmt, clippy, test jobs to GitHub Actions with rust-cache. Created `rustfmt.toml` + `deny.toml`. |
| B43 | Sprint 60 | Image/screenshot input | `ImageContent` type, `ImageMediaType` enum (PNG/JPEG/GIF/WebP). Anthropic/OpenAI/Gemini serialization. `--image` CLI flag, `/image` TUI command. 28 tests. |
| B51 | Sprint 60 | Hash-anchored edits (Hashline) | FNV-1a hash per line, `HashlineCache` shared between read/edit tools, strategy 0 (highest priority) in edit cascade, stale file detection. `hash_lines` parameter on read tool. 20+ tests. |
| B35 | Sprint 60 | @-mention context scoping | `ContextAttachment` enum, `parse_mentions()` parser, `mention_picker` autocomplete widget, fuzzy file search, context prepended as XML blocks. `@file`/`@folder`/`@codebase` in composer. 14+ tests. |
| B36 | Sprint 60 | Multi-file diff preview + accept/reject | 887-line `DiffPreviewState` widget with per-hunk y/n/a/d controls, `compute_hunks()` via `similar::TextDiff`, `apply_partial_hunks()`, themed rendering. 14 tests. |
| B70 | Sprint 60 | Plan mode file writes | `check_plan_mode_tool()` validation — Plan mode allows writes ONLY to `.ava/plans/*.md`, blocks bash. `/plan`, `/code`, `/plans` commands. 9 tests. |
| B31 | Sprint 60-03 | Rename ava-commander → ava-praxis + Director | Crate renamed, Commander→Director, CommanderEvent→PraxisEvent, `--praxis` CLI alias added. 35 files, 14+ docs updated. |
| B1 | Sprint 60 | TUI freezes during LLM calls | `AgentStack::run()` uses `run_streaming()` with event channel |
| B2 | Sprint 60-02 | No conversation memory between turns | `AgentStack::run()` accepts `history: Vec<Message>` |
| B3 | Sprint 60-02 | Scroll in chat shows input history | Mouse scroll + `MessageState.scroll_up/down()` |
| B4 | Sprint 60-02 | No session sidebar UI | Session list with Ctrl+L, `/sessions` command |
| B5 | Sprint 60-02 | Last model not remembered on restart | Per-project `.ava/state.json` + per-session metadata |
| B7 | Sprint 60 | Compilation errors in ava-agent tests | All callers updated, tests pass |
| B8 | Sprint 60 | Ancestor directory walking for instructions | Walks parent dirs for `AGENTS.md`/`CLAUDE.md`, stops at `.git` boundary |
| B9 | Sprint 60 | Config-driven instructions array | `instructions:` list in config.yaml, supports file paths + glob patterns |
| B10 | Sprint 60 | Per-file contextual instructions | `contextual_instructions_for_file()` helper in instructions.rs, 6 tests |
| B11 | Sprint 60 | Glob-scoped rules with frontmatter | `.ava/rules/*.md` with `paths:` YAML frontmatter, glob matching |
| B12 | Sprint 60 | TUI slash command testing | 34 integration tests covering all commands |
| B13 | Sprint 60 | Sub-agent configuration UI | `/agents` command, read-only modal showing config from `agents.toml` |
| B14 | Sprint 60 | Sub-agent config format | `AgentsConfig` in `ava-config/src/agents.rs`, TOML with global + project merge |
| B15 | Sprint 60 | Wire agents.toml into AgentStack | Sub-agents use resolved config for max_turns, prompt, enabled |
| B16 | Sprint 60 | Auto-name sessions | `generate_title()` smart truncation, stored in session metadata |
| B17 | Sprint 60 | Wire contextual instructions into read tool | Post-processes read results in `tool_execution.rs` via `contextual_instructions_for_file()` |
| B18 | Sprint 60 | Sub-agent navigation UI | ViewMode switching, Esc to go back, Enter to enter sub-agent, session linking with parent_id |
| B19 | Sprint 60 | Sub-agent sidebar polish | Dim completed, cap visible to 5, tool count + duration stats |
| B20 | Sprint 60 | Unlimited turns by default | max_turns=0 means unlimited, forced summary at limits, --max-budget-usd flag |
