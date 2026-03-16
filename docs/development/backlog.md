# AVA Backlog

> Last updated: 2026-03-16 (v3 complete; Sprints 63-66 delivered)
> Related: `docs/development/roadmap.md`, `docs/development/epics.md`, `docs/development/v3-plan.md`

> Items waiting for sprint assignment. Completed items moved to bottom.

Tool surface policy: AVA should keep the default tool set capped at 6 (`read`, `write`, `edit`, `bash`, `glob`, `grep`). New tool capabilities should default to **Extended**, MCP/plugin, or custom-tool delivery unless there is a strong reason to expand the default surface.

## Recommended Order for Open P2/P3 Items

This section does **not** rewrite the raw `P2`/`P3` labels below. It is the recommended execution order after applying the lean-core tool policy and current codebase state.

v3 planning now runs in two lanes:

- **Backend lane**: Rust-first capability work in `crates/`.
- **Frontend lane**: TUI/Desktop surfaces for already-approved capabilities, led by `B26` and the paired UX tracks in `docs/development/v3-plan.md`.

### Next

1. `B26` Praxis in chat composer — only open `P1`, first slice delivered, needs deeper UX.
2. `B73` Network proxy with SSRF protection — highest-leverage remaining backend hardening.
3. `B46` Plugin/skill marketplace — distribution/install layer for optional capabilities.
4. `B79` Evaluation harness enhancement — formal quality regression/perf signal.
5. `B80` Trajectory recording — debugging and behavior analytics substrate.

### Soon

6. `B41` Session templates
7. `B42` Custom agent modes/personas
8. `B74` Custom keybindings

### Later

9. `B78` Auto-formatting detection

### Plugin/Extended-Only (do not expand default tool surface)

- `B68` Batch tool — Extended tier.
- `B75` Directory listing tool — Extended tier.
- `B55`, `B56`, `B72`, `B77` — plugin/MCP-first capabilities.

## Delivered in Sprints 63-66

- **Sprint 63**: `B65` Pluggable backend ops, `B39` Background agents on branches, `B61` Dev tooling, `B71` Skill discovery, `B45` File watcher
- **Sprint 64**: `B38` Auto-learned memories, `B57` Multi-repo context, `B58` Semantic indexing, `B48` Change impact analysis
- **Sprint 65**: `B49` Spec-driven dev, `B59` Agent artifacts, `B50` Agent team peer comm, `B76` ACP
- **Sprint 66**: `B44` Web search (Extended), `B52` AST ops (Extended), `B53` LSP ops (Extended), `B69` Code search (Extended)

## Open Items Needing Tighter Scoping

- `B41` Session templates — should specify relationship to project instructions and whether templates are per-user, per-project, or shareable.
- `B46` Plugin/skill marketplace — needs packaging, trust/signing, versioning, install UX, and update/removal rules.
- `B73` Network proxy with SSRF protection — needs a concrete architecture split between proxy, policy, and sandbox integration.
- `B79` Evaluation harness enhancement — needs dataset strategy, scoring protocol, and CI/runtime budget limits.
- `B80` Trajectory recording — needs retention policy, privacy/redaction approach, and replay tooling boundaries.

## Recommended Next Sprints (Post-v3)

### Sprint 67 — Runtime Hardening + Observability

- `B73` Network proxy with SSRF protection
- `B79` Evaluation harness enhancement
- `B80` Trajectory recording

### Sprint 68 — Praxis UX Deepening

- `B26` Praxis in chat composer (deeper worker/task inspection, merge-back, session persistence)

### Sprint 69 — Extensibility

- `B46` Plugin/skill marketplace
- `B41` Session templates
- `B42` Custom agent modes/personas

## Open

| ID | Priority | Title | Notes |
|----|----------|-------|-------|
| B26 | P1 | Praxis in chat composer | First TUI slice shipped — Praxis accessible via Tab cycling, worker visibility in sidebar, cancellation, grouped completion summary. Remaining: richer worker decomposition, merge-back/session persistence, fuller task inspection UX. |
| B41 | P3 | Session templates | Save conversation patterns as reusable templates — system prompt + tool set + follow-up pipeline. Store in `.ava/templates/`. |
| B42 | P3 | Custom agent modes/personas | Beyond Code/Plan — user-defined modes with specific system prompts, tool permissions, and model overrides. agents.toml already has `prompt` field per agent but TUI only exposes Code/Plan. |
| B46 | P2 | Plugin/skill marketplace | `ava plugin install <name>` for community tools, hooks, MCP configs, agent presets. AVA has the extension API but no discovery/install mechanism. |
| B55 | P2 | Security scanning agent tool | **Tier: Plugin.** Vulnerability detection and auto-fix via semgrep/cargo-audit/npm-audit as MCP server or TOML custom tool. |
| B56 | P2 | Test generation tool | **Tier: Plugin.** Automated test generation with edge case detection. Implement as plugin or slash command, not built-in tool. |
| B68 | P2 | Batch tool | **Tier: Extended.** Explicit parallel tool execution — up to 25 tool invocations, deduplication, combined results. |
| B72 | P2 | Browser automation (MCP/plugin) | **Tier: Plugin.** Web page interaction via MCP server config or plugin. |
| B73 | P2 | Network proxy with SSRF protection | Managed network proxy for agent outbound requests. Block private IPs, metadata endpoints, configurable deny-lists. |
| B74 | P3 | Custom keybindings | User-definable keybindings in `~/.ava/keybindings.json`. |
| B75 | P3 | Directory listing tool | **Tier: Extended.** Tree-view directory listing respecting .gitignore. |
| B77 | P3 | PR checkout workflow (plugin) | `/pr <number>` — auto-checkout PR branch. Implement as plugin wrapping `gh` CLI. |
| B78 | P3 | Auto-formatting detection (opt-in) | Detect IDE auto-formatters changing files between agent write and next read. |
| B79 | P3 | Evaluation harness enhancement | SWE-bench integration, formal quality regression scoring. |
| B80 | P3 | Trajectory recording | Full agent decision tree as structured JSONL per session. Enables replay, pattern analysis, debugging. |

