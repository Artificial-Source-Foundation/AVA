# Sprint 60: Streaming, Session UX, Chat UI Rework

## Goal

Three parallel tracks: (1) replace blocking `generate_with_tools()` with true streaming, (2) fix conversation context + session UX, (3) rework chat UI to match OpenCode's polished layout.

## Prompts

| # | Name | Type | Status |
|---|------|------|--------|
| 01 | `01-streaming-tool-calls.md` | Implementation (mega) | Complete |
| 02 | `02-session-context-ux.md` | Implementation (mega) | Complete (already implemented) |
| 03 | `03-chat-ui-rework.md` | Implementation (mega) | Complete |

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

## Status: Complete
