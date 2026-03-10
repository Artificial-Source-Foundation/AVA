# Sprint 60: Streaming, Session UX, Chat UI Rework

## Goal

Three parallel tracks: (1) replace blocking `generate_with_tools()` with true streaming, (2) fix conversation context + session UX, (3) rework chat UI to match OpenCode's polished layout.

## Prompts

| # | Name | Type | Status |
|---|------|------|--------|
| 01 | `01-streaming-tool-calls.md` | Implementation (mega) | Complete |
| 02 | `02-session-context-ux.md` | Implementation (mega) | Complete (already implemented) |
| 03 | `03-chat-ui-rework.md` | Implementation (mega) | Complete |
| 04 | Agent Modes & Permission Levels | Implementation | Complete |

### Execution Order
- **01 + 02 + 03** can run in **parallel** (zero file overlap: 01 touches provider layer + agent loop streaming, 02 touches TUI state + session + config, 03 touches TUI widgets + layout + styling)
- **02 vs 03 overlap note**: Both touch `crates/ava-tui/` but different files. 02 focuses on `state/agent.rs`, `app/event_handler.rs`, `state/messages.rs` (scroll offset only). 03 focuses on `ui/layout.rs`, `widgets/composer.rs`, `widgets/message_list.rs` (rendering), `widgets/welcome.rs`, `ui/status_bar.rs`.

## Background

**01**: Sprint 59-04 added `StreamChunk` with tool call support, but the agent loop still uses non-streaming `generate_with_tools()` for tool-supporting providers. TUI freezes during LLM calls.

**02**: Four critical UX issues:
1. Model has no memory of previous messages (fresh context per turn)
2. Can't scroll up through chat (mouse scroll = input history instead of scrolling messages)
3. No session sidebar/picker (sessions exist in DB but no UI)
4. Last model not remembered on restart

**03**: Chat UI rework — bordered input box with top border, left-border message styling with role-specific colors, enhanced welcome screen, consolidated status bars, scroll indicator, focus indicator.

**04**: Agent Modes and Permission Levels — three execution modes (Code/Plan/Architect) with Tab/Shift+Tab cycling, permission levels replacing YOLO, and model-aware thinking display.

### Sprint 60-04: Agent Modes & Permission Levels

**Agent Modes** (`AgentMode` enum in `state/agent.rs`):
- **Code** (default): Full tool access, no system prompt modification.
- **Plan**: Read-only tools only. System prompt suffix constrains agent to read, glob, grep, codebase_search, diagnostics, session_search, session_list, recall, memory_search. No file modifications or command execution.
- **Architect**: Plan-first mode. Agent analyzes codebase and presents implementation plan before making changes.
- Tab/Shift+Tab cycles modes in composer. Mode badge `[Code]`/`[Plan]`/`[Architect]` rendered on composer line 2 with role-specific colors (green/blue/accent).
- Mode prompt suffix stored in `AgentStack.mode_prompt_suffix: RwLock<Option<String>>`, read at each `run()` call and appended to system prompt via `AgentConfig.system_prompt_suffix`.

**Permission Levels** (`PermissionLevel` enum in `state/permission.rs`):
- **Standard** (default): Auto-approve reads+writes, ask for bash/commands based on risk threshold.
- **AutoApprove**: Auto-approve all tools except Critical-blocked commands (rm -rf /, sudo, fork bombs). Replaces the old `--yolo` CLI flag.
- CLI: `--auto-approve` flag (with `--yolo` alias for backward compat).
- TUI: `/permissions` slash command toggles between Standard and AutoApprove.
- Top bar shows "auto-approve" warning badge when active.
- `InspectionContext.auto_approve` feeds into step 3 of the 9-step `DefaultInspector` in `ava-permissions`.

**Thinking display improvements**:
- `model_supports_thinking()`: Claude, GPT-5, Gemini 2.5/3, o3/o4, DeepSeek-R1, QwQ, Kimi.
- `model_supports_thinking_levels()`: Subset that accepts granular level params (excludes native thinkers like Kimi, DeepSeek-R1, QwQ).
- Status bar badge: `thinking:{level}` for granular models, plain `thinking` for native thinkers.

## Status: Complete
