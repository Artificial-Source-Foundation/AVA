//! TUI interaction tests — verifies event handling, key bindings, modals,
//! and rendering without a real terminal, database, or external services.

use ava_tui::app::{App, ModalType, ViewMode};
use ava_tui::state::agent::SubAgentInfo;
use ava_tui::ui;
use ava_tui::ui::layout::{sidebar_visible, SIDEBAR_AUTO_SHOW_WIDTH, SIDEBAR_MANUAL_SHOW_WIDTH};
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
    assert!(
        text.contains("A focused AI workspace for coding in the terminal"),
        "welcome screen should contain the current product tagline"
    );
    assert!(
        text.contains("test-model"),
        "welcome screen should show the configured model"
    );
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
    assert!(
        app.state.input.buffer.is_empty(),
        "input should be cleared after Enter"
    );
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
    assert_eq!(
        app.state.input.buffer, "/",
        "'/' should be typed into the buffer"
    );
    assert!(
        app.state.input.has_slash_autocomplete(),
        "slash autocomplete menu should be visible after typing '/'"
    );
}

#[test]
fn slash_autocomplete_enter_executes_selected_command() {
    let (mut app, _tmp) = make_app();

    // Open inline autocomplete and move from default selection (/btw) to /help.
    press_char(&mut app, '/');
    assert_eq!(
        app.state.input.autocomplete_selected_value().as_deref(),
        Some("btw")
    );
    press(&mut app, KeyCode::Down);
    assert_eq!(
        app.state.input.autocomplete_selected_value().as_deref(),
        Some("help"),
        "Down should move selection to /help"
    );

    // Accept selected command directly from autocomplete with Enter and verify execution.
    press(&mut app, KeyCode::Enter);
    assert!(
        app.state.input.autocomplete.is_none(),
        "autocomplete should be dismissed after command execution"
    );
    assert_eq!(
        app.state.input.buffer, "",
        "input should be cleared after autocomplete execution"
    );
    assert_eq!(
        app.state.active_modal,
        Some(ModalType::InfoPanel),
        "/help should execute and open the info panel modal"
    );
    let panel = app
        .state
        .info_panel
        .as_ref()
        .expect("info panel should be set by /help");
    assert!(
        panel.title.contains("Help"),
        "info panel should show help title"
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
    assert_eq!(app.state.active_modal, None, "Esc should close the modal");
}

#[test]
fn tool_approval_shows_risk() {
    use ava_permissions::tags::{RiskLevel, SafetyTag};
    use ava_tui::state::permission::{ApprovalRequest, InspectionInfo};

    let (mut app, _tmp) = make_app();

    // Enqueue a high-risk approval request
    let request = ApprovalRequest {
        request_id: "approval-test-1".to_string(),
        run_id: None,
        call: ava_types::ToolCall {
            id: "test-1".to_string(),
            name: "bash".to_string(),
            arguments: serde_json::json!({"command": "rm -rf /"}),
        },
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
        text.contains("high"),
        "tool approval should display the lower-case risk badge"
    );
    assert!(
        text.contains("bash: rm -rf /"),
        "tool approval should show the tool name and command summary"
    );
}

#[test]
fn approval_dock_takes_precedence_over_transcript_hint() {
    use ava_permissions::tags::{RiskLevel, SafetyTag};
    use ava_tui::state::permission::{ApprovalRequest, InspectionInfo};

    let (mut app, _tmp) = make_app();
    app.state.view_mode = ViewMode::SubAgent {
        agent_index: 0,
        description: "Inspect files.".to_string(),
    };

    app.state.permission.enqueue(ApprovalRequest {
        request_id: "approval-transcript-precedence".to_string(),
        run_id: None,
        call: ava_types::ToolCall {
            id: "test-approval-precedence".to_string(),
            name: "bash".to_string(),
            arguments: serde_json::json!({"command": "git status"}),
        },
        inspection: Some(InspectionInfo {
            risk_level: RiskLevel::Low,
            tags: vec![SafetyTag::Destructive],
            warnings: vec![],
        }),
    });
    app.state.active_modal = Some(ModalType::ToolApproval);

    let terminal = render_frame(&mut app);
    let text = buffer_text(&terminal);
    assert!(
        text.contains("permission required"),
        "approval dock should remain visible in transcript view"
    );
    assert!(
        !text.contains("read-only transcript"),
        "transcript hint should be suppressed when approval dock is active"
    );
}

#[test]
fn sidebar_render_distinguishes_failed_and_successful_completed_subagents() {
    let (mut app, _tmp) = make_app();
    app.state.show_sidebar = true;

    app.state.agent.sub_agents.push(SubAgentInfo {
        call_id: "call-success".to_string(),
        agent_type: Some("scout".to_string()),
        description: "Successful subagent".to_string(),
        background: false,
        is_running: false,
        tool_count: 1,
        current_tool: None,
        started_at: std::time::Instant::now(),
        elapsed: Some(std::time::Duration::from_secs(1)),
        session_id: None,
        session_messages: vec![],
        provider: None,
        resumed: false,
        cost_usd: None,
        input_tokens: None,
        output_tokens: None,
    });
    app.state.agent.sub_agents.push(SubAgentInfo {
        call_id: "call-failed".to_string(),
        agent_type: Some("scout".to_string()),
        description: "Failed subagent".to_string(),
        background: false,
        is_running: false,
        tool_count: 1,
        current_tool: None,
        started_at: std::time::Instant::now(),
        elapsed: Some(std::time::Duration::from_secs(1)),
        session_id: None,
        session_messages: vec![],
        provider: None,
        resumed: false,
        cost_usd: None,
        input_tokens: None,
        output_tokens: None,
    });

    let mut success_message = ava_tui::state::messages::UiMessage::new(
        ava_tui::state::messages::MessageKind::SubAgent,
        "ok",
    );
    success_message.sub_agent = Some(ava_tui::state::messages::SubAgentData {
        agent_type: Some("scout".to_string()),
        description: "Successful subagent".to_string(),
        background: false,
        tool_count: 1,
        current_tool: None,
        duration: Some(std::time::Duration::from_secs(1)),
        is_running: false,
        failed: false,
        call_id: "call-success".to_string(),
        session_id: None,
        session_messages: vec![],
        provider: None,
        resumed: false,
        cost_usd: None,
        input_tokens: None,
        output_tokens: None,
    });
    app.state.messages.push(success_message);

    let mut failed_message = ava_tui::state::messages::UiMessage::new(
        ava_tui::state::messages::MessageKind::SubAgent,
        "failed",
    );
    failed_message.sub_agent = Some(ava_tui::state::messages::SubAgentData {
        agent_type: Some("scout".to_string()),
        description: "Failed subagent".to_string(),
        background: false,
        tool_count: 1,
        current_tool: None,
        duration: Some(std::time::Duration::from_secs(1)),
        is_running: false,
        failed: true,
        call_id: "call-failed".to_string(),
        session_id: None,
        session_messages: vec![],
        provider: None,
        resumed: false,
        cost_usd: None,
        input_tokens: None,
        output_tokens: None,
    });
    app.state.messages.push(failed_message);

    let terminal = render_frame(&mut app);
    let text = buffer_text(&terminal);

    assert!(
        text.contains("✓ Successful subagent"),
        "completed successful sub-agent should render with a check mark"
    );
    assert!(
        text.contains("✗ Failed subagent"),
        "completed failed sub-agent should render with a failure marker"
    );
}

#[test]
fn sidebar_wide_screens_still_require_explicit_toggle() {
    assert!(!sidebar_visible(SIDEBAR_AUTO_SHOW_WIDTH, false));
    assert!(sidebar_visible(SIDEBAR_AUTO_SHOW_WIDTH, true));
}

#[test]
fn sidebar_respects_middle_band_toggle() {
    assert!(!sidebar_visible(SIDEBAR_MANUAL_SHOW_WIDTH, false));
    assert!(sidebar_visible(SIDEBAR_MANUAL_SHOW_WIDTH, true));
}
