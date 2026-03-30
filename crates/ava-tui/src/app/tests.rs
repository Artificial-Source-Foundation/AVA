use super::*;
use tempfile::tempdir;

#[test]
fn backgrounded_run_events_stay_out_of_foreground() {
    let temp = tempdir().expect("tempdir");
    let db_path = temp.path().join("data.db");
    let mut app = App::test_new(&db_path);
    let (app_tx, _) = mpsc::unbounded_channel();
    let (agent_tx, _) = mpsc::unbounded_channel();

    app.state.agent.is_running = true;
    app.foreground_run_id = Some(42);
    app.state
        .messages
        .push(UiMessage::new(MessageKind::User, "ship it"));

    app.background_current_agent(app_tx.clone());

    // Messages preserved after backgrounding (user msg + system notification)
    assert!(!app.state.messages.messages.is_empty());
    assert_eq!(app.foreground_run_id, None);
    assert_eq!(app.background_run_routes.get(&42), Some(&1));

    app.handle_event(
        AppEvent::AgentRunEvent {
            run_id: 42,
            event: ava_agent::AgentEvent::Thinking("working".to_string()),
        },
        app_tx.clone(),
        agent_tx.clone(),
    );
    app.handle_event(
        AppEvent::AgentRunEvent {
            run_id: 42,
            event: ava_agent::AgentEvent::Token("done".to_string()),
        },
        app_tx.clone(),
        agent_tx.clone(),
    );

    // Foreground messages preserved (user msg + system notification) — background events don't leak in
    let fg_count = app.state.messages.messages.len();
    assert!(
        (1..=2).contains(&fg_count),
        "foreground should have original messages only, got {fg_count}"
    );
    let bg = app.state.background.lock().unwrap();
    let task = bg.tasks.iter().find(|task| task.id == 1).expect("task");
    assert_eq!(task.messages.len(), 3);
    assert_eq!(task.messages[0].kind, MessageKind::User);
    assert_eq!(task.messages[1].kind, MessageKind::Thinking);
    assert_eq!(task.messages[2].kind, MessageKind::Assistant);
    assert_eq!(task.messages[2].content, "done");
    drop(bg);

    app.handle_event(
        AppEvent::AgentRunDone {
            run_id: 42,
            result: Ok(ava_agent::stack::AgentRunResult {
                success: true,
                turns: 1,
                session: ava_types::Session::new(),
            }),
        },
        app_tx,
        agent_tx,
    );

    let bg = app.state.background.lock().unwrap();
    let task = bg.tasks.iter().find(|task| task.id == 1).expect("task");
    assert_eq!(task.status, crate::state::background::TaskStatus::Completed);
    assert!(!app.background_run_routes.contains_key(&42));
}

#[test]
fn model_switch_result_updates_state_and_closes_modal() {
    let temp = tempdir().expect("tempdir");
    let db_path = temp.path().join("data.db");
    let mut app = App::test_new(&db_path);
    let (app_tx, _) = mpsc::unbounded_channel();
    let (agent_tx, _) = mpsc::unbounded_channel();

    app.state.model_selector = Some(ModelSelectorState::default());
    app.state.active_modal = Some(ModalType::ModelSelector);

    app.handle_event(
        AppEvent::ModelSwitchFinished(crate::event::ModelSwitchResult {
            provider: "openrouter".to_string(),
            model: "anthropic/claude-sonnet-4".to_string(),
            display: "Claude Sonnet".to_string(),
            result: Ok(()),
            context: crate::event::ModelSwitchContext::Selector,
        }),
        app_tx,
        agent_tx,
    );

    assert_eq!(app.state.agent.provider_name, "openrouter");
    assert_eq!(app.state.agent.model_name, "anthropic/claude-sonnet-4");
    assert!(app.state.model_selector.is_none());
    assert!(app.state.active_modal.is_none());
    assert_eq!(
        app.state
            .status_message
            .as_ref()
            .map(|msg| msg.text.as_str()),
        Some("Switched to Claude Sonnet")
    );
}

// /bg command removed — tests removed

// HQ multi-agent support removed from TUI
