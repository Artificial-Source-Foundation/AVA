# Sprint 50c: TUI Interaction Tests

## IMPORTANT: Start in Plan Mode

**Before writing ANY code**, you MUST:

1. Read ALL files listed in "Key Files to Read"
2. Read `CLAUDE.md` for conventions
3. Enter plan mode and produce a detailed implementation plan
4. Get the plan confirmed before proceeding

## Goal

Test the TUI event handling and rendering without a real terminal. Verify that key bindings, modals, state transitions, and UI components work correctly using Ratatui's TestBackend.

## Key Files to Read

```
CLAUDE.md
crates/ava-tui/src/app.rs                    # App, AppState, ModalType, event loop
crates/ava-tui/src/ui/mod.rs                 # UI rendering
crates/ava-tui/src/ui/sidebar.rs             # Sidebar rendering
crates/ava-tui/src/ui/status_bar.rs          # Status bar
crates/ava-tui/src/widgets/message_list.rs   # Message list widget
crates/ava-tui/src/widgets/command_palette.rs # Command palette
crates/ava-tui/src/widgets/tool_approval.rs  # Tool approval modal
crates/ava-tui/src/state/agent.rs            # AgentState
crates/ava-tui/src/state/messages.rs         # Message state
crates/ava-tui/src/state/session.rs          # Session state
crates/ava-tui/src/rendering/markdown.rs     # Markdown rendering
crates/ava-tui/Cargo.toml                    # Check ratatui version for TestBackend
```

## Implementation

### File: `crates/ava-tui/tests/tui_interaction.rs` (NEW)

Use `ratatui::backend::TestBackend` to render the TUI without a terminal and verify state.

### Test Cases

| Test | Setup | Action | Assert |
|------|-------|--------|--------|
| `welcome_screen_renders` | Create App with default state | Render one frame | Buffer contains "AVA" or welcome text |
| `input_field_accepts_text` | Create App | Send key events 'h','e','l','l','o' | `app.input_buffer() == "hello"` |
| `enter_submits_input` | Create App with "hello" in input | Send Enter | Input buffer cleared, message added |
| `model_selector_opens` | Create App | Send Ctrl+M | `app.modal == Some(ModalType::ModelSelector)` |
| `command_palette_opens` | Create App | Send Ctrl+P | `app.modal == Some(ModalType::CommandPalette)` |
| `slash_opens_palette` | Create App | Send '/' as first char | Command palette opens |
| `sidebar_toggles` | Create App | Send Ctrl+B twice | Sidebar visible → hidden → visible |
| `status_bar_shows_model` | Create App with model set | Render frame | Buffer contains model name |
| `escape_closes_modal` | Create App with modal open | Send Esc | `app.modal == None` |
| `tool_approval_shows_risk` | Create App with pending approval (High risk) | Render frame | Buffer contains "HIGH" or risk indicator |

### Implementation Notes

- **TestBackend**: `ratatui::backend::TestBackend::new(width, height)` creates a virtual terminal
- **Terminal**: `ratatui::Terminal::new(backend)` for rendering
- **Key simulation**: Build `crossterm::event::KeyEvent` structs and feed to the app's key handler
- **State inspection**: Check `AppState` fields directly after handling events
- **Buffer inspection**: After `terminal.draw()`, check `backend.buffer()` for expected content

**Helper:**
```rust
fn create_test_app() -> App {
    // Create app with mock/minimal state
    // No real provider needed — just testing UI state transitions
}

fn send_key(app: &mut App, key: KeyCode) {
    let event = KeyEvent::new(key, KeyModifiers::NONE);
    app.handle_key_event(event);
}

fn send_ctrl(app: &mut App, key: char) {
    let event = KeyEvent::new(KeyCode::Char(key), KeyModifiers::CONTROL);
    app.handle_key_event(event);
}
```

### Acceptance Criteria

- All 10 TUI tests pass without a real terminal
- Tests run fast (< 1s each, < 5s total)
- No dependencies on external services (providers, APIs)
- Tests verify both state transitions AND rendered output where applicable

## Constraints

- **Rust only**
- `cargo test --workspace` — all tests pass
- `cargo clippy --workspace` — no warnings
- Tests must NOT require a real terminal, TTY, or provider
- Don't modify existing TUI code unless fixing a bug found during testing
- If App construction requires complex setup, create a `test_helpers` module

## Validation

```bash
cargo test --workspace
cargo clippy --workspace
cargo test -p ava-tui --test tui_interaction -- --nocapture
```
