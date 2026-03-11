//! Tests for all TUI slash commands handled by `App::test_slash_command()`.

use ava_tui::app::{App, ModalType};
use ava_tui::state::messages::MessageKind;

// ── helpers ──────────────────────────────────────────────────────────────

fn make_app() -> (App, tempfile::TempDir) {
    let tmp = tempfile::tempdir().expect("tempdir");
    let db_path = tmp.path().join("test.db");
    let app = App::test_new(&db_path);
    (app, tmp)
}

// ── /help ────────────────────────────────────────────────────────────────

#[test]
fn help_returns_system_message() {
    let (mut app, _tmp) = make_app();
    let result = app.test_slash_command("/help");
    assert!(result.is_some(), "/help should return Some");
    let (kind, msg) = result.unwrap();
    assert_eq!(kind, MessageKind::System);
    assert!(msg.contains("Available commands:"), "help text should list commands");
    assert!(msg.contains("/model"), "help should mention /model");
    assert!(msg.contains("/clear"), "help should mention /clear");
    assert!(msg.contains("/help"), "help should mention /help");
    assert!(msg.contains("/theme"), "help should mention /theme");
    assert!(msg.contains("/compact"), "help should mention /compact");
    assert!(msg.contains("/sessions"), "help should mention /sessions");
    assert!(msg.contains("/status"), "help should mention /status");
    assert!(msg.contains("/diff"), "help should mention /diff");
    assert!(msg.contains("/commit"), "help should mention /commit");
    assert!(msg.contains("/tools"), "help should mention /tools");
    assert!(msg.contains("/mcp"), "help should mention /mcp");
    assert!(msg.contains("/connect"), "help should mention /connect");
    assert!(msg.contains("/disconnect"), "help should mention /disconnect");
    assert!(msg.contains("/copy"), "help should mention /copy");
    assert!(msg.contains("/think"), "help should mention /think");
    assert!(msg.contains("Keyboard shortcuts:"), "help should include keyboard shortcuts");
}

// ── /clear ───────────────────────────────────────────────────────────────

#[test]
fn clear_returns_none_and_clears_messages() {
    let (mut app, _tmp) = make_app();
    // Add some messages first
    app.state.messages.push(
        ava_tui::state::messages::UiMessage::new(MessageKind::User, "hello".to_string()),
    );
    app.state.messages.push(
        ava_tui::state::messages::UiMessage::new(MessageKind::Assistant, "hi".to_string()),
    );
    assert_eq!(app.state.messages.messages.len(), 2);

    let result = app.test_slash_command("/clear");
    assert!(result.is_none(), "/clear should return None");
    assert!(
        app.state.messages.messages.is_empty(),
        "messages should be cleared"
    );
    // Status bar should show confirmation
    assert!(app.state.status_message.is_some());
}

// ── /compact ─────────────────────────────────────────────────────────────

#[test]
fn compact_with_no_messages_reports_nothing_to_compact() {
    let (mut app, _tmp) = make_app();
    let result = app.test_slash_command("/compact");
    assert!(result.is_some(), "/compact should return Some");
    let (kind, msg) = result.unwrap();
    assert_eq!(kind, MessageKind::System);
    // With no messages, should indicate nothing to compact or low usage
    assert!(
        msg.to_lowercase().contains("no ") || msg.to_lowercase().contains("compact") || msg.to_lowercase().contains("empty"),
        "should indicate nothing to compact, got: {msg}"
    );
}

#[test]
fn compact_with_focus_instructions() {
    let (mut app, _tmp) = make_app();
    let result = app.test_slash_command("/compact focus on auth");
    assert!(result.is_some(), "/compact with focus should return Some");
    let (kind, _msg) = result.unwrap();
    assert_eq!(kind, MessageKind::System);
}

// ── /sessions ────────────────────────────────────────────────────────────

#[test]
fn sessions_returns_none_and_opens_modal() {
    let (mut app, _tmp) = make_app();
    let result = app.test_slash_command("/sessions");
    assert!(result.is_none(), "/sessions should return None (opens modal)");
    assert_eq!(
        app.state.active_modal,
        Some(ModalType::SessionList),
        "should open session list modal"
    );
}

// ── /theme ───────────────────────────────────────────────────────────────

