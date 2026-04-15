use super::*;
use ava_agent::control_plane::commands::{queue_message_tier, ControlPlaneCommand};
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
fn background_subagent_events_follow_real_tool_sequence() {
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

    app.handle_event(
        AppEvent::AgentRunEvent {
            run_id: 42,
            event: ava_agent::AgentEvent::ToolCall(ava_types::ToolCall {
                id: "call_subagent_bg".to_string(),
                name: "subagent".to_string(),
                arguments: serde_json::json!({"prompt": "Read AGENTS.md and summarize it.", "background": true}),
            }),
        },
        app_tx.clone(),
        agent_tx.clone(),
    );

    app.handle_event(
        AppEvent::AgentRunEvent {
            run_id: 42,
            event: ava_agent::AgentEvent::ToolResult(ava_types::ToolResult {
                call_id: "call_subagent_bg".to_string(),
                content: "Background sub-agent launched. You will be notified when it completes. Continue with other work.".to_string(),
                is_error: false,
            }),
        },
        app_tx.clone(),
        agent_tx.clone(),
    );

    {
        let bg = app.state.background.lock().unwrap();
        let task = bg.tasks.iter().find(|task| task.id == 1).expect("task");
        let sub_msg = task
            .messages
            .iter()
            .find(|msg| matches!(msg.kind, MessageKind::SubAgent))
            .expect("background subagent message should exist after launch ack");
        let sub_data = sub_msg
            .sub_agent
            .as_ref()
            .expect("background subagent data should exist after launch ack");
        assert!(
            sub_msg.is_streaming,
            "background launch ack should keep card running"
        );
        assert!(
            sub_data.is_running,
            "background launch ack should not mark subagent done"
        );
    }

    app.handle_event(
        AppEvent::AgentRunEvent {
            run_id: 42,
            event: ava_agent::AgentEvent::SubAgentComplete {
                call_id: String::new(),
                session_id: "sub-session-bg".to_string(),
                messages: vec![ava_types::Message::new(
                    ava_types::Role::Assistant,
                    "AGENTS summary",
                )],
                description: "Read AGENTS.md and summarize it.".to_string(),
                input_tokens: 13,
                output_tokens: 9,
                cost_usd: 0.02,
                agent_type: Some("scout".to_string()),
                provider: Some("openai".to_string()),
                resumed: false,
            },
        },
        app_tx,
        agent_tx,
    );

    let bg = app.state.background.lock().unwrap();
    let task = bg.tasks.iter().find(|task| task.id == 1).expect("task");
    let sub_msg = task
        .messages
        .iter()
        .find(|msg| matches!(msg.kind, MessageKind::SubAgent))
        .expect("background subagent message should exist");
    let sub_data = sub_msg
        .sub_agent
        .as_ref()
        .expect("background subagent data should exist");
    assert_eq!(sub_msg.content, "AGENTS summary");
    assert_eq!(sub_data.session_id.as_deref(), Some("sub-session-bg"));
    assert_eq!(sub_data.provider.as_deref(), Some("openai"));
    assert_eq!(sub_data.input_tokens, Some(13));
    assert_eq!(sub_data.output_tokens, Some(9));
    assert_eq!(sub_data.cost_usd, Some(0.02));
    assert!(!sub_data.is_running);

    let sub_state = app
        .state
        .agent
        .sub_agents
        .iter()
        .find(|sa| sa.description == "Read AGENTS.md and summarize it.")
        .expect("background subagent state should be tracked");
    assert!(!sub_state.is_running);
    assert_eq!(sub_state.session_id.as_deref(), Some("sub-session-bg"));
    assert_eq!(sub_state.provider.as_deref(), Some("openai"));
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

#[test]
fn foreground_background_subagent_events_stay_running_until_complete() {
    let temp = tempdir().expect("tempdir");
    let db_path = temp.path().join("data.db");
    let mut app = App::test_new(&db_path);
    let (app_tx, _) = mpsc::unbounded_channel();
    let (agent_tx, _) = mpsc::unbounded_channel();

    app.state.agent.is_running = true;
    app.foreground_run_id = Some(7);

    app.handle_event(
        AppEvent::AgentRunEvent {
            run_id: 7,
            event: ava_agent::AgentEvent::ToolCall(ava_types::ToolCall {
                id: "call_subagent_1".to_string(),
                name: "subagent".to_string(),
                arguments: serde_json::json!({"prompt": "Read AGENTS.md and summarize it.", "background": true}),
            }),
        },
        app_tx.clone(),
        agent_tx.clone(),
    );

    let sub_msg = app
        .state
        .messages
        .messages
        .iter()
        .find(|msg| matches!(msg.kind, MessageKind::SubAgent))
        .expect("subagent message should be created");
    let sub_data = sub_msg
        .sub_agent
        .as_ref()
        .expect("subagent data should exist");
    assert_eq!(sub_data.call_id, "call_subagent_1");
    assert!(sub_data.is_running);
    assert_eq!(sub_data.description, "Read AGENTS.md and summarize it.");

    app.handle_event(
        AppEvent::AgentRunEvent {
            run_id: 7,
            event: ava_agent::AgentEvent::ToolResult(ava_types::ToolResult {
                call_id: "call_subagent_1".to_string(),
                content: "Background sub-agent launched. You will be notified when it completes. Continue with other work.".to_string(),
                is_error: false,
            }),
        },
        app_tx.clone(),
        agent_tx.clone(),
    );

    let sub_msg = app
        .state
        .messages
        .messages
        .iter()
        .find(|msg| matches!(msg.kind, MessageKind::SubAgent))
        .expect("subagent message should still exist after tool result");
    let sub_data = sub_msg
        .sub_agent
        .as_ref()
        .expect("subagent data should still exist after tool result");
    assert!(sub_msg.is_streaming);
    assert!(sub_data.is_running);

    app.handle_event(
        AppEvent::AgentRunEvent {
            run_id: 7,
            event: ava_agent::AgentEvent::ToolCall(ava_types::ToolCall {
                id: "call_read_1".to_string(),
                name: "read".to_string(),
                arguments: serde_json::json!({"file_path": "AGENTS.md"}),
            }),
        },
        app_tx.clone(),
        agent_tx.clone(),
    );
    app.handle_event(
        AppEvent::AgentRunEvent {
            run_id: 7,
            event: ava_agent::AgentEvent::ToolResult(ava_types::ToolResult {
                call_id: "call_read_1".to_string(),
                content: "AGENTS content".to_string(),
                is_error: false,
            }),
        },
        app_tx.clone(),
        agent_tx.clone(),
    );

    let sub_msg = app
        .state
        .messages
        .messages
        .iter()
        .find(|msg| matches!(msg.kind, MessageKind::SubAgent))
        .expect("subagent message should still exist after parent tool activity");
    let sub_data = sub_msg
        .sub_agent
        .as_ref()
        .expect("subagent data should still exist after parent tool activity");
    assert!(
        sub_data.is_running,
        "parent tool activity should not complete background subagent"
    );

    app.handle_event(
        AppEvent::AgentRunEvent {
            run_id: 7,
            event: ava_agent::AgentEvent::SubAgentComplete {
                call_id: String::new(),
                session_id: "sub-session-1".to_string(),
                messages: vec![ava_types::Message::new(
                    ava_types::Role::Assistant,
                    "AGENTS summary",
                )],
                description: "Read AGENTS.md and summarize it.".to_string(),
                input_tokens: 11,
                output_tokens: 7,
                cost_usd: 0.01,
                agent_type: Some("scout".to_string()),
                provider: Some("openai".to_string()),
                resumed: false,
            },
        },
        app_tx,
        agent_tx,
    );

    let sub_msg = app
        .state
        .messages
        .messages
        .iter()
        .find(|msg| matches!(msg.kind, MessageKind::SubAgent))
        .expect("subagent message should still exist");
    let sub_data = sub_msg
        .sub_agent
        .as_ref()
        .expect("subagent data should still exist");
    assert_eq!(sub_data.session_id.as_deref(), Some("sub-session-1"));
    assert_eq!(sub_data.provider.as_deref(), Some("openai"));
    assert_eq!(sub_data.input_tokens, Some(11));
    assert_eq!(sub_data.output_tokens, Some(7));
    assert_eq!(sub_data.cost_usd, Some(0.01));

    let sub_state = app
        .state
        .agent
        .sub_agents
        .iter()
        .find(|sa| sa.description == "Read AGENTS.md and summarize it.")
        .expect("subagent state should be tracked");
    assert_eq!(sub_state.session_id.as_deref(), Some("sub-session-1"));
    assert_eq!(sub_state.provider.as_deref(), Some("openai"));
}

// /bg command removed — tests removed

// HQ multi-agent support removed from TUI

#[test]
fn queued_follow_up_messages_use_shared_command_family_labels() {
    let temp = tempdir().expect("tempdir");
    let db_path = temp.path().join("data.db");
    let mut app = App::test_new(&db_path);
    let (message_tx, mut message_rx) = mpsc::unbounded_channel();

    app.state.agent.message_tx = Some(message_tx);
    app.send_queued_message(
        "run tests when you finish".to_string(),
        queue_message_tier(ControlPlaneCommand::FollowUpAgent, None)
            .expect("follow-up command should map to a queue tier"),
    );

    let queued = message_rx.try_recv().expect("queued message");
    assert_eq!(queued.tier, ava_types::MessageTier::FollowUp);
    assert_eq!(queued.text, "run tests when you finish");

    let status = app.state.status_message.as_ref().expect("status message");
    assert_eq!(status.text, "Queued follow-up message");

    let last = app.state.messages.messages.last().expect("last ui message");
    assert_eq!(last.content, "[F] run tests when you finish");
}
