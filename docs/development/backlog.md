# AVA Backlog

> Items waiting for sprint assignment. Completed items moved to bottom.

## Open

| ID | Priority | Title | Notes |
|----|----------|-------|-------|
| B6 | P2 | Desktop gap: session CRUD commands | Missing from Tauri backend (desktop-only, not CLI) |
| B21 | P1 | `/btw` side conversations | Ephemeral Q&A while agent runs — spawns read-only LLM call with current context, no tools, renders in dismissible overlay, never enters history. Inverse of subagent (full context, no tools). ~200-300 lines. Inspired by Claude Code. |
| B22 | P1 | Rewind system (`/undo`, `Esc+Esc`) | Checkpoint rewind to any message with 5 options: restore code+conversation, conversation only, code only, summarize from here, cancel. Track file changes per tool call (edit/write only, not bash). Show diff stats (+N -M) in rewind UI. Warning: "does not affect files edited manually or via bash". Inspired by Claude Code. |
| B23 | P1 | `/export` conversation export | Dump conversation to markdown/text/JSON file. Includes messages, tool calls, code blocks. `/export [filename]` with auto-generated name if omitted. Low effort — serialize MessageState to disk. |
| B24 | P0 | Hooks system — lifecycle automation | User-defined hooks at lifecycle events. MUST surpass Claude Code's implementation. Claude has 12 events + 4 hook types (shell, HTTP, prompt, agent). We should add: conditional matchers (glob/regex on tool name, file path), hook chaining (output of one feeds next), priority ordering, dry-run mode, hook marketplace/sharing. Events: PreToolUse, PostToolUse, PostToolUseFailure, SessionStart, SessionEnd, Notification, PreCompact, ConfigChange, SubagentStart, SubagentStop, Stop, PermissionRequest + AVA-specific: PreModelSwitch, PostModelSwitch, BudgetWarning. Config in `.ava/hooks/` (TOML) + `~/.ava/hooks/` (global). Leverage existing middleware infrastructure in ToolRegistry. Competitors: Claude Code (12 events, 4 types), Aider (auto-lint/test only). |
| B25 | P0 | Background agents (`Ctrl+B`) | Move running agent to background, start new conversation. Notification on completion. Features: `Ctrl+B` to background current task, `/tasks` to list background agents, click to view output, git worktree isolation per background agent (Codex-style — each agent works on its own branch, no conflicts). Task manager tracks status (running/done/failed), token usage, duration. Reuses AgentStack + event channel architecture. This is the "composer-level" version of commander — single user-initiated background tasks, not the multi-agent orchestration flow. Competitors: Claude Code (Ctrl+B + agent teams), OpenCode (parallel subagents), Codex (worktree isolation). |
| B26 | P1 | Commander in chat composer | Expose commander/multi-agent orchestration directly from chat — `/multi` or `/team` command spawns a coordinated multi-agent workflow from the composer without needing `--multi-agent` CLI flag. Workers visible in sidebar, results merged into main session. Builds on existing ava-commander infrastructure. |
| B27 | P1 | `/compact` command + configurable compaction model | Expose existing 3-stage condensation as `/compact [focus]` command. Focus instructions let users control what to keep ("forget debugging, keep architecture"). Configurable compaction model/provider — users pick a cheap fast model (e.g. Haiku) for compaction instead of burning expensive tokens. Config in agents modal or `/compact --model provider/model`. Compare with Claude Code (`/compact [instructions]`), OpenCode (`/compact`), Codex (`/compact`) and surpass — add: focus instructions, model selection, preview mode (show what will be kept/dropped before committing), token savings estimate. |
| B28 | P1 | `/init` project bootstrap | One command to scaffold `.ava/` for any project. Auto-detect: language (Cargo.toml/package.json/go.mod/pyproject.toml), framework, test runner, linter, formatter. Generate: starter config, AGENTS.md with project-specific instructions, recommended permission policy, custom tool templates for detected stack. Surpass competitors: Claude Code just creates config, OpenCode just generates AGENTS.md. AVA should: detect CI/CD (GitHub Actions, GitLab CI), suggest MCP servers for detected services (e.g. postgres MCP if DB detected), offer interactive wizard vs quick mode, generate `.ava/hooks/` with auto-lint/test hooks for detected toolchain, detect monorepo structure and suggest per-workspace configs. |
| B29 | P1 | Custom slash commands + skills system | **Custom commands**: User-defined `/commands` from `.ava/commands/` and `~/.ava/commands/` as Markdown/TOML files. Each defines: name, description, prompt template, allowed tools, parameters. Teams share commands via repo. Surpass competitors: OpenCode has Markdown files, Goose has YAML recipes — AVA should add: parameter validation, tool restrictions per command, chaining (output of one command feeds another), `/commands list` to browse, tab-completion in slash menu. **Skills**: Agent-detected capabilities that activate contextually — like Claude Code's skills but smarter. Skills are prompt+tool bundles that the agent can invoke when it detects a matching situation (e.g. "commit" skill activates when agent sees staged changes, "review" skill when on a PR branch). Skills defined in `.ava/skills/` with frontmatter triggers (glob patterns, context conditions). Difference: commands are user-invoked (`/foo`), skills are agent-invoked (auto-detected). Both share the same definition format. |
| B30 | P2 | `/copy` code block picker | Enhance existing `/copy` + `Ctrl+Y` with a code block picker modal — when response has multiple code blocks, show numbered list to pick which one. Also add `/copy all` to copy entire response. Low effort, mostly UI. |

## Completed

| ID | Completed | Title | Resolution |
|----|-----------|-------|------------|
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