#[test]
fn theme_no_args_returns_none_and_opens_selector() {
    let (mut app, _tmp) = make_app();
    let result = app.test_slash_command("/theme");
    assert!(result.is_none(), "/theme with no args should return None (opens modal)");
    assert_eq!(
        app.state.active_modal,
        Some(ModalType::ThemeSelector),
        "should open theme selector modal"
    );
    assert!(
        app.state.theme_selector.is_some(),
        "theme selector state should be populated"
    );
    assert!(
        app.state.theme_before_preview.is_some(),
        "should save previous theme for revert"
    );
}

#[test]
fn theme_with_valid_name_switches() {
    let (mut app, _tmp) = make_app();
    let result = app.test_slash_command("/theme dracula");
    assert!(result.is_some(), "/theme dracula should return Some");
    let (kind, msg) = result.unwrap();
    assert_eq!(kind, MessageKind::System);
    assert!(msg.contains("dracula"), "should mention theme name");
    assert_eq!(app.state.theme.name, "dracula", "theme should be dracula");
}

#[test]
fn theme_with_nord_switches() {
    let (mut app, _tmp) = make_app();
    let result = app.test_slash_command("/theme nord");
    let (kind, msg) = result.unwrap();
    assert_eq!(kind, MessageKind::System);
    assert!(msg.contains("nord"));
    assert_eq!(app.state.theme.name, "nord");
}

#[test]
fn theme_with_default_switches() {
    let (mut app, _tmp) = make_app();
    // Switch away from default first
    app.test_slash_command("/theme dracula");
    assert_eq!(app.state.theme.name, "dracula");
    // Switch back
    let result = app.test_slash_command("/theme default");
    let (kind, msg) = result.unwrap();
    assert_eq!(kind, MessageKind::System);
    assert!(msg.contains("default"));
    assert_eq!(app.state.theme.name, "default");
}

#[test]
fn theme_with_invalid_name_returns_error() {
    let (mut app, _tmp) = make_app();
    let result = app.test_slash_command("/theme nonexistent");
    assert!(result.is_some());
    let (kind, msg) = result.unwrap();
    assert_eq!(kind, MessageKind::Error);
    assert!(msg.contains("Unknown theme"), "should say unknown theme");
    assert!(msg.contains("nonexistent"), "should echo the bad name");
    assert!(msg.contains("Available:"), "should list available themes");
}

// ── /think ───────────────────────────────────────────────────────────────

#[test]
fn think_no_args_cycles_level() {
    let (mut app, _tmp) = make_app();
    // Default thinking level is Off, cycling should go to Low
    let result = app.test_slash_command("/think");
    assert!(result.is_some());
    let (kind, msg) = result.unwrap();
    assert_eq!(kind, MessageKind::System);
    assert!(msg.contains("Thinking level:"), "should show current level");
    assert!(msg.contains("low"), "cycling from Off should land on low");
}

#[test]
fn think_cycles_through_levels() {
    let (mut app, _tmp) = make_app();
    // Off -> Low -> Medium -> High -> Max -> Off
    let expected = ["low", "med", "high", "max", "off"];
    for level_name in &expected {
        let result = app.test_slash_command("/think");
        let (_, msg) = result.unwrap();
        assert!(
            msg.contains(level_name),
            "expected {level_name} but got: {msg}"
        );
    }
}

#[test]
fn think_with_valid_levels() {
    for (input, expected) in [
        ("off", "off"),
        ("low", "low"),
        ("med", "med"),
        ("high", "high"),
        ("max", "max"),
    ] {
        let (mut app, _tmp) = make_app();
        let result = app.test_slash_command(&format!("/think {input}"));
        assert!(result.is_some(), "/think {input} should return Some");
        let (kind, msg) = result.unwrap();
        assert_eq!(kind, MessageKind::System);
        assert!(
            msg.contains(expected),
            "for /think {input}: expected '{expected}' in '{msg}'"
        );
    }
}

#[test]
fn think_with_invalid_level_returns_error() {
    let (mut app, _tmp) = make_app();
    let result = app.test_slash_command("/think banana");
    assert!(result.is_some());
    let (kind, msg) = result.unwrap();
    assert_eq!(kind, MessageKind::Error);
    assert!(msg.contains("Invalid level"), "should say invalid level");
    assert!(msg.contains("/think"), "should show usage");
}

// ── /permissions ─────────────────────────────────────────────────────────

