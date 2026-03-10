# AVA Backlog

> Items waiting for sprint assignment

| ID | Priority | Title | Notes |
|----|----------|-------|-------|
| B1 | P1 | TUI freezes during LLM calls | Non-streaming generate_with_tools path blocks TUI — Sprint 60-01 |
| B2 | P1 | No conversation memory between turns | Fresh context per agent run — Sprint 60-02 |
| B3 | P2 | Scroll in chat shows input history | Should scroll messages, not input history — Sprint 60-02 |
| B4 | P2 | No session sidebar UI | Need session list/switch in TUI — Sprint 60-02 |
| B5 | P2 | Last model not remembered on restart | Model selection lost between sessions — Sprint 60-02 |
| B6 | P2 | Desktop gap: session CRUD commands | Missing from Tauri backend |
| B7 | P3 | Compilation errors in ava-agent tests | `AgentStack::run()` signature changed (added `Vec<Message>` arg), callers not updated |