## Implemented (pending manual testing)

| ID | Implemented | Title | Status |
|----|-------------|-------|--------|
| B67 | Sprint 61 | RelativeIndenter for edit matching | Code complete — new fallback strategy in edit cascade. Needs real-world validation. |
| B54 | Sprint 61 | Auto lint+test after edits | Code complete — opt-in post-edit validation. Needs config/UX polish. |
| B37 | Sprint 61 | Smart `/commit` with LLM message generation | Code complete — `/commit` inspects git readiness, diff stats, suggests commit message. |
| B66 | Sprint 61 | Ghost snapshots | Code complete — git-backed blob snapshots before edit/multiedit. Needs retention/restore UX. |
| B34 | Sprint 60 | Three-tier mid-stream messaging | Code complete — steering/follow-up/post-complete pipelines. Needs live testing. |
| B33 | Sprint 60 | Claude Code as subagent | Phase 1+2 code complete — `claude_code` tool. Needs live CLI testing outside CC session. |
| B24 | Sprint 60-03 | Hooks system — lifecycle automation | Code complete — 16 events, 3 action types, TOML config. Needs live testing. |
| B25 | Sprint 60-03 | Background agents (`Ctrl+B`) | Code complete — `/tasks` modal, SharedBackgroundState. Needs live testing. |
| B32 | Sprint 60 | OS keychain credential storage | Code complete — OS keychain + AES-256-GCM fallback. Needs live testing. |
| B21 | Sprint 60-03 | `/btw` side conversations | Code complete — ephemeral overlay. Needs live testing. |
| B22 | Sprint 60-03 | Rewind system (`/undo`, `Esc+Esc`) | Code complete — 5-option modal, file change tracking. Needs live testing. |
| B23 | Sprint 60-03 | `/export` conversation export | Code complete — markdown/JSON formats. Needs live testing. |
| B27 | Sprint 60-03 | `/compact` command | Code complete — wired to condensation. Needs live testing. |
| B28 | Sprint 60-03 | `/init` project bootstrap | Code complete — detects 6 languages, 26+ frameworks. Needs live testing. |
| B29 | Sprint 60-03 | Custom slash commands | Code complete — TOML command definitions. Needs live testing. |
| B30 | Sprint 60-03 | `/copy` code block picker | Code complete — numbered modal for multiple code blocks. Needs live testing. |

## Completed

| ID | Completed | Title | Resolution |
|----|-----------|-------|------------|
| B6 | Sprint 63 | Desktop gap: session CRUD commands | Session commands in `src-tauri/src/commands/session_commands.rs`. Desktop calls Rust via Tauri IPC; `packages/` deleted. |
| B64 | Sprint 62 | Thinking budget configuration | Validated in Sprint 62V — per-provider/per-model thinking budgets. |
| B63 | Sprint 62 | Dynamic API key resolution | Validated in Sprint 62V — request-time credential refresh for OAuth providers. |
| B47 | Sprint 62 | Cost-aware model routing | Validated in Sprint 62V — configurable cheap/capable routing. |
| B40 | Sprint 62 | Budget alerts + cost dashboard | Validated in Sprint 62V — cumulative budget telemetry, threshold warnings. |
| B65 | Sprint 63 | Pluggable backend operations | Trait-based tool execution with `Backend` trait. |
| B39 | Sprint 63 | Background agents on branches | Git worktree isolation for background agents. |
| B61 | Sprint 63 | Dev tooling setup | cargo-nextest, cargo-llvm-cov, Justfile, pre-commit hooks. |
| B71 | Sprint 63 | Skill discovery | Auto-discover skill files from `.claude/skills/`, `.agents/skills/`, `.ava/skills/`. |
| B45 | Sprint 63 | File watcher mode | File save watching and comment-driven prompts via `notify` crate. |
| B38 | Sprint 64 | Auto-learned project memories | Learned-memory persistence and review in ava-memory. |
| B57 | Sprint 64 | Multi-repo context | Cross-repo search with workspace config. |
| B58 | Sprint 64 | Semantic codebase indexing | Embedding-based semantic search in ava-codebase. |
| B48 | Sprint 64 | Change impact analysis | Blast radius analysis using LSP + codebase index. |
| B49 | Sprint 65 | Spec-driven development | Requirements/design/tasks spec generation. |
| B59 | Sprint 65 | Agent artifacts system | Agent-produced deliverables with review. |
| B50 | Sprint 65 | Agent team peer communication | Lateral mailbox between parallel agents. |
| B76 | Sprint 65 | Agent Client Protocol (ACP) | Standardized RPC interface for external clients. |
| B44 | Sprint 66 | Web search capability | Extended tool — web_search via DuckDuckGo/SearXNG. |
| B52 | Sprint 66 | AST-aware operations | Extended tool — ast_ops via tree-sitter. |
| B53 | Sprint 66 | Full LSP exposure to agent | Extended tool — lsp_ops. |
| B69 | Sprint 66 | Code search tool | Extended tool — code_search. |
| B81 | Sprint 60 | Tool surface rationalization — 6 default tools | Default: read, write, edit, bash, glob, grep. Extended tools gated by `ToolTier` enum. |
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