#[test]
fn permissions_toggles_and_returns_none() {
    let (mut app, _tmp) = make_app();
    let before = app.state.permission.permission_level;
    let result = app.test_slash_command("/permissions");
    assert!(result.is_none(), "/permissions should return None");
    assert_ne!(
        app.state.permission.permission_level, before,
        "permission level should have toggled"
    );
    // Status message should be set
    assert!(app.state.status_message.is_some());
}

#[test]
fn permissions_toggles_back_on_double_call() {
    let (mut app, _tmp) = make_app();
    let original = app.state.permission.permission_level;
    app.test_slash_command("/permissions");
    app.test_slash_command("/permissions");
    assert_eq!(
        app.state.permission.permission_level, original,
        "double toggle should restore original level"
    );
}

// ── /model ───────────────────────────────────────────────────────────────

#[test]
fn model_with_invalid_format_returns_error() {
    let (mut app, _tmp) = make_app();
    // No slash in the model name => error
    let result = app.test_slash_command("/model just-a-model");
    assert!(result.is_some());
    let (kind, msg) = result.unwrap();
    assert_eq!(kind, MessageKind::Error);
    assert!(msg.contains("Invalid format"), "should explain correct format");
    assert!(msg.contains("provider/model"), "should show expected format");
}

// ── /disconnect ──────────────────────────────────────────────────────────

#[test]
fn disconnect_no_args_returns_error() {
    let (mut app, _tmp) = make_app();
    let result = app.test_slash_command("/disconnect");
    assert!(result.is_some());
    let (kind, msg) = result.unwrap();
    assert_eq!(kind, MessageKind::Error);
    assert!(msg.contains("Usage:"), "should show usage hint");
    assert!(msg.contains("/disconnect"), "should echo the command");
}

// ── /status ──────────────────────────────────────────────────────────────
// /status uses tokio::task::block_in_place, needs a multi-threaded runtime.

#[tokio::test(flavor = "multi_thread", worker_threads = 2)]
async fn status_returns_system_info() {
    let (mut app, _tmp) = make_app();
    let result = app.test_slash_command("/status");
    assert!(result.is_some());
    let (kind, msg) = result.unwrap();
    assert_eq!(kind, MessageKind::System);
    assert!(msg.contains("Model:"), "should show model");
    assert!(msg.contains("test-provider/test-model"), "should show test provider/model");
    assert!(msg.contains("Tokens:"), "should show tokens");
    assert!(msg.contains("Session:"), "should show session");
    assert!(msg.contains("Tools:"), "should show tools");
    assert!(msg.contains("Working directory:"), "should show cwd");
}

// ── /diff ────────────────────────────────────────────────────────────────

#[test]
fn diff_returns_some() {
    let (mut app, _tmp) = make_app();
    let result = app.test_slash_command("/diff");
    // Will either show git diff output or an error if not in a git repo,
    // but it should always return Some
    assert!(result.is_some(), "/diff should return Some");
    let (kind, _msg) = result.unwrap();
    // Could be System (if git works) or Error (if git not found / not a repo)
    assert!(
        kind == MessageKind::System || kind == MessageKind::Error,
        "should be System or Error"
    );
}

// ── /commit ──────────────────────────────────────────────────────────────

#[test]
fn commit_returns_some() {
    let (mut app, _tmp) = make_app();
    let result = app.test_slash_command("/commit");
    assert!(result.is_some(), "/commit should return Some");
    let (kind, _msg) = result.unwrap();
    assert!(
        kind == MessageKind::System || kind == MessageKind::Error,
        "should be System or Error"
    );
}

// ── /mcp ─────────────────────────────────────────────────────────────────

#[test]
fn mcp_unknown_subcommand_returns_error() {
    let (mut app, _tmp) = make_app();
    let result = app.test_slash_command("/mcp bogus");
    assert!(result.is_some());
    let (kind, msg) = result.unwrap();
    assert_eq!(kind, MessageKind::Error);
    assert!(msg.contains("Unknown /mcp subcommand"), "should say unknown subcommand");
    assert!(msg.contains("bogus"), "should echo the bad subcommand");
    assert!(msg.contains("list"), "should suggest valid subcommands");
    assert!(msg.contains("reload"), "should suggest valid subcommands");
}

// ── /copy ────────────────────────────────────────────────────────────────

#[test]
fn copy_returns_none() {
    let (mut app, _tmp) = make_app();
    let result = app.test_slash_command("/copy");
    assert!(result.is_none(), "/copy should return None");
}

// ── /connect and /providers ──────────────────────────────────────────────
// These use tokio::task::block_in_place for credential loading.

