//! TUI interaction tests — verifies event handling, key bindings, modals,
//! and rendering without a real terminal, database, or external services.

use ava_tui::app::{App, ModalType};
use ava_tui::ui;
use crossterm::event::{KeyCode, KeyEvent, KeyModifiers};
use ratatui::backend::TestBackend;
use ratatui::Terminal;

// ── helpers ──────────────────────────────────────────────────────────────

fn make_app() -> (App, tempfile::TempDir) {
    let tmp = tempfile::tempdir().expect("tempdir");
    let db_path = tmp.path().join("test.db");
    let app = App::test_new(&db_path);
    (app, tmp)
}

fn press(app: &mut App, code: KeyCode) {
    let key = KeyEvent::new(code, KeyModifiers::NONE);
    app.process_key_for_test(key);
}

fn press_char(app: &mut App, ch: char) {
    press(app, KeyCode::Char(ch));
}

fn press_ctrl(app: &mut App, ch: char) {
    let key = KeyEvent::new(KeyCode::Char(ch), KeyModifiers::CONTROL);
    app.process_key_for_test(key);
}

fn render_frame(app: &mut App) -> Terminal<TestBackend> {
    let backend = TestBackend::new(120, 40);
    let mut terminal = Terminal::new(backend).unwrap();
    terminal
        .draw(|frame| ui::render(frame, &mut app.state))
        .unwrap();
    terminal
}

fn buffer_text(terminal: &Terminal<TestBackend>) -> String {
    let buf = terminal.backend().buffer();
    let mut text = String::new();
    for y in 0..buf.area.height {
        for x in 0..buf.area.width {
            let cell = &buf[(x, y)];
            text.push_str(cell.symbol());
        }
    }
    text
}

// ── tests ────────────────────────────────────────────────────────────────

#[test]
fn welcome_screen_renders() {
    let (mut app, _tmp) = make_app();
    let terminal = render_frame(&mut app);
    let text = buffer_text(&terminal);
    assert!(text.contains("AVA"), "welcome screen should contain 'AVA'");
}

#[test]
fn input_field_accepts_text() {
    let (mut app, _tmp) = make_app();
    for ch in "hello".chars() {
        press_char(&mut app, ch);
    }
    assert_eq!(app.state.input.buffer, "hello");
}

#[test]
fn enter_submits_input() {
    let (mut app, _tmp) = make_app();
    for ch in "hello".chars() {
        press_char(&mut app, ch);
    }
    press(&mut app, KeyCode::Enter);
    // Buffer should be cleared after submit
    assert!(app.state.input.buffer.is_empty(), "input should be cleared after Enter");
    // A user message should have been added
    assert!(
        !app.state.messages.messages.is_empty(),
        "a message should be added on submit"
    );
}

#[test]
fn model_selector_opens() {
    let (mut app, _tmp) = make_app();
    press_ctrl(&mut app, 'm');
    assert_eq!(
        app.state.active_modal,
        Some(ModalType::ModelSelector),
        "Ctrl+M should open model selector"
    );
}

#[test]
fn command_palette_opens() {
    let (mut app, _tmp) = make_app();
    press_ctrl(&mut app, '/');
    assert_eq!(
        app.state.active_modal,
        Some(ModalType::CommandPalette),
        "Ctrl+/ should open command palette"
    );
}

#[test]
fn slash_opens_inline_autocomplete() {
    let (mut app, _tmp) = make_app();
    press_char(&mut app, '/');
    assert_eq!(
        app.state.active_modal, None,
        "typing '/' should NOT open a modal — it shows inline autocomplete"
    );
    assert_eq!(app.state.input.buffer, "/", "'/' should be typed into the buffer");
    assert!(
        app.state.input.has_slash_autocomplete(),
        "slash autocomplete menu should be visible after typing '/'"
    );
}

#[test]
fn sidebar_toggles() {
    let (mut app, _tmp) = make_app();
    assert!(!app.state.show_sidebar, "sidebar should start hidden");
    press_ctrl(&mut app, 's');
    assert!(app.state.show_sidebar, "Ctrl+S should show sidebar");
    press_ctrl(&mut app, 's');
    assert!(!app.state.show_sidebar, "Ctrl+S again should hide sidebar");
}

#[test]
fn status_bar_shows_model() {
    let (mut app, _tmp) = make_app();
    let terminal = render_frame(&mut app);
    let text = buffer_text(&terminal);
    assert!(
        text.contains("test-model"),
        "status bar should display the model name"
    );
}

#[test]
fn escape_closes_modal() {
    let (mut app, _tmp) = make_app();
    // Open command palette
    press_ctrl(&mut app, '/');
    assert_eq!(app.state.active_modal, Some(ModalType::CommandPalette));
    // Press Escape
    press(&mut app, KeyCode::Esc);
    assert_eq!(
        app.state.active_modal, None,
        "Esc should close the modal"
    );
}

#[test]
fn tool_approval_shows_risk() {
    use ava_tui::state::permission::{ApprovalRequest, InspectionInfo};
    use ava_permissions::tags::{RiskLevel, SafetyTag};

    let (mut app, _tmp) = make_app();

    // Enqueue a high-risk approval request
    let (tx, _rx) = tokio::sync::oneshot::channel();
    let request = ApprovalRequest {
        call: ava_types::ToolCall {
            id: "test-1".to_string(),
            name: "bash".to_string(),
            arguments: serde_json::json!({"command": "rm -rf /"}),
        },
        approve_tx: tx,
        inspection: Some(InspectionInfo {
            risk_level: RiskLevel::High,
            tags: vec![SafetyTag::Destructive],
            warnings: vec!["Dangerous command".to_string()],
        }),
    };
    app.state.permission.enqueue(request);
    app.state.active_modal = Some(ModalType::ToolApproval);

    let terminal = render_frame(&mut app);
    let text = buffer_text(&terminal);
    assert!(
        text.contains("HIGH"),
        "tool approval should display 'HIGH' risk label"
    );
}
