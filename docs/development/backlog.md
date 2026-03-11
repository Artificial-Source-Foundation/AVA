# AVA Backlog

> Items waiting for sprint assignment. Completed items moved to bottom.

## Open

| ID | Priority | Title | Notes |
|----|----------|-------|-------|
| B6 | P2 | Desktop gap: session CRUD commands | Missing from Tauri backend (desktop-only, not CLI) |
| B24 | P0 | Hooks system — lifecycle automation | User-defined hooks at lifecycle events. MUST surpass Claude Code's implementation. Claude has 12 events + 4 hook types (shell, HTTP, prompt, agent). We should add: conditional matchers (glob/regex on tool name, file path), hook chaining (output of one feeds next), priority ordering, dry-run mode, hook marketplace/sharing. Events: PreToolUse, PostToolUse, PostToolUseFailure, SessionStart, SessionEnd, Notification, PreCompact, ConfigChange, SubagentStart, SubagentStop, Stop, PermissionRequest + AVA-specific: PreModelSwitch, PostModelSwitch, BudgetWarning. Config in `.ava/hooks/` (TOML) + `~/.ava/hooks/` (global). Leverage existing middleware infrastructure in ToolRegistry. Competitors: Claude Code (12 events, 4 types), Aider (auto-lint/test only). |
| B25 | P0 | Background agents (`Ctrl+B`) | Move running agent to background, start new conversation. Notification on completion. Features: `Ctrl+B` to background current task, `/tasks` to list background agents, click to view output, git worktree isolation per background agent (Codex-style — each agent works on its own branch, no conflicts). Task manager tracks status (running/done/failed), token usage, duration. Reuses AgentStack + event channel architecture. This is the "composer-level" version of Praxis — single user-initiated background tasks, not the multi-agent orchestration flow. Competitors: Claude Code (Ctrl+B + agent teams), OpenCode (parallel subagents), Codex (worktree isolation). |
| B26 | P1 | Praxis in chat composer | Expose Praxis multi-agent orchestration from chat — `/praxis` or `/team` command spawns Director → Leads → Workers pipeline from the composer without `--praxis` CLI flag. Workers visible in sidebar, results merged into main session. Two tiers: Full (Director → Leads → Workers for complex refactors) and Lite (Director → Workers for quick parallel tasks). Builds on ava-praxis crate. |
| B32 | P1 | OS keychain credential storage | Replace plaintext `~/.ava/credentials.json` with OS-native secret storage via `keyring` crate (macOS Keychain, Linux Secret Service/libsecret, Windows Credential Manager). Fallback to encrypted JSON (AES-256 with master password) when keychain unavailable. Redact API keys in all logs (show only last 4 chars). Add `/credentials` command to manage keys. Migration: auto-import existing JSON on first run, delete plaintext after successful migration. Add file integrity HMAC. |

## Implemented (pending manual testing)

| ID | Implemented | Title | Status |
|----|-------------|-------|--------|
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