#[tokio::test(flavor = "multi_thread", worker_threads = 2)]
async fn connect_no_args_opens_provider_modal() {
    let (mut app, _tmp) = make_app();
    let result = app.test_slash_command("/connect");
    assert!(result.is_none(), "/connect should return None (opens modal)");
    assert_eq!(
        app.state.active_modal,
        Some(ModalType::ProviderConnect),
        "should open provider connect modal"
    );
    assert!(
        app.state.provider_connect.is_some(),
        "provider connect state should be set"
    );
}

#[tokio::test(flavor = "multi_thread", worker_threads = 2)]
async fn connect_with_provider_arg_opens_modal() {
    let (mut app, _tmp) = make_app();
    let result = app.test_slash_command("/connect openrouter");
    assert!(result.is_none());
    assert_eq!(app.state.active_modal, Some(ModalType::ProviderConnect));
    assert!(app.state.provider_connect.is_some());
}

#[tokio::test(flavor = "multi_thread", worker_threads = 2)]
async fn providers_alias_opens_modal() {
    let (mut app, _tmp) = make_app();
    let result = app.test_slash_command("/providers");
    assert!(result.is_none());
    assert_eq!(app.state.active_modal, Some(ModalType::ProviderConnect));
}

// ── /tools ───────────────────────────────────────────────────────────────
// /tools (no args) uses tokio::task::block_in_place for tool listing.

#[tokio::test(flavor = "multi_thread", worker_threads = 2)]
async fn tools_no_args_opens_tool_list_modal() {
    let (mut app, _tmp) = make_app();
    let result = app.test_slash_command("/tools");
    assert!(result.is_none(), "/tools should return None (opens modal)");
    assert_eq!(
        app.state.active_modal,
        Some(ModalType::ToolList),
        "should open tool list modal"
    );
}

// ── unknown command ──────────────────────────────────────────────────────

#[test]
fn unknown_command_returns_error() {
    let (mut app, _tmp) = make_app();
    let result = app.test_slash_command("/foobar");
    assert!(result.is_some(), "unknown command should return Some");
    let (kind, msg) = result.unwrap();
    assert_eq!(kind, MessageKind::Error);
    assert!(msg.contains("Unknown command"), "should say unknown command");
    assert!(msg.contains("/foobar"), "should echo the bad command");
    assert!(msg.contains("/help"), "should suggest /help");
}

#[test]
fn unknown_command_xyz() {
    let (mut app, _tmp) = make_app();
    let result = app.test_slash_command("/xyz");
    let (kind, msg) = result.unwrap();
    assert_eq!(kind, MessageKind::Error);
    assert!(msg.contains("/xyz"));
}

#[test]
fn unknown_command_with_args() {
    let (mut app, _tmp) = make_app();
    let result = app.test_slash_command("/notreal some args");
    let (kind, msg) = result.unwrap();
    assert_eq!(kind, MessageKind::Error);
    assert!(msg.contains("/notreal"), "should include just the command, not args");
}

// ── non-slash input returns None ─────────────────────────────────────────

#[test]
fn non_slash_input_returns_none() {
    let (mut app, _tmp) = make_app();
    assert!(app.test_slash_command("hello world").is_none());
    assert!(app.test_slash_command("").is_none());
    assert!(app.test_slash_command("  no slash  ").is_none());
}

// ── whitespace handling ──────────────────────────────────────────────────

#[test]
fn leading_whitespace_is_trimmed() {
    let (mut app, _tmp) = make_app();
    // Input with leading spaces: after trim, starts with /
    let result = app.test_slash_command("  /help  ");
    assert!(result.is_some(), "trimmed input starting with / should be handled");
    let (kind, _) = result.unwrap();
    assert_eq!(kind, MessageKind::System);
}

#[test]
fn slash_alone_is_unknown_command() {
    let (mut app, _tmp) = make_app();
    let result = app.test_slash_command("/");
    assert!(result.is_some());
    let (kind, _) = result.unwrap();
    assert_eq!(kind, MessageKind::Error);
}

#[test]
fn command_with_extra_whitespace_in_args() {
    let (mut app, _tmp) = make_app();
    // /theme with extra spaces around the argument
    let result = app.test_slash_command("/theme   dracula  ");
    assert!(result.is_some());
    let (kind, _) = result.unwrap();
    assert_eq!(kind, MessageKind::System);
    assert_eq!(app.state.theme.name, "dracula");
}
