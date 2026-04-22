use super::*;
use crate::state::agent::SubAgentInfo;
use ava_agent_orchestration::stack::AgentRunResult;
use ava_control_plane::commands::{queue_message_tier, ControlPlaneCommand};
use ava_control_plane::events::{required_backend_event_kinds, CanonicalEventKind};
use ava_control_plane::interactive::InteractiveRequestKind;
use ava_permissions::tags::RiskLevel;
use clap::Parser;
use std::time::Duration;
use tempfile::tempdir;
use tokio::sync::{mpsc, oneshot};

fn parse_cli(args: &[&str]) -> crate::config::cli::CliArgs {
    crate::config::cli::CliArgs::parse_from(args)
}

fn sample_plan(summary: &str) -> ava_types::Plan {
    ava_types::Plan {
        steps: vec![ava_types::PlanStep {
            id: "step-1".to_string(),
            description: "Audit the current TUI lifecycle path".to_string(),
            files: vec!["crates/ava-tui/src/app/tests.rs".to_string()],
            action: ava_types::PlanAction::Research,
            depends_on: Vec::new(),
        }],
        summary: summary.to_string(),
        estimated_turns: Some(2),
        codename: Some("Milestone-3".to_string()),
    }
}

#[test]
fn resume_restore_uses_session_metadata_without_cli_agent_override() {
    let metadata = serde_json::json!({
        "provider": "openrouter",
        "model": "anthropic/claude-sonnet-4",
        "primaryAgentId": "architect",
        "primaryAgentPrompt": "You are the architect profile"
    });

    let restore_plan = resume_restore_from_metadata(&metadata, None, false);

    assert!(restore_plan.apply_primary_agent);
    assert_eq!(restore_plan.primary_agent_id.as_deref(), Some("architect"));
    assert_eq!(
        restore_plan.primary_agent_prompt.as_deref(),
        Some("You are the architect profile")
    );
    assert_eq!(
        restore_plan.restore_model,
        Some((
            "openrouter".to_string(),
            "anthropic/claude-sonnet-4".to_string()
        ))
    );
}

#[test]
fn resume_restore_skips_session_metadata_when_cli_agent_override_present() {
    let metadata = serde_json::json!({
        "provider": "openrouter",
        "model": "anthropic/claude-sonnet-4",
        "primaryAgentId": "architect",
        "primaryAgentPrompt": "You are the architect profile"
    });

    let restore_plan = resume_restore_from_metadata(&metadata, Some("coder"), false);

    assert!(!restore_plan.apply_primary_agent);
    assert!(restore_plan.primary_agent_id.is_none());
    assert!(restore_plan.primary_agent_prompt.is_none());
    assert!(restore_plan.restore_model.is_none());
}

#[test]
fn resume_restore_skips_session_model_when_cli_provider_or_model_override_present() {
    let metadata = serde_json::json!({
        "provider": "openrouter",
        "model": "anthropic/claude-sonnet-4",
        "primaryAgentId": "architect",
        "primaryAgentPrompt": "You are the architect profile"
    });

    let restore_plan = resume_restore_from_metadata(&metadata, None, true);

    assert!(restore_plan.apply_primary_agent);
    assert_eq!(restore_plan.primary_agent_id.as_deref(), Some("architect"));
    assert_eq!(
        restore_plan.primary_agent_prompt.as_deref(),
        Some("You are the architect profile")
    );
    assert!(restore_plan.restore_model.is_none());
}

#[test]
fn persisted_primary_agent_prompt_round_trips_and_cli_agent_override_wins() {
    let temp = tempdir().expect("tempdir");
    let db_path = temp.path().join("data.db");
    let mut app = App::test_new(&db_path);
    let session = app.state.session.create_session().expect("session");

    app.state.agent.provider_name = "openrouter".to_string();
    app.state.agent.model_name = "anthropic/claude-sonnet-4".to_string();
    app.state.agent.set_primary_agent_profile(
        Some("architect".to_string()),
        Some("You are the architect profile".to_string()),
        None,
    );

    app.finish_run(AgentRunResult {
        success: true,
        turns: 1,
        session,
    });

    let saved = app
        .state
        .session
        .list_recent(1)
        .expect("load saved session")
        .into_iter()
        .next()
        .expect("saved session");

    let restore_plan = resume_restore_from_metadata(&saved.metadata, None, false);
    assert!(restore_plan.apply_primary_agent);
    assert_eq!(restore_plan.primary_agent_id.as_deref(), Some("architect"));
    assert_eq!(
        restore_plan.primary_agent_prompt.as_deref(),
        Some("You are the architect profile")
    );
    assert_eq!(
        restore_plan.restore_model,
        Some((
            "openrouter".to_string(),
            "anthropic/claude-sonnet-4".to_string()
        ))
    );

    let restore_override = resume_restore_from_metadata(&saved.metadata, Some("coder"), false);
    assert!(!restore_override.apply_primary_agent);
    assert!(restore_override.primary_agent_id.is_none());
    assert!(restore_override.primary_agent_prompt.is_none());
    assert!(restore_override.restore_model.is_none());
}

#[tokio::test]
async fn resumed_app_path_keeps_cli_agent_profile_active() {
    let temp = tempdir().expect("tempdir");
    let db_path = temp.path().join("data.db");
    let mut app = App::test_new(&db_path);
    app.cli_agent_override = Some("coder".to_string());
    app.state.agent.primary_agent_id = Some("coder".to_string());
    app.state.agent.primary_agent_prompt = Some("You are coder".to_string());

    let mut session = ava_types::Session::new();
    session.metadata = serde_json::json!({
        "provider": "openrouter",
        "model": "anthropic/claude-sonnet-4",
        "primaryAgentId": "architect",
        "primaryAgentPrompt": "You are the architect profile"
    });

    app.apply_resume_state(&session).await;

    assert_eq!(app.state.agent.primary_agent_id.as_deref(), Some("coder"));
    assert_eq!(
        app.state.agent.primary_agent_prompt.as_deref(),
        Some("You are coder")
    );
}

#[tokio::test]
async fn resumed_app_path_applies_session_primary_agent_profile_without_override() {
    let temp = tempdir().expect("tempdir");
    let db_path = temp.path().join("data.db");
    let mut app = App::test_new(&db_path);
    app.state.agent.primary_agent_id = Some("coder".to_string());
    app.state.agent.primary_agent_prompt = Some("You are coder".to_string());

    let mut session = ava_types::Session::new();
    session.metadata = serde_json::json!({
        "provider": "openrouter",
        "model": "anthropic/claude-sonnet-4",
        "primaryAgentId": "architect",
        "primaryAgentPrompt": "You are the architect profile"
    });

    app.apply_resume_state(&session).await;

    assert_eq!(
        app.state.agent.primary_agent_id.as_deref(),
        Some("architect")
    );
    assert_eq!(
        app.state.agent.primary_agent_prompt.as_deref(),
        Some("You are the architect profile")
    );
}

#[tokio::test]
async fn resumed_app_path_rehydrates_legacy_primary_agent_prompt_when_metadata_missing() {
    let temp = tempdir().expect("tempdir");
    let db_path = temp.path().join("data.db");
    let mut app = App::test_new(&db_path);
    app.state.agent.primary_agent_id = Some("architect".to_string());
    app.state.agent.primary_agent_prompt = Some("You are the architect profile".to_string());

    let mut session = ava_types::Session::new();
    session.metadata = serde_json::json!({
        "provider": "openrouter",
        "model": "anthropic/claude-sonnet-4",
        "primaryAgentId": "architect"
    });

    app.apply_resume_state(&session).await;

    assert_eq!(
        app.state.agent.primary_agent_id.as_deref(),
        Some("architect")
    );
    assert_eq!(
        app.state.agent.primary_agent_prompt.as_deref(),
        Some("You are the architect profile")
    );
}

#[tokio::test]
async fn resumed_app_path_surfaces_model_restore_failures() {
    let temp = tempdir().expect("tempdir");
    let db_path = temp.path().join("data.db");
    let mut app = App::test_new(&db_path);

    let mut session = ava_types::Session::new();
    session.metadata = serde_json::json!({
        "provider": "openrouter",
        "model": "anthropic/claude-sonnet-4"
    });

    app.apply_resume_state(&session).await;

    let has_restore_error = app.state.messages.messages.iter().any(|msg| {
        msg.kind == MessageKind::Error
            && msg.content.contains(
                "Failed to restore resumed session model openrouter/anthropic/claude-sonnet-4",
            )
    });
    assert!(has_restore_error, "expected restore-model error message");
}

#[test]
fn tab_cycles_configured_primary_agents_before_mode_switching() {
    let temp = tempdir().expect("tempdir");
    let db_path = temp.path().join("data.db");
    let mut app = App::test_new(&db_path);
    app.state.primary_agent_profiles = vec![
        PrimaryAgentProfile {
            id: "architect".to_string(),
            provider: None,
            model: None,
            prompt: Some("Architecture-first".to_string()),
            description: Some("Architecture profile".to_string()),
        },
        PrimaryAgentProfile {
            id: "coder".to_string(),
            provider: None,
            model: None,
            prompt: Some("Implementation-first".to_string()),
            description: Some("Implementation profile".to_string()),
        },
    ];

    app.process_key_for_test(crossterm::event::KeyEvent::from(KeyCode::Tab));
    assert_eq!(
        app.state.agent.primary_agent_id.as_deref(),
        Some("architect")
    );
    assert_eq!(app.state.agent_mode, AgentMode::Code);

    app.process_key_for_test(crossterm::event::KeyEvent::from(KeyCode::Tab));
    assert_eq!(app.state.agent.primary_agent_id.as_deref(), Some("coder"));
    assert_eq!(app.state.agent_mode, AgentMode::Code);

    let status = app.state.status_message.as_ref().expect("status message");
    assert_eq!(status.text, "Primary agent: coder");
}

#[test]
fn shift_tab_cycles_primary_agents_in_reverse() {
    let temp = tempdir().expect("tempdir");
    let db_path = temp.path().join("data.db");
    let mut app = App::test_new(&db_path);
    app.state.primary_agent_profiles = vec![
        PrimaryAgentProfile {
            id: "architect".to_string(),
            provider: None,
            model: None,
            prompt: None,
            description: None,
        },
        PrimaryAgentProfile {
            id: "coder".to_string(),
            provider: None,
            model: None,
            prompt: None,
            description: None,
        },
    ];

    app.process_key_for_test(crossterm::event::KeyEvent::from(KeyCode::BackTab));

    assert_eq!(app.state.agent.primary_agent_id.as_deref(), Some("coder"));
    assert_eq!(app.state.agent_mode, AgentMode::Code);
}

#[test]
fn tab_falls_back_to_mode_cycle_when_no_primary_agents_are_configured() {
    let temp = tempdir().expect("tempdir");
    let db_path = temp.path().join("data.db");
    let mut app = App::test_new(&db_path);

    app.process_key_for_test(crossterm::event::KeyEvent::from(KeyCode::Tab));

    assert_eq!(app.state.agent_mode, AgentMode::Plan);
    assert!(app.state.agent.primary_agent_id.is_none());
    let status = app.state.status_message.as_ref().expect("status message");
    assert_eq!(status.text, "Mode: Plan");
}

#[test]
fn tab_does_not_switch_primary_agent_while_running() {
    let temp = tempdir().expect("tempdir");
    let db_path = temp.path().join("data.db");
    let mut app = App::test_new(&db_path);
    app.state.primary_agent_profiles = vec![PrimaryAgentProfile {
        id: "architect".to_string(),
        provider: None,
        model: None,
        prompt: None,
        description: None,
    }];
    app.state.agent.is_running = true;

    app.process_key_for_test(crossterm::event::KeyEvent::from(KeyCode::Tab));

    assert!(app.state.agent.primary_agent_id.is_none());
    let status = app.state.status_message.as_ref().expect("status message");
    assert_eq!(
        status.text,
        "Cannot switch primary agent while a run is active"
    );
}

#[tokio::test]
async fn app_new_errors_when_continue_requested_but_no_sessions_exist() {
    let cli = parse_cli(&["ava", "--continue"]);
    let err = match App::new(cli).await {
        Ok(_) => panic!("expected resume startup failure"),
        Err(err) => err,
    };
    assert!(err
        .to_string()
        .contains("--continue was requested but no existing sessions were found"));
}

#[tokio::test]
async fn app_new_errors_when_requested_session_is_invalid_uuid() {
    let cli = parse_cli(&["ava", "--session", "not-a-uuid"]);
    let err = match App::new(cli).await {
        Ok(_) => panic!("expected invalid --session startup failure"),
        Err(err) => err,
    };
    assert!(err
        .to_string()
        .contains("Invalid --session id 'not-a-uuid': expected UUID format"));
}

#[test]
fn session_loaded_event_restores_primary_agent_prompt_metadata() {
    let temp = tempdir().expect("tempdir");
    let db_path = temp.path().join("data.db");
    let mut app = App::test_new(&db_path);
    let session = ava_types::Session::new();
    let (app_tx, _) = mpsc::unbounded_channel();
    let (agent_tx, _) = mpsc::unbounded_channel();

    app.handle_event(
        AppEvent::SessionLoaded(Ok(crate::event::SessionLoadResult {
            session,
            restore_model: None,
            restore_primary_agent_id: Some("architect".to_string()),
            restore_primary_agent_prompt: Some("You are the architect profile".to_string()),
        })),
        app_tx,
        agent_tx,
    );

    assert_eq!(
        app.state.agent.primary_agent_id.as_deref(),
        Some("architect")
    );
    assert_eq!(
        app.state.agent.primary_agent_prompt.as_deref(),
        Some("You are the architect profile")
    );
}

fn sample_approval_request(
    run_id: &str,
    call_id: &str,
    command: &str,
    reply: oneshot::Sender<ava_tools::permission_middleware::ToolApproval>,
) -> ava_tools::permission_middleware::ApprovalRequest {
    ava_tools::permission_middleware::ApprovalRequest {
        run_id: Some(run_id.to_string()),
        call: ava_types::ToolCall {
            id: call_id.to_string(),
            name: "bash".to_string(),
            arguments: serde_json::json!({"command": command}),
        },
        inspection: ava_permissions::inspector::InspectionResult {
            action: ava_permissions::Action::Ask,
            reason: "background approval".to_string(),
            risk_level: RiskLevel::High,
            tags: Vec::new(),
            warnings: vec!["danger".to_string()],
        },
        reply,
    }
}

async fn recv_interactive_cleared(app_rx: &mut mpsc::UnboundedReceiver<AppEvent>) -> AppEvent {
    tokio::time::timeout(Duration::from_millis(100), async {
        loop {
            match app_rx.recv().await {
                Some(event @ AppEvent::InteractiveRequestCleared { .. }) => return event,
                Some(_) => continue,
                None => panic!("app event channel closed before clear event"),
            }
        }
    })
    .await
    .expect("interactive clear event")
}

#[test]
fn btw_branch_stashes_and_restores_active_session_snapshot() {
    let temp = tempdir().expect("tempdir");
    let db_path = temp.path().join("data.db");
    let mut app = App::test_new(&db_path);
    let session = app.state.session.create_session().expect("session");

    app.start_btw_branch(None);
    assert!(app.state.session.current_session.is_none());

    app.end_btw_branch();
    assert_eq!(
        app.state
            .session
            .current_session
            .as_ref()
            .map(|current| current.id),
        Some(session.id)
    );
}

#[test]
fn backgrounded_run_events_stay_out_of_foreground() {
    let temp = tempdir().expect("tempdir");
    let db_path = temp.path().join("data.db");
    let mut app = App::test_new(&db_path);
    let (app_tx, _) = mpsc::unbounded_channel();
    let (agent_tx, _) = mpsc::unbounded_channel();
    app.foreground_run_id = Some(0);

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
            result: Ok(ava_agent_orchestration::stack::AgentRunResult {
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

    {
        let bg = app.state.background.lock().unwrap();
        let task = bg.tasks.iter().find(|task| task.id == 1).expect("task");
        let sub_msg = task
            .messages
            .iter()
            .find(|msg| matches!(msg.kind, MessageKind::SubAgent))
            .expect("background subagent message should exist after tool call");
        let sub_data = sub_msg
            .sub_agent
            .as_ref()
            .expect("background subagent data should exist after tool call");
        assert!(
            sub_data
                .session_messages
                .iter()
                .any(|msg| msg.content.contains("Live transcript updates will appear")),
            "background child transcript should start with a read-only placeholder before real child events arrive"
        );
    }

    app.handle_event(
        AppEvent::AgentRunEvent {
            run_id: 42,
            event: ava_agent::AgentEvent::ToolResult(ava_types::ToolResult {
                call_id: "call_subagent_bg".to_string(),
                content: "Background agent launched. Continue with the main task; AVA will surface completion when it finishes.".to_string(),
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
        assert!(
            sub_data
                .session_messages
                .iter()
                .any(|msg| msg.content.contains("Live transcript updates will appear")),
            "background launch ack should preserve the placeholder transcript until real child events arrive"
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
                description: "[scout] Read AGENTS.md and summarize it.".to_string(),
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
                content: "Background agent launched. Continue with the main task; AVA will surface completion when it finishes.".to_string(),
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

#[test]
fn background_agent_completion_notifies_parent_and_queues_follow_up() {
    let temp = tempdir().expect("tempdir");
    let db_path = temp.path().join("data.db");
    let mut app = App::test_new(&db_path);
    let (app_tx, _) = mpsc::unbounded_channel();
    let (agent_tx, _) = mpsc::unbounded_channel();
    let (message_tx, mut message_rx) = mpsc::unbounded_channel();

    app.state.agent.is_running = true;
    app.foreground_run_id = Some(7);
    app.state.agent.message_tx = Some(message_tx);

    app.handle_event(
        AppEvent::AgentRunEvent {
            run_id: 7,
            event: ava_agent::AgentEvent::ToolCall(ava_types::ToolCall {
                id: "call_bg_notify".to_string(),
                name: "background_agent".to_string(),
                arguments: serde_json::json!({"prompt": "Inspect docs", "agent": "scout"}),
            }),
        },
        app_tx.clone(),
        agent_tx.clone(),
    );

    app.handle_event(
        AppEvent::AgentRunEvent {
            run_id: 7,
            event: ava_agent::AgentEvent::SubAgentComplete {
                call_id: "call_bg_notify".to_string(),
                session_id: uuid::Uuid::new_v4().to_string(),
                messages: vec![ava_types::Message::new(
                    ava_types::Role::Assistant,
                    "Docs summary",
                )],
                description: "Inspect docs".to_string(),
                input_tokens: 1,
                output_tokens: 1,
                cost_usd: 0.0,
                agent_type: Some("scout".to_string()),
                provider: None,
                resumed: false,
            },
        },
        app_tx,
        agent_tx,
    );

    let queued = message_rx
        .try_recv()
        .expect("background completion should queue follow-up");
    assert!(matches!(queued.tier, ava_types::MessageTier::FollowUp));
    assert!(queued
        .text
        .contains("Background agent completed. Task: Inspect docs"));
    assert!(queued.text.contains("Docs summary"));
    assert!(app.state.messages.messages.iter().any(|msg| msg
        .content
        .contains("Background agent finished: Inspect docs")));
}

#[test]
fn background_agent_completion_reactivates_idle_parent() {
    let temp = tempdir().expect("tempdir");
    let db_path = temp.path().join("data.db");
    let mut app = App::test_new(&db_path);
    let (app_tx, _) = mpsc::unbounded_channel();
    let (agent_tx, _) = mpsc::unbounded_channel();

    let session = app.state.session.create_session().expect("create session");
    app.state.session.current_session = Some(session);
    app.foreground_run_id = None;
    app.state.agent.is_running = false;
    app.state.agent.message_tx = None;

    app.state.agent.sub_agents.push(SubAgentInfo {
        call_id: "call_bg_restart".to_string(),
        agent_type: Some("scout".to_string()),
        description: "Inspect docs".to_string(),
        background: true,
        is_running: true,
        tool_count: 0,
        current_tool: None,
        started_at: std::time::Instant::now(),
        elapsed: None,
        session_id: None,
        session_messages: initial_subagent_session_messages("Inspect docs", true),
        provider: None,
        resumed: false,
        cost_usd: None,
        input_tokens: None,
        output_tokens: None,
    });

    app.handle_event(
        AppEvent::AgentRunEvent {
            run_id: 999,
            event: ava_agent::AgentEvent::SubAgentComplete {
                call_id: "call_bg_restart".to_string(),
                session_id: uuid::Uuid::new_v4().to_string(),
                messages: vec![ava_types::Message::new(
                    ava_types::Role::Assistant,
                    "Docs summary",
                )],
                description: "Inspect docs".to_string(),
                input_tokens: 1,
                output_tokens: 1,
                cost_usd: 0.0,
                agent_type: Some("scout".to_string()),
                provider: None,
                resumed: false,
            },
        },
        app_tx,
        agent_tx,
    );

    assert!(app.state.agent.is_running);
    assert_eq!(app.foreground_run_id, Some(1));
    assert!(app.state.messages.messages.iter().any(|msg| msg
        .content
        .contains("Background agent completed. Task: Inspect docs")));
}

#[test]
fn subagent_completion_reconstructs_tool_history_for_child_view() {
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
                arguments: serde_json::json!({"prompt": "Inspect files.", "agent": "scout", "background": false}),
            }),
        },
        app_tx.clone(),
        agent_tx.clone(),
    );

    app.handle_event(
        AppEvent::AgentRunEvent {
            run_id: 7,
            event: ava_agent::AgentEvent::SubAgentComplete {
                call_id: "call_subagent_1".to_string(),
                session_id: "child-session-1".to_string(),
                messages: vec![
                    ava_types::Message::new(ava_types::Role::Assistant, "I'll inspect the repo.")
                        .with_tool_calls(vec![ava_types::ToolCall {
                            id: "tool-1".to_string(),
                            name: "glob".to_string(),
                            arguments: serde_json::json!({"pattern": "src/**/*.rs"}),
                        }]),
                    ava_types::Message::new(ava_types::Role::Tool, "")
                        .with_tool_results(vec![ava_types::ToolResult {
                            call_id: "tool-1".to_string(),
                            content: "src/main.rs".to_string(),
                            is_error: false,
                        }])
                        .with_tool_call_id("tool-1"),
                    ava_types::Message::new(ava_types::Role::Assistant, "Done."),
                ],
                description: "[scout] Inspect files.".to_string(),
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

    assert_eq!(sub_data.agent_type.as_deref(), Some("scout"));
    assert_eq!(sub_data.session_messages.len(), 4);
    assert!(matches!(
        sub_data.session_messages[0].kind,
        MessageKind::Assistant
    ));
    assert!(matches!(
        sub_data.session_messages[1].kind,
        MessageKind::ToolCall
    ));
    assert_eq!(
        sub_data.session_messages[1].tool_name.as_deref(),
        Some("glob")
    );
    assert!(matches!(
        sub_data.session_messages[2].kind,
        MessageKind::ToolResult
    ));
    assert_eq!(
        sub_data.session_messages[2].tool_name.as_deref(),
        Some("glob")
    );
    assert!(matches!(
        sub_data.session_messages[3].kind,
        MessageKind::Assistant
    ));
}

#[test]
fn subagent_live_updates_stream_into_child_transcript_before_completion() {
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
                id: "call_live_subagent".to_string(),
                name: "subagent".to_string(),
                arguments: serde_json::json!({"prompt": "Inspect files.", "agent": "scout", "background": false}),
            }),
        },
        app_tx.clone(),
        agent_tx.clone(),
    );

    app.handle_event(
        AppEvent::AgentRunEvent {
            run_id: 7,
            event: ava_agent::AgentEvent::SubAgentUpdate {
                call_id: "call_live_subagent".to_string(),
                description: "[scout] Inspect files.".to_string(),
                event: ava_agent::agent_loop::SubAgentLiveEvent::Started {
                    session_id: "child-live-session".to_string(),
                },
            },
        },
        app_tx.clone(),
        agent_tx.clone(),
    );

    app.handle_event(
        AppEvent::AgentRunEvent {
            run_id: 7,
            event: ava_agent::AgentEvent::SubAgentUpdate {
                call_id: "call_live_subagent".to_string(),
                description: "[scout] Inspect files.".to_string(),
                event: ava_agent::agent_loop::SubAgentLiveEvent::Thinking(
                    "Scanning manifests".to_string(),
                ),
            },
        },
        app_tx.clone(),
        agent_tx.clone(),
    );

    app.handle_event(
        AppEvent::AgentRunEvent {
            run_id: 7,
            event: ava_agent::AgentEvent::SubAgentUpdate {
                call_id: "call_live_subagent".to_string(),
                description: "[scout] Inspect files.".to_string(),
                event: ava_agent::agent_loop::SubAgentLiveEvent::ToolCall(ava_types::ToolCall {
                    id: "tool-live-1".to_string(),
                    name: "glob".to_string(),
                    arguments: serde_json::json!({"pattern": "src/**/*.rs"}),
                }),
            },
        },
        app_tx.clone(),
        agent_tx.clone(),
    );

    let subagent = &app.state.agent.sub_agents[0];
    assert_eq!(subagent.session_id.as_deref(), Some("child-live-session"));
    assert!(subagent
        .session_messages
        .iter()
        .any(|msg| matches!(msg.kind, MessageKind::Thinking)
            && msg.content.contains("Scanning manifests")));
    assert!(subagent
        .session_messages
        .iter()
        .any(|msg| matches!(msg.kind, MessageKind::ToolCall)
            && msg.tool_name.as_deref() == Some("glob")));
}

#[test]
fn subagent_live_updates_with_empty_call_id_fall_back_to_description_matching() {
    let temp = tempdir().expect("tempdir");
    let db_path = temp.path().join("data.db");
    let mut app = App::test_new(&db_path);
    let (app_tx, _) = mpsc::unbounded_channel();
    let (agent_tx, _) = mpsc::unbounded_channel();

    app.state.agent.sub_agents.push(SubAgentInfo {
        call_id: String::new(),
        agent_type: Some("scout".to_string()),
        description: "Inspect files.".to_string(),
        background: false,
        is_running: true,
        tool_count: 0,
        current_tool: None,
        started_at: std::time::Instant::now(),
        elapsed: None,
        session_id: None,
        session_messages: initial_subagent_session_messages("Inspect files.", false),
        provider: None,
        resumed: false,
        cost_usd: None,
        input_tokens: None,
        output_tokens: None,
    });

    app.handle_event(
        AppEvent::AgentRunEvent {
            run_id: 0,
            event: ava_agent::AgentEvent::SubAgentUpdate {
                call_id: String::new(),
                description: "[scout] Inspect files.".to_string(),
                event: ava_agent::agent_loop::SubAgentLiveEvent::Thinking(
                    "Scanning repo".to_string(),
                ),
            },
        },
        app_tx.clone(),
        agent_tx.clone(),
    );

    app.handle_event(
        AppEvent::AgentRunEvent {
            run_id: 0,
            event: ava_agent::AgentEvent::SubAgentComplete {
                call_id: String::new(),
                session_id: uuid::Uuid::new_v4().to_string(),
                messages: vec![ava_types::Message::new(ava_types::Role::Assistant, "Done")],
                description: "[scout] Inspect files.".to_string(),
                input_tokens: 0,
                output_tokens: 0,
                cost_usd: 0.0,
                agent_type: Some("scout".to_string()),
                provider: None,
                resumed: false,
            },
        },
        app_tx,
        agent_tx,
    );

    assert!(app.state.agent.sub_agents[0]
        .session_messages
        .iter()
        .any(|msg| msg.content.contains("Done")));
}

#[test]
fn enter_subagent_view_reloads_canonical_child_session_from_cache() {
    let temp = tempdir().expect("tempdir");
    let db_path = temp.path().join("data.db");
    let mut app = App::test_new(&db_path);

    app.state.agent.sub_agents.push(SubAgentInfo {
        call_id: "call-checkpoint-child".to_string(),
        agent_type: Some("scout".to_string()),
        description: "Inspect files.".to_string(),
        background: false,
        is_running: true,
        tool_count: 0,
        current_tool: None,
        started_at: std::time::Instant::now(),
        elapsed: None,
        session_id: Some(uuid::Uuid::nil().to_string()),
        session_messages: initial_subagent_session_messages("Inspect files.", false),
        provider: None,
        resumed: false,
        cost_usd: None,
        input_tokens: None,
        output_tokens: None,
    });
    let mut card = UiMessage::new(MessageKind::SubAgent, String::new());
    card.sub_agent = Some(crate::state::messages::SubAgentData {
        agent_type: Some("scout".to_string()),
        description: "Inspect files.".to_string(),
        background: false,
        tool_count: 0,
        current_tool: None,
        duration: None,
        is_running: true,
        failed: false,
        call_id: "call-checkpoint-child".to_string(),
        session_id: Some(uuid::Uuid::nil().to_string()),
        session_messages: initial_subagent_session_messages("Inspect files.", false),
        provider: None,
        resumed: false,
        cost_usd: None,
        input_tokens: None,
        output_tokens: None,
    });
    app.state.messages.push(card);

    let mut child_session = ava_types::Session::new();
    child_session.metadata["parent_id"] = serde_json::json!(uuid::Uuid::new_v4().to_string());
    child_session.metadata["is_sub_agent"] = serde_json::json!(true);
    child_session.metadata["agent_type"] = serde_json::json!("scout");
    child_session.add_message(ava_types::Message::new(
        ava_types::Role::User,
        "Inspect files.",
    ));
    child_session.add_message(ava_types::Message::new(
        ava_types::Role::Assistant,
        "Visible child output",
    ));
    app.state.agent.sub_agents[0].session_id = Some(child_session.id.to_string());
    if let Some(data) = app.state.messages.messages[0].sub_agent.as_mut() {
        data.session_id = Some(child_session.id.to_string());
    }
    app.state.session.cache_session(&child_session);

    let loaded = app
        .state
        .session
        .get_or_load_session(child_session.id)
        .expect("session lookup should succeed")
        .expect("child session should be cached");
    assert_eq!(loaded.messages, child_session.messages);

    assert!(app.enter_sub_agent_view(0));
    assert!(matches!(
        app.state.agent.sub_agents[0].session_messages[0].kind,
        MessageKind::User
    ));
    assert_eq!(
        app.state.agent.sub_agents[0].session_messages[0].content,
        "Inspect files."
    );
    assert_eq!(
        app.state.agent.sub_agents[0].session_messages[1].content,
        "Visible child output"
    );
}

#[test]
fn subagent_completion_filters_internal_system_messages_from_child_view() {
    let messages = vec![
        ava_types::Message::new(ava_types::Role::System, "internal scaffolding"),
        ava_types::Message::new(ava_types::Role::Assistant, "Visible result"),
    ];

    let ui_messages = session_messages_to_subagent_ui_messages(&messages);

    assert_eq!(ui_messages.len(), 1);
    assert!(matches!(ui_messages[0].kind, MessageKind::Assistant));
    assert_eq!(ui_messages[0].content, "Visible result");
}

#[test]
fn clicking_tool_group_inside_subagent_view_toggles_child_transcript_group() {
    let temp = tempdir().expect("tempdir");
    let db_path = temp.path().join("data.db");
    let mut app = App::test_new(&db_path);
    let (app_tx, _) = mpsc::unbounded_channel();
    let (agent_tx, _) = mpsc::unbounded_channel();

    app.state.agent.sub_agents.push(SubAgentInfo {
        call_id: "call-child-tools".to_string(),
        agent_type: Some("scout".to_string()),
        description: "Inspect files.".to_string(),
        background: false,
        is_running: false,
        tool_count: 1,
        current_tool: None,
        started_at: std::time::Instant::now(),
        elapsed: None,
        session_id: Some("child-session".to_string()),
        session_messages: vec![
            UiMessage::new(MessageKind::ToolCall, "glob {\"pattern\":\"src/**/*.rs\"}"),
            UiMessage::new(MessageKind::ToolResult, "src/main.rs"),
        ],
        provider: None,
        resumed: false,
        cost_usd: None,
        input_tokens: None,
        output_tokens: None,
    });
    app.state.view_mode = ViewMode::SubAgent {
        agent_index: 0,
        description: "Inspect files.".to_string(),
    };
    app.state.messages.messages_area = ratatui::layout::Rect::new(0, 0, 80, 10);
    app.state.messages.message_line_ranges = vec![(0, 1), (0, 1)];

    app.handle_event(
        AppEvent::Mouse(crossterm::event::MouseEvent {
            kind: crossterm::event::MouseEventKind::Down(crossterm::event::MouseButton::Left),
            column: 1,
            row: 0,
            modifiers: crossterm::event::KeyModifiers::NONE,
        }),
        app_tx,
        agent_tx,
    );

    let child_messages = &app.state.agent.sub_agents[0].session_messages;
    assert!(child_messages[0].tool_group_expanded);
    assert!(child_messages[1].tool_group_expanded);
}

#[test]
fn clicking_thinking_inside_subagent_view_toggles_child_transcript_message() {
    let temp = tempdir().expect("tempdir");
    let db_path = temp.path().join("data.db");
    let mut app = App::test_new(&db_path);
    let (app_tx, _) = mpsc::unbounded_channel();
    let (agent_tx, _) = mpsc::unbounded_channel();

    app.state.agent.sub_agents.push(SubAgentInfo {
        call_id: "call-child-thinking".to_string(),
        agent_type: Some("scout".to_string()),
        description: "Inspect files.".to_string(),
        background: false,
        is_running: false,
        tool_count: 0,
        current_tool: None,
        started_at: std::time::Instant::now(),
        elapsed: None,
        session_id: Some("child-session".to_string()),
        session_messages: vec![UiMessage::new(
            MessageKind::Thinking,
            "step 1\nstep 2\nstep 3",
        )],
        provider: None,
        resumed: false,
        cost_usd: None,
        input_tokens: None,
        output_tokens: None,
    });
    app.state.view_mode = ViewMode::SubAgent {
        agent_index: 0,
        description: "Inspect files.".to_string(),
    };
    app.state.messages.messages_area = ratatui::layout::Rect::new(0, 0, 80, 10);
    app.state.messages.message_line_ranges = vec![(0, 1)];

    app.handle_event(
        AppEvent::Mouse(crossterm::event::MouseEvent {
            kind: crossterm::event::MouseEventKind::Down(crossterm::event::MouseButton::Left),
            column: 1,
            row: 0,
            modifiers: crossterm::event::KeyModifiers::NONE,
        }),
        app_tx,
        agent_tx,
    );

    assert!(app.state.agent.sub_agents[0].session_messages[0].thinking_expanded);
}

#[test]
fn clicking_subagent_card_enters_matching_child_view_without_enter_shortcut() {
    let temp = tempdir().expect("tempdir");
    let db_path = temp.path().join("data.db");
    let mut app = App::test_new(&db_path);
    let (app_tx, _) = mpsc::unbounded_channel();
    let (agent_tx, _) = mpsc::unbounded_channel();

    app.state.agent.sub_agents.push(SubAgentInfo {
        call_id: "call-subagent-old".to_string(),
        agent_type: Some("reviewer".to_string()),
        description: "Review the patch.".to_string(),
        background: false,
        is_running: false,
        tool_count: 1,
        current_tool: None,
        started_at: std::time::Instant::now(),
        elapsed: None,
        session_id: Some("child-session-1".to_string()),
        session_messages: vec![UiMessage::new(MessageKind::Assistant, "Older run.")],
        provider: None,
        resumed: false,
        cost_usd: None,
        input_tokens: None,
        output_tokens: None,
    });

    app.state.agent.sub_agents.push(SubAgentInfo {
        call_id: "call-subagent-click".to_string(),
        agent_type: Some("reviewer".to_string()),
        description: "Review the patch.".to_string(),
        background: false,
        is_running: false,
        tool_count: 1,
        current_tool: None,
        started_at: std::time::Instant::now(),
        elapsed: None,
        session_id: Some("child-session-2".to_string()),
        session_messages: vec![UiMessage::new(MessageKind::Assistant, "Looks good.")],
        provider: None,
        resumed: false,
        cost_usd: None,
        input_tokens: None,
        output_tokens: None,
    });

    let mut subagent_message = UiMessage::new(MessageKind::SubAgent, "summary");
    subagent_message.sub_agent = Some(crate::state::messages::SubAgentData {
        agent_type: Some("reviewer".to_string()),
        description: "Review the patch.".to_string(),
        background: false,
        tool_count: 1,
        current_tool: None,
        duration: None,
        is_running: false,
        failed: false,
        call_id: "call-subagent-click".to_string(),
        session_id: Some("child-session-2".to_string()),
        session_messages: vec![UiMessage::new(MessageKind::Assistant, "Looks good.")],
        provider: None,
        resumed: false,
        cost_usd: None,
        input_tokens: None,
        output_tokens: None,
    });
    app.state.messages.push(subagent_message);
    app.state.messages.messages_area = ratatui::layout::Rect::new(0, 0, 80, 10);
    app.state.messages.message_line_ranges = vec![(0, 3)];

    let enter = crossterm::event::KeyEvent::new(
        crossterm::event::KeyCode::Enter,
        crossterm::event::KeyModifiers::NONE,
    );
    app.handle_event(AppEvent::Key(enter), app_tx.clone(), agent_tx.clone());
    assert!(matches!(app.state.view_mode, ViewMode::Main));

    app.handle_event(
        AppEvent::Mouse(crossterm::event::MouseEvent {
            kind: crossterm::event::MouseEventKind::Down(crossterm::event::MouseButton::Left),
            column: 1,
            row: 1,
            modifiers: crossterm::event::KeyModifiers::NONE,
        }),
        app_tx,
        agent_tx,
    );

    assert!(matches!(
        app.state.view_mode,
        ViewMode::SubAgent { agent_index: 1, .. }
    ));
}

#[test]
fn clicking_background_subagent_card_enters_matching_child_view() {
    let temp = tempdir().expect("tempdir");
    let db_path = temp.path().join("data.db");
    let mut app = App::test_new(&db_path);
    let (app_tx, _) = mpsc::unbounded_channel();
    let (agent_tx, _) = mpsc::unbounded_channel();

    app.state.agent.sub_agents.push(SubAgentInfo {
        call_id: "call-background-click".to_string(),
        agent_type: Some("scout".to_string()),
        description: "Inspect files.".to_string(),
        background: true,
        is_running: false,
        tool_count: 1,
        current_tool: None,
        started_at: std::time::Instant::now(),
        elapsed: None,
        session_id: Some("bg-child-session".to_string()),
        session_messages: vec![UiMessage::new(MessageKind::Assistant, "Finished.")],
        provider: None,
        resumed: false,
        cost_usd: None,
        input_tokens: None,
        output_tokens: None,
    });

    let task_id = {
        let mut bg = app.state.background.lock().unwrap();
        let task_id = bg.add_task("Background audit".to_string());
        let mut subagent_message = UiMessage::new(MessageKind::SubAgent, "summary");
        subagent_message.sub_agent = Some(crate::state::messages::SubAgentData {
            agent_type: Some("scout".to_string()),
            description: "Inspect files.".to_string(),
            background: true,
            tool_count: 1,
            current_tool: None,
            duration: None,
            is_running: false,
            failed: false,
            call_id: "call-background-click".to_string(),
            session_id: Some("bg-child-session".to_string()),
            session_messages: vec![UiMessage::new(MessageKind::Assistant, "Finished.")],
            provider: None,
            resumed: false,
            cost_usd: None,
            input_tokens: None,
            output_tokens: None,
        });
        bg.append_message(task_id, subagent_message);
        task_id
    };

    app.state.view_mode = ViewMode::BackgroundTask {
        task_id,
        goal: "Background audit".to_string(),
    };
    app.state.messages.messages_area = ratatui::layout::Rect::new(0, 0, 80, 10);
    app.state.messages.message_line_ranges = vec![(0, 3)];

    app.handle_event(
        AppEvent::Mouse(crossterm::event::MouseEvent {
            kind: crossterm::event::MouseEventKind::Down(crossterm::event::MouseButton::Left),
            column: 1,
            row: 1,
            modifiers: crossterm::event::KeyModifiers::NONE,
        }),
        app_tx,
        agent_tx,
    );

    assert!(matches!(
        app.state.view_mode,
        ViewMode::SubAgent { agent_index: 0, .. }
    ));
}

#[test]
fn clicking_subagent_card_without_session_id_matches_normalized_description() {
    let temp = tempdir().expect("tempdir");
    let db_path = temp.path().join("data.db");
    let mut app = App::test_new(&db_path);
    let (app_tx, _) = mpsc::unbounded_channel();
    let (agent_tx, _) = mpsc::unbounded_channel();

    app.state.agent.sub_agents.push(SubAgentInfo {
        call_id: "call-normalized".to_string(),
        agent_type: Some("scout".to_string()),
        description: "Inspect files.".to_string(),
        background: false,
        is_running: false,
        tool_count: 1,
        current_tool: None,
        started_at: std::time::Instant::now(),
        elapsed: None,
        session_id: None,
        session_messages: vec![UiMessage::new(MessageKind::Assistant, "Finished.")],
        provider: None,
        resumed: false,
        cost_usd: None,
        input_tokens: None,
        output_tokens: None,
    });

    let mut subagent_message = UiMessage::new(MessageKind::SubAgent, "summary");
    subagent_message.sub_agent = Some(crate::state::messages::SubAgentData {
        agent_type: Some("scout".to_string()),
        description: "[scout] Inspect files.".to_string(),
        background: false,
        tool_count: 1,
        current_tool: None,
        duration: None,
        is_running: false,
        failed: false,
        call_id: "call-normalized".to_string(),
        session_id: None,
        session_messages: vec![UiMessage::new(MessageKind::Assistant, "Finished.")],
        provider: None,
        resumed: false,
        cost_usd: None,
        input_tokens: None,
        output_tokens: None,
    });
    app.state.messages.push(subagent_message);
    app.state.messages.messages_area = ratatui::layout::Rect::new(0, 0, 80, 10);
    app.state.messages.message_line_ranges = vec![(0, 3)];

    app.handle_event(
        AppEvent::Mouse(crossterm::event::MouseEvent {
            kind: crossterm::event::MouseEventKind::Down(crossterm::event::MouseButton::Left),
            column: 1,
            row: 1,
            modifiers: crossterm::event::KeyModifiers::NONE,
        }),
        app_tx,
        agent_tx,
    );

    assert!(matches!(
        app.state.view_mode,
        ViewMode::SubAgent { agent_index: 0, .. }
    ));
}

#[test]
fn clicking_inflight_duplicate_subagent_card_uses_call_id() {
    let temp = tempdir().expect("tempdir");
    let db_path = temp.path().join("data.db");
    let mut app = App::test_new(&db_path);
    let (app_tx, _) = mpsc::unbounded_channel();
    let (agent_tx, _) = mpsc::unbounded_channel();

    for (call_id, text) in [("call-older", "Older run"), ("call-newer", "Newer run")] {
        app.state.agent.sub_agents.push(SubAgentInfo {
            call_id: call_id.to_string(),
            agent_type: Some("scout".to_string()),
            description: "Inspect files.".to_string(),
            background: false,
            is_running: true,
            tool_count: 0,
            current_tool: None,
            started_at: std::time::Instant::now(),
            elapsed: None,
            session_id: None,
            session_messages: vec![UiMessage::new(MessageKind::Assistant, text)],
            provider: None,
            resumed: false,
            cost_usd: None,
            input_tokens: None,
            output_tokens: None,
        });
    }

    let mut older_message = UiMessage::new(MessageKind::SubAgent, "summary");
    older_message.sub_agent = Some(crate::state::messages::SubAgentData {
        agent_type: Some("scout".to_string()),
        description: "Inspect files.".to_string(),
        background: false,
        tool_count: 0,
        current_tool: None,
        duration: None,
        is_running: true,
        failed: false,
        call_id: "call-older".to_string(),
        session_id: None,
        session_messages: vec![],
        provider: None,
        resumed: false,
        cost_usd: None,
        input_tokens: None,
        output_tokens: None,
    });
    app.state.messages.push(older_message);

    let mut newer_message = UiMessage::new(MessageKind::SubAgent, "summary");
    newer_message.sub_agent = Some(crate::state::messages::SubAgentData {
        agent_type: Some("scout".to_string()),
        description: "Inspect files.".to_string(),
        background: false,
        tool_count: 0,
        current_tool: None,
        duration: None,
        is_running: true,
        failed: false,
        call_id: "call-newer".to_string(),
        session_id: None,
        session_messages: vec![],
        provider: None,
        resumed: false,
        cost_usd: None,
        input_tokens: None,
        output_tokens: None,
    });
    app.state.messages.push(newer_message);

    app.state.messages.messages_area = ratatui::layout::Rect::new(0, 0, 80, 10);
    app.state.messages.message_line_ranges = vec![(0, 3), (3, 6)];

    app.handle_event(
        AppEvent::Mouse(crossterm::event::MouseEvent {
            kind: crossterm::event::MouseEventKind::Down(crossterm::event::MouseButton::Left),
            column: 1,
            row: 1,
            modifiers: crossterm::event::KeyModifiers::NONE,
        }),
        app_tx,
        agent_tx,
    );

    assert!(matches!(
        app.state.view_mode,
        ViewMode::SubAgent { agent_index: 0, .. }
    ));
}

#[test]
fn enter_subagent_view_allows_running_agent_without_completed_transcript() {
    let temp = tempdir().expect("tempdir");
    let db_path = temp.path().join("data.db");
    let mut app = App::test_new(&db_path);

    app.state.agent.sub_agents.push(SubAgentInfo {
        call_id: "call-running".to_string(),
        agent_type: Some("scout".to_string()),
        description: "Inspect files.".to_string(),
        background: false,
        is_running: true,
        tool_count: 0,
        current_tool: None,
        started_at: std::time::Instant::now(),
        elapsed: None,
        session_id: None,
        session_messages: vec![],
        provider: None,
        resumed: false,
        cost_usd: None,
        input_tokens: None,
        output_tokens: None,
    });

    assert!(app.enter_sub_agent_view(0));
    assert!(matches!(app.state.view_mode, ViewMode::SubAgent { .. }));
}

#[test]
fn enter_subagent_view_rejects_invalid_index() {
    let temp = tempdir().expect("tempdir");
    let db_path = temp.path().join("data.db");
    let mut app = App::test_new(&db_path);

    assert!(!app.enter_sub_agent_view(0));
    assert!(matches!(app.state.view_mode, ViewMode::Main));
}

#[test]
fn typing_is_ignored_while_viewing_subagent_transcript() {
    let temp = tempdir().expect("tempdir");
    let db_path = temp.path().join("data.db");
    let mut app = App::test_new(&db_path);
    let (app_tx, _) = mpsc::unbounded_channel();
    let (agent_tx, _) = mpsc::unbounded_channel();

    app.state.view_mode = ViewMode::SubAgent {
        agent_index: 0,
        description: "Inspect files.".to_string(),
    };

    app.handle_event(
        AppEvent::Key(crossterm::event::KeyEvent::new(
            crossterm::event::KeyCode::Char('x'),
            crossterm::event::KeyModifiers::NONE,
        )),
        app_tx,
        agent_tx,
    );

    assert!(app.state.input.buffer.is_empty());
}

#[test]
fn esc_from_transcript_view_returns_to_main_without_cancel_or_rewind_side_effects() {
    let temp = tempdir().expect("tempdir");
    let db_path = temp.path().join("data.db");
    let mut app = App::test_new(&db_path);
    let (app_tx, _) = mpsc::unbounded_channel();
    let (agent_tx, _) = mpsc::unbounded_channel();

    app.state.agent.is_running = true;
    app.state.view_mode = ViewMode::SubAgent {
        agent_index: 0,
        description: "Inspect files.".to_string(),
    };

    app.handle_event(
        AppEvent::Key(crossterm::event::KeyEvent::new(
            crossterm::event::KeyCode::Esc,
            crossterm::event::KeyModifiers::NONE,
        )),
        app_tx,
        agent_tx,
    );

    assert!(matches!(app.state.view_mode, ViewMode::Main));
    assert!(
        app.state.agent.is_running,
        "Esc from transcript view should not cancel active runs"
    );
    assert!(
        app.state.active_modal.is_none(),
        "Esc from transcript view should not open rewind or other modals"
    );
    assert!(
        app.last_esc_time.is_none(),
        "Esc from transcript view should not arm double-Esc rewind tracking"
    );
    assert!(
        !app.state
            .messages
            .messages
            .iter()
            .any(|msg| msg.content == "Session interrupted"),
        "Esc from transcript view should not emit cancellation system messages"
    );
}

#[test]
fn subagent_transcript_left_right_cycles_siblings_and_wraps() {
    let temp = tempdir().expect("tempdir");
    let db_path = temp.path().join("data.db");
    let mut app = App::test_new(&db_path);
    let (app_tx, _) = mpsc::unbounded_channel();
    let (agent_tx, _) = mpsc::unbounded_channel();

    for idx in 0..3 {
        app.state.agent.sub_agents.push(SubAgentInfo {
            call_id: format!("call-{idx}"),
            agent_type: Some("scout".to_string()),
            description: format!("Inspect files #{idx}"),
            background: false,
            is_running: idx == 1,
            tool_count: 0,
            current_tool: if idx == 1 {
                Some("glob".to_string())
            } else {
                None
            },
            started_at: std::time::Instant::now(),
            elapsed: None,
            session_id: None,
            session_messages: Vec::new(),
            provider: None,
            resumed: false,
            cost_usd: None,
            input_tokens: None,
            output_tokens: None,
        });
    }

    app.state.view_mode = ViewMode::SubAgent {
        agent_index: 0,
        description: "Inspect files #0".to_string(),
    };

    app.handle_event(
        AppEvent::Key(crossterm::event::KeyEvent::new(
            crossterm::event::KeyCode::Right,
            crossterm::event::KeyModifiers::NONE,
        )),
        app_tx.clone(),
        agent_tx.clone(),
    );
    assert!(matches!(
        app.state.view_mode,
        ViewMode::SubAgent { agent_index: 1, .. }
    ));

    app.handle_event(
        AppEvent::Key(crossterm::event::KeyEvent::new(
            crossterm::event::KeyCode::Left,
            crossterm::event::KeyModifiers::NONE,
        )),
        app_tx.clone(),
        agent_tx.clone(),
    );
    assert!(matches!(
        app.state.view_mode,
        ViewMode::SubAgent { agent_index: 0, .. }
    ));

    app.handle_event(
        AppEvent::Key(crossterm::event::KeyEvent::new(
            crossterm::event::KeyCode::Left,
            crossterm::event::KeyModifiers::NONE,
        )),
        app_tx,
        agent_tx,
    );
    assert!(matches!(
        app.state.view_mode,
        ViewMode::SubAgent { agent_index: 2, .. }
    ));
}

#[test]
fn transcript_view_blocks_mode_tab_shortcut() {
    let temp = tempdir().expect("tempdir");
    let db_path = temp.path().join("data.db");
    let mut app = App::test_new(&db_path);
    let (app_tx, _) = mpsc::unbounded_channel();
    let (agent_tx, _) = mpsc::unbounded_channel();

    app.state.agent.sub_agents.push(SubAgentInfo {
        call_id: "call-running".to_string(),
        agent_type: Some("scout".to_string()),
        description: "Inspect files.".to_string(),
        background: false,
        is_running: true,
        tool_count: 0,
        current_tool: Some("glob".to_string()),
        started_at: std::time::Instant::now(),
        elapsed: None,
        session_id: None,
        session_messages: Vec::new(),
        provider: None,
        resumed: false,
        cost_usd: None,
        input_tokens: None,
        output_tokens: None,
    });

    app.state.view_mode = ViewMode::SubAgent {
        agent_index: 0,
        description: "Inspect files.".to_string(),
    };

    let before_mode = app.state.agent_mode;
    app.handle_event(
        AppEvent::Key(crossterm::event::KeyEvent::new(
            crossterm::event::KeyCode::Tab,
            crossterm::event::KeyModifiers::NONE,
        )),
        app_tx,
        agent_tx,
    );

    assert_eq!(app.state.agent_mode, before_mode);
}

#[test]
fn background_task_view_blocks_mode_tab_shortcut() {
    let temp = tempdir().expect("tempdir");
    let db_path = temp.path().join("data.db");
    let mut app = App::test_new(&db_path);
    let (app_tx, _) = mpsc::unbounded_channel();
    let (agent_tx, _) = mpsc::unbounded_channel();

    app.state.view_mode = ViewMode::BackgroundTask {
        task_id: 7,
        goal: "Inspect logs".to_string(),
    };

    let before_mode = app.state.agent_mode;
    app.handle_event(
        AppEvent::Key(crossterm::event::KeyEvent::new(
            crossterm::event::KeyCode::Tab,
            crossterm::event::KeyModifiers::NONE,
        )),
        app_tx,
        agent_tx,
    );

    assert_eq!(app.state.agent_mode, before_mode);
}

#[test]
fn sidebar_subagent_click_target_opens_subagent_view() {
    let temp = tempdir().expect("tempdir");
    let db_path = temp.path().join("data.db");
    let mut app = App::test_new(&db_path);
    let (app_tx, _) = mpsc::unbounded_channel();
    let (agent_tx, _) = mpsc::unbounded_channel();

    app.state.agent.sub_agents.push(SubAgentInfo {
        call_id: "call-sidebar-open".to_string(),
        agent_type: Some("scout".to_string()),
        description: "Inspect files.".to_string(),
        background: false,
        is_running: true,
        tool_count: 0,
        current_tool: Some("glob".to_string()),
        started_at: std::time::Instant::now(),
        elapsed: None,
        session_id: None,
        session_messages: Vec::new(),
        provider: None,
        resumed: false,
        cost_usd: None,
        input_tokens: None,
        output_tokens: None,
    });

    app.state.sidebar_click_targets.push(SidebarClickTarget {
        x: 0..20,
        y: 0..1,
        action: SidebarClickAction::OpenSubAgent { index: 0 },
    });

    app.handle_event(
        AppEvent::Mouse(crossterm::event::MouseEvent {
            kind: crossterm::event::MouseEventKind::Down(crossterm::event::MouseButton::Left),
            column: 1,
            row: 0,
            modifiers: crossterm::event::KeyModifiers::NONE,
        }),
        app_tx,
        agent_tx,
    );

    assert!(matches!(
        app.state.view_mode,
        ViewMode::SubAgent { agent_index: 0, .. }
    ));
}

#[test]
fn subagent_completion_uses_call_id_before_duplicate_description() {
    let temp = tempdir().expect("tempdir");
    let db_path = temp.path().join("data.db");
    let mut app = App::test_new(&db_path);
    let (app_tx, _) = mpsc::unbounded_channel();
    let (agent_tx, _) = mpsc::unbounded_channel();
    app.state.agent.is_running = true;
    app.foreground_run_id = Some(7);

    for (call_id, summary) in [
        ("call-older", "Older result"),
        ("call-newer", "Newer result"),
    ] {
        app.handle_event(
            AppEvent::AgentRunEvent {
                run_id: 7,
                event: ava_agent::AgentEvent::ToolCall(ava_types::ToolCall {
                    id: call_id.to_string(),
                    name: "subagent".to_string(),
                    arguments: serde_json::json!({"prompt": "Inspect files.", "agent": "scout", "background": false}),
                }),
            },
            app_tx.clone(),
            agent_tx.clone(),
        );

        app.handle_event(
            AppEvent::AgentRunEvent {
                run_id: 7,
                event: ava_agent::AgentEvent::SubAgentComplete {
                    call_id: call_id.to_string(),
                    session_id: format!("session-{call_id}"),
                    messages: vec![ava_types::Message::new(ava_types::Role::Assistant, summary)],
                    description: "[scout] Inspect files.".to_string(),
                    input_tokens: 1,
                    output_tokens: 1,
                    cost_usd: 0.01,
                    agent_type: Some("scout".to_string()),
                    provider: Some("openai".to_string()),
                    resumed: false,
                },
            },
            app_tx.clone(),
            agent_tx.clone(),
        );
    }

    let first = &app.state.agent.sub_agents[0];
    let second = &app.state.agent.sub_agents[1];
    assert_eq!(first.call_id, "call-older");
    assert_eq!(first.session_id.as_deref(), Some("session-call-older"));
    assert_eq!(
        first.session_messages.last().map(|m| m.content.as_str()),
        Some("Older result")
    );
    assert_eq!(second.call_id, "call-newer");
    assert_eq!(second.session_id.as_deref(), Some("session-call-newer"));
    assert_eq!(
        second.session_messages.last().map(|m| m.content.as_str()),
        Some("Newer result")
    );
}

#[test]
fn subagent_completion_updates_older_duplicate_description_by_call_id() {
    let temp = tempdir().expect("tempdir");
    let db_path = temp.path().join("data.db");
    let mut app = App::test_new(&db_path);
    let (app_tx, _) = mpsc::unbounded_channel();
    let (agent_tx, _) = mpsc::unbounded_channel();

    app.state.agent.is_running = true;
    app.foreground_run_id = Some(7);

    for call_id in ["call-older", "call-newer"] {
        app.handle_event(
            AppEvent::AgentRunEvent {
                run_id: 7,
                event: ava_agent::AgentEvent::ToolCall(ava_types::ToolCall {
                    id: call_id.to_string(),
                    name: "subagent".to_string(),
                    arguments: serde_json::json!({"prompt": "Inspect files.", "agent": "scout", "background": false}),
                }),
            },
            app_tx.clone(),
            agent_tx.clone(),
        );
    }

    app.handle_event(
        AppEvent::AgentRunEvent {
            run_id: 7,
            event: ava_agent::AgentEvent::SubAgentComplete {
                call_id: "call-older".to_string(),
                session_id: "session-call-older".to_string(),
                messages: vec![ava_types::Message::new(
                    ava_types::Role::Assistant,
                    "Older result",
                )],
                description: "[scout] Inspect files.".to_string(),
                input_tokens: 1,
                output_tokens: 1,
                cost_usd: 0.01,
                agent_type: Some("scout".to_string()),
                provider: Some("openai".to_string()),
                resumed: false,
            },
        },
        app_tx,
        agent_tx,
    );

    let older = app
        .state
        .agent
        .sub_agents
        .iter()
        .find(|subagent| subagent.call_id == "call-older")
        .expect("older subagent should exist");
    let newer = app
        .state
        .agent
        .sub_agents
        .iter()
        .find(|subagent| subagent.call_id == "call-newer")
        .expect("newer subagent should exist");

    assert!(
        !older.is_running,
        "older matching call-id subagent should be marked completed"
    );
    assert_eq!(older.session_id.as_deref(), Some("session-call-older"));
    assert_eq!(
        older
            .session_messages
            .last()
            .map(|message| message.content.as_str()),
        Some("Older result")
    );
    assert!(
        newer.is_running,
        "newer duplicate-description subagent should remain running"
    );
    assert_eq!(newer.session_id, None);

    let older_card = app
        .state
        .messages
        .messages
        .iter()
        .filter(|message| matches!(message.kind, MessageKind::SubAgent))
        .find(|message| {
            message
                .sub_agent
                .as_ref()
                .is_some_and(|subagent| subagent.call_id == "call-older")
        })
        .expect("older subagent card should exist");
    let newer_card = app
        .state
        .messages
        .messages
        .iter()
        .filter(|message| matches!(message.kind, MessageKind::SubAgent))
        .find(|message| {
            message
                .sub_agent
                .as_ref()
                .is_some_and(|subagent| subagent.call_id == "call-newer")
        })
        .expect("newer subagent card should exist");

    let older_card_data = older_card.sub_agent.as_ref().expect("older card data");
    let newer_card_data = newer_card.sub_agent.as_ref().expect("newer card data");

    assert!(
        !older_card_data.is_running,
        "older card should be completed"
    );
    assert_eq!(
        older_card_data.session_id.as_deref(),
        Some("session-call-older")
    );
    assert!(
        newer_card_data.is_running,
        "newer card should still be running"
    );
    assert_eq!(newer_card_data.session_id, None);
}

#[test]
fn subagent_tool_result_updates_matching_call_id_not_latest_running_card() {
    let temp = tempdir().expect("tempdir");
    let db_path = temp.path().join("data.db");
    let mut app = App::test_new(&db_path);
    let (app_tx, _) = mpsc::unbounded_channel();
    let (agent_tx, _) = mpsc::unbounded_channel();

    app.state.agent.is_running = true;
    app.foreground_run_id = Some(7);

    for call_id in ["call-older", "call-newer"] {
        app.handle_event(
            AppEvent::AgentRunEvent {
                run_id: 7,
                event: ava_agent::AgentEvent::ToolCall(ava_types::ToolCall {
                    id: call_id.to_string(),
                    name: "subagent".to_string(),
                    arguments: serde_json::json!({"prompt": "Inspect files.", "agent": "scout", "background": false}),
                }),
            },
            app_tx.clone(),
            agent_tx.clone(),
        );
    }

    app.handle_event(
        AppEvent::AgentRunEvent {
            run_id: 7,
            event: ava_agent::AgentEvent::ToolResult(ava_types::ToolResult {
                call_id: "call-older".to_string(),
                content: "Older sub-agent completed".to_string(),
                is_error: false,
            }),
        },
        app_tx,
        agent_tx,
    );

    let older = app
        .state
        .agent
        .sub_agents
        .iter()
        .find(|subagent| subagent.call_id == "call-older")
        .expect("older subagent should exist");
    let newer = app
        .state
        .agent
        .sub_agents
        .iter()
        .find(|subagent| subagent.call_id == "call-newer")
        .expect("newer subagent should exist");

    assert!(
        !older.is_running,
        "matching call-id subagent should be marked completed"
    );
    assert!(
        newer.is_running,
        "newer running subagent should remain running"
    );
}

#[test]
fn new_session_from_subagent_view_resets_to_main_view() {
    let temp = tempdir().expect("tempdir");
    let db_path = temp.path().join("data.db");
    let mut app = App::test_new(&db_path);

    app.state.agent.sub_agents.push(SubAgentInfo {
        call_id: "call-running".to_string(),
        agent_type: Some("scout".to_string()),
        description: "Inspect files.".to_string(),
        background: false,
        is_running: true,
        tool_count: 0,
        current_tool: Some("glob".to_string()),
        started_at: std::time::Instant::now(),
        elapsed: None,
        session_id: None,
        session_messages: vec![UiMessage::new(MessageKind::Thinking, "Running...")],
        provider: None,
        resumed: false,
        cost_usd: None,
        input_tokens: None,
        output_tokens: None,
    });
    app.state.view_mode = ViewMode::SubAgent {
        agent_index: 0,
        description: "Inspect files.".to_string(),
    };

    app.execute_command_action(Action::NewSession, None);

    assert!(matches!(app.state.view_mode, ViewMode::Main));
    assert!(app.state.agent.sub_agents.is_empty());
    assert!(app.state.messages.messages.is_empty());
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

#[tokio::test]
async fn question_requests_use_shared_timeout_and_clear_lifecycle() {
    let temp = tempdir().expect("tempdir");
    let db_path = temp.path().join("data.db");
    let mut app = App::test_new(&db_path);
    app.set_interactive_timeout_for_test(Duration::from_millis(5));
    app.foreground_run_id = Some(77);

    let (app_tx, mut app_rx) = mpsc::unbounded_channel();
    let (agent_tx, _) = mpsc::unbounded_channel();
    let (reply_tx, reply_rx) = oneshot::channel();

    app.receive_question_request(
        ava_tools::core::question::QuestionRequest {
            run_id: Some("77".to_string()),
            question: "Continue?".to_string(),
            options: Vec::new(),
            reply: reply_tx,
        },
        app_tx.clone(),
    )
    .await;

    let request_id = app
        .state
        .question
        .as_ref()
        .expect("pending question")
        .request_id
        .clone();
    assert!(request_id.starts_with("question-"));

    let cleared = tokio::time::timeout(Duration::from_millis(100), app_rx.recv())
        .await
        .expect("question timeout event")
        .expect("clear event");

    match &cleared {
        AppEvent::InteractiveRequestCleared {
            request_id: cleared_id,
            request_kind,
            timed_out,
            run_id,
        } => {
            assert_eq!(cleared_id, &request_id);
            assert_eq!(*request_kind, InteractiveRequestKind::Question);
            assert!(*timed_out);
            assert_eq!(run_id.as_deref(), Some("77"));
        }
        other => panic!("unexpected event: {other:?}"),
    }

    app.handle_event(cleared, app_tx, agent_tx);

    assert!(app.state.question.is_none());
    assert_eq!(
        app.state
            .status_message
            .as_ref()
            .map(|message| message.text.as_str()),
        Some("Question timed out")
    );
    assert_eq!(reply_rx.await.expect("timeout answer"), "");
}

#[tokio::test]
async fn question_resolution_uses_request_id_and_shared_clear_event() {
    let temp = tempdir().expect("tempdir");
    let db_path = temp.path().join("data.db");
    let mut app = App::test_new(&db_path);
    app.foreground_run_id = Some(12);

    let (app_tx, mut app_rx) = mpsc::unbounded_channel();
    let (agent_tx, _) = mpsc::unbounded_channel();
    let (reply_tx, reply_rx) = oneshot::channel();

    app.receive_question_request(
        ava_tools::core::question::QuestionRequest {
            run_id: Some("12".to_string()),
            question: "Language?".to_string(),
            options: Vec::new(),
            reply: reply_tx,
        },
        app_tx.clone(),
    )
    .await;

    let request_id = app
        .state
        .question
        .as_ref()
        .expect("pending question")
        .request_id
        .clone();
    app.state.question.as_mut().expect("question state").input = "Rust".to_string();

    app.handle_question_key(
        crossterm::event::KeyEvent::from(KeyCode::Enter),
        app_tx.clone(),
    );

    let cleared = tokio::time::timeout(Duration::from_millis(100), app_rx.recv())
        .await
        .expect("question resolve event")
        .expect("clear event");

    match &cleared {
        AppEvent::InteractiveRequestCleared {
            request_id: cleared_id,
            request_kind,
            timed_out,
            run_id,
        } => {
            assert_eq!(cleared_id, &request_id);
            assert_eq!(*request_kind, InteractiveRequestKind::Question);
            assert!(!timed_out);
            assert_eq!(run_id.as_deref(), Some("12"));
        }
        other => panic!("unexpected event: {other:?}"),
    }

    app.handle_event(cleared, app_tx, agent_tx);

    assert!(app.state.question.is_none());
    assert_eq!(reply_rx.await.expect("question answer"), "Rust");
}

#[tokio::test]
async fn plan_resolution_uses_request_id_and_shared_clear_event() {
    let temp = tempdir().expect("tempdir");
    let db_path = temp.path().join("data.db");
    let mut app = App::test_new(&db_path);
    app.foreground_run_id = Some(18);

    let (app_tx, mut app_rx) = mpsc::unbounded_channel();
    let (agent_tx, _) = mpsc::unbounded_channel();
    let (reply_tx, reply_rx) = oneshot::channel();

    app.receive_plan_request(
        ava_tools::core::plan::PlanRequest {
            run_id: Some("18".to_string()),
            plan: sample_plan("Normalize plan lifecycle events"),
            reply: reply_tx,
        },
        app_tx.clone(),
    )
    .await;

    let request_id = app
        .state
        .plan_approval
        .as_ref()
        .expect("pending plan")
        .request_id
        .clone();
    assert!(request_id.starts_with("plan-"));
    assert_eq!(app.state.active_modal, Some(ModalType::PlanApproval));

    app.handle_plan_approval_key(
        crossterm::event::KeyEvent::from(KeyCode::Char('e')),
        app_tx.clone(),
    );

    let cleared = recv_interactive_cleared(&mut app_rx).await;

    match &cleared {
        AppEvent::InteractiveRequestCleared {
            request_id: cleared_id,
            request_kind,
            timed_out,
            run_id,
        } => {
            assert_eq!(cleared_id, &request_id);
            assert_eq!(*request_kind, InteractiveRequestKind::Plan);
            assert!(!timed_out);
            assert_eq!(run_id.as_deref(), Some("18"));
        }
        other => panic!("unexpected event: {other:?}"),
    }

    assert!(app.state.plan_approval.is_none());
    assert_eq!(app.state.active_modal, None);
    assert_eq!(
        app.state
            .status_message
            .as_ref()
            .map(|message| message.text.as_str()),
        Some("Plan approved — executing")
    );

    app.handle_event(cleared, app_tx, agent_tx);

    assert_eq!(
        reply_rx.await.expect("plan decision"),
        ava_types::PlanDecision::Approved
    );
}

#[tokio::test]
async fn plan_requests_use_shared_timeout_and_clear_lifecycle() {
    let temp = tempdir().expect("tempdir");
    let db_path = temp.path().join("data.db");
    let mut app = App::test_new(&db_path);
    app.set_interactive_timeout_for_test(Duration::from_millis(5));
    app.foreground_run_id = Some(33);

    let (app_tx, mut app_rx) = mpsc::unbounded_channel();
    let (agent_tx, _) = mpsc::unbounded_channel();
    let (reply_tx, reply_rx) = oneshot::channel();

    app.receive_plan_request(
        ava_tools::core::plan::PlanRequest {
            run_id: Some("33".to_string()),
            plan: sample_plan("Timeout plan approval cleanup"),
            reply: reply_tx,
        },
        app_tx.clone(),
    )
    .await;

    let request_id = app
        .state
        .plan_approval
        .as_ref()
        .expect("pending plan")
        .request_id
        .clone();

    let cleared = recv_interactive_cleared(&mut app_rx).await;

    match &cleared {
        AppEvent::InteractiveRequestCleared {
            request_id: cleared_id,
            request_kind,
            timed_out,
            run_id,
        } => {
            assert_eq!(cleared_id, &request_id);
            assert_eq!(*request_kind, InteractiveRequestKind::Plan);
            assert!(*timed_out);
            assert_eq!(run_id.as_deref(), Some("33"));
        }
        other => panic!("unexpected event: {other:?}"),
    }

    app.handle_event(cleared, app_tx, agent_tx);

    assert!(app.state.plan_approval.is_none());
    assert_eq!(app.state.active_modal, None);
    assert_eq!(
        app.state
            .status_message
            .as_ref()
            .map(|message| message.text.as_str()),
        Some("Plan approval timed out")
    );
    assert_eq!(
        reply_rx.await.expect("timeout decision"),
        ava_types::PlanDecision::Rejected {
            feedback: "Timed out waiting for plan response in TUI".to_string(),
        }
    );
}

#[test]
fn foreground_required_control_plane_events_are_visible_in_tui() {
    let temp = tempdir().expect("tempdir");
    let db_path = temp.path().join("data.db");
    let mut app = App::test_new(&db_path);
    let (app_tx, _) = mpsc::unbounded_channel();
    let (agent_tx, _) = mpsc::unbounded_channel();

    let required: std::collections::HashSet<_> =
        required_backend_event_kinds().iter().copied().collect();
    assert!(required.contains(&CanonicalEventKind::PlanStepComplete));
    assert!(required.contains(&CanonicalEventKind::StreamingEditProgress));
    assert!(required.contains(&CanonicalEventKind::Complete));
    assert!(required.contains(&CanonicalEventKind::Error));
    assert!(required.contains(&CanonicalEventKind::SubagentComplete));

    app.state.agent.is_running = true;
    app.foreground_run_id = Some(5);
    app.state
        .input
        .queue_display
        .push("queued".to_string(), ava_types::MessageTier::Steering);

    app.handle_event(
        AppEvent::AgentRunEvent {
            run_id: 5,
            event: ava_agent::AgentEvent::ToolCall(ava_types::ToolCall {
                id: "call_subagent_required".to_string(),
                name: "subagent".to_string(),
                arguments: serde_json::json!({"prompt": "Check docs", "background": true}),
            }),
        },
        app_tx.clone(),
        agent_tx.clone(),
    );

    app.handle_event(
        AppEvent::AgentRunEvent {
            run_id: 5,
            event: ava_agent::AgentEvent::PlanStepComplete {
                step_id: "step-1".to_string(),
            },
        },
        app_tx.clone(),
        agent_tx.clone(),
    );
    assert_eq!(
        app.state
            .status_message
            .as_ref()
            .map(|message| message.text.as_str()),
        Some("Plan step completed: step-1")
    );

    app.handle_event(
        AppEvent::AgentRunEvent {
            run_id: 5,
            event: ava_agent::AgentEvent::StreamingEditProgress {
                call_id: "call-edit-1".to_string(),
                tool_name: "apply_patch".to_string(),
                file_path: Some("src/main.rs".to_string()),
                bytes_received: 256,
            },
        },
        app_tx.clone(),
        agent_tx.clone(),
    );
    assert_eq!(
        app.state
            .status_message
            .as_ref()
            .map(|message| message.text.as_str()),
        Some("apply_patch src/main.rs... (256 bytes)")
    );

    app.handle_event(
        AppEvent::AgentRunEvent {
            run_id: 5,
            event: ava_agent::AgentEvent::SubAgentComplete {
                call_id: String::new(),
                session_id: "required-sub-session".to_string(),
                messages: vec![ava_types::Message::new(ava_types::Role::Assistant, "done")],
                description: "Check docs".to_string(),
                input_tokens: 4,
                output_tokens: 3,
                cost_usd: 0.01,
                agent_type: Some("reviewer".to_string()),
                provider: Some("openai".to_string()),
                resumed: false,
            },
        },
        app_tx.clone(),
        agent_tx.clone(),
    );
    let subagent = app
        .state
        .agent
        .sub_agents
        .iter()
        .find(|subagent| subagent.description == "Check docs")
        .expect("subagent state");
    assert_eq!(subagent.session_id.as_deref(), Some("required-sub-session"));

    app.handle_event(
        AppEvent::AgentRunEvent {
            run_id: 5,
            event: ava_agent::AgentEvent::Error("boom".to_string()),
        },
        app_tx.clone(),
        agent_tx.clone(),
    );
    let last = app.state.messages.messages.last().expect("error message");
    assert_eq!(last.kind, MessageKind::Error);
    assert_eq!(last.content, "boom");

    app.state.agent.is_running = true;
    app.handle_event(
        AppEvent::AgentRunEvent {
            run_id: 5,
            event: ava_agent::AgentEvent::Complete(ava_types::Session::new()),
        },
        app_tx,
        agent_tx,
    );
    assert!(!app.state.agent.is_running);
    assert_eq!(
        app.state.agent.activity,
        crate::state::agent::AgentActivity::Idle
    );
    assert!(app.state.input.queue_display.is_empty());
}

#[test]
fn background_required_control_plane_events_are_not_silently_dropped() {
    let temp = tempdir().expect("tempdir");
    let db_path = temp.path().join("data.db");
    let mut app = App::test_new(&db_path);
    let (app_tx, _) = mpsc::unbounded_channel();
    let (agent_tx, _) = mpsc::unbounded_channel();

    app.state.agent.is_running = true;
    app.foreground_run_id = Some(42);
    app.state
        .messages
        .push(UiMessage::new(MessageKind::User, "background me"));
    app.background_current_agent(app_tx.clone());

    app.handle_event(
        AppEvent::AgentRunEvent {
            run_id: 42,
            event: ava_agent::AgentEvent::PlanStepComplete {
                step_id: "bg-step".to_string(),
            },
        },
        app_tx.clone(),
        agent_tx.clone(),
    );
    app.handle_event(
        AppEvent::AgentRunEvent {
            run_id: 42,
            event: ava_agent::AgentEvent::StreamingEditProgress {
                call_id: "bg-call".to_string(),
                tool_name: "write".to_string(),
                file_path: Some("src/lib.rs".to_string()),
                bytes_received: 64,
            },
        },
        app_tx.clone(),
        agent_tx.clone(),
    );
    app.handle_event(
        AppEvent::AgentRunEvent {
            run_id: 42,
            event: ava_agent::AgentEvent::Complete(ava_types::Session::new()),
        },
        app_tx,
        agent_tx,
    );

    let bg = app.state.background.lock().unwrap();
    let task = bg.tasks.iter().find(|task| task.id == 1).expect("task");
    let contents: Vec<_> = task
        .messages
        .iter()
        .map(|message| message.content.as_str())
        .collect();
    assert!(contents.contains(&"Plan step completed: bg-step"));
    assert!(contents.contains(&"write src/lib.rs... (64 bytes)"));
    assert!(contents.contains(&"Run complete"));
}

#[tokio::test]
async fn cancelling_tui_run_clears_pending_approval_via_shared_lifecycle() {
    let temp = tempdir().expect("tempdir");
    let db_path = temp.path().join("data.db");
    let mut app = App::test_new(&db_path);
    app.foreground_run_id = Some(9);

    let (app_tx, mut app_rx) = mpsc::unbounded_channel();
    let (agent_tx, _) = mpsc::unbounded_channel();
    let (reply_tx, reply_rx) = oneshot::channel();

    app.receive_tool_approval_request(
        ava_tools::permission_middleware::ApprovalRequest {
            run_id: Some("9".to_string()),
            call: ava_types::ToolCall {
                id: "call-approve".to_string(),
                name: "bash".to_string(),
                arguments: serde_json::json!({"command": "cargo test"}),
            },
            inspection: ava_permissions::inspector::InspectionResult {
                action: ava_permissions::Action::Ask,
                reason: "needs approval".to_string(),
                risk_level: RiskLevel::High,
                tags: Vec::new(),
                warnings: vec!["danger".to_string()],
            },
            reply: reply_tx,
        },
        app_tx.clone(),
    )
    .await;

    let request_id = app
        .state
        .permission
        .queue
        .front()
        .expect("pending approval")
        .request_id
        .clone();

    app.cancel_pending_interactive_requests(
        app_tx.clone(),
        "Agent run cancelled from TUI",
        Some("9".to_string()),
    );

    let cleared = tokio::time::timeout(Duration::from_millis(100), app_rx.recv())
        .await
        .expect("approval cancel event")
        .expect("clear event");

    match &cleared {
        AppEvent::InteractiveRequestCleared {
            request_id: cleared_id,
            request_kind,
            timed_out,
            run_id,
        } => {
            assert_eq!(cleared_id, &request_id);
            assert_eq!(*request_kind, InteractiveRequestKind::Approval);
            assert!(!timed_out);
            assert_eq!(run_id.as_deref(), Some("9"));
        }
        other => panic!("unexpected event: {other:?}"),
    }

    app.handle_event(cleared, app_tx, agent_tx);

    assert!(app.state.permission.queue.is_empty());
    match reply_rx.await.expect("approval answer") {
        ava_tools::permission_middleware::ToolApproval::Rejected(Some(reason)) => {
            assert_eq!(reason, "Agent run cancelled from TUI");
        }
        other => panic!("unexpected approval result: {other:?}"),
    }
}

#[tokio::test]
async fn background_run_approval_survives_foreground_switch_until_origin_run_is_cancelled() {
    let temp = tempdir().expect("tempdir");
    let db_path = temp.path().join("data.db");
    let mut app = App::test_new(&db_path);
    app.foreground_run_id = Some(200);

    let (app_tx, mut app_rx) = mpsc::unbounded_channel();
    let (agent_tx, _) = mpsc::unbounded_channel();
    let (reply_tx, mut reply_rx) = oneshot::channel();

    app.receive_tool_approval_request(
        ava_tools::permission_middleware::ApprovalRequest {
            run_id: Some("41".to_string()),
            call: ava_types::ToolCall {
                id: "call-bg-approve".to_string(),
                name: "bash".to_string(),
                arguments: serde_json::json!({"command": "cargo test -p ava-tui"}),
            },
            inspection: ava_permissions::inspector::InspectionResult {
                action: ava_permissions::Action::Ask,
                reason: "background approval".to_string(),
                risk_level: RiskLevel::High,
                tags: Vec::new(),
                warnings: vec!["danger".to_string()],
            },
            reply: reply_tx,
        },
        app_tx.clone(),
    )
    .await;

    let request = app
        .state
        .permission
        .queue
        .front()
        .expect("pending approval");
    let request_id = request.request_id.clone();
    assert_eq!(request.run_id.as_deref(), Some("41"));

    app.cancel_pending_interactive_requests(
        app_tx.clone(),
        "Agent run cancelled from TUI",
        Some("200".to_string()),
    );

    tokio::time::sleep(Duration::from_millis(20)).await;
    assert!(matches!(
        app_rx.try_recv(),
        Err(tokio::sync::mpsc::error::TryRecvError::Empty)
    ));
    assert!(matches!(
        reply_rx.try_recv(),
        Err(tokio::sync::oneshot::error::TryRecvError::Empty)
    ));
    assert_eq!(app.state.permission.queue.len(), 1);

    app.cancel_pending_interactive_requests(
        app_tx.clone(),
        "Agent run cancelled from TUI",
        Some("41".to_string()),
    );

    let cleared = recv_interactive_cleared(&mut app_rx).await;
    match &cleared {
        AppEvent::InteractiveRequestCleared {
            request_id: cleared_id,
            request_kind,
            timed_out,
            run_id,
        } => {
            assert_eq!(cleared_id, &request_id);
            assert_eq!(*request_kind, InteractiveRequestKind::Approval);
            assert!(!timed_out);
            assert_eq!(run_id.as_deref(), Some("41"));
        }
        other => panic!("unexpected event: {other:?}"),
    }

    app.handle_event(cleared, app_tx, agent_tx);

    assert!(app.state.permission.queue.is_empty());
    match reply_rx.await.expect("approval answer") {
        ava_tools::permission_middleware::ToolApproval::Rejected(Some(reason)) => {
            assert_eq!(reason, "Agent run cancelled from TUI");
        }
        other => panic!("unexpected approval result: {other:?}"),
    }
}

#[tokio::test]
async fn queued_tool_approvals_preserve_replies_and_resolve_in_order() {
    let temp = tempdir().expect("tempdir");
    let db_path = temp.path().join("data.db");
    let mut app = App::test_new(&db_path);
    app.foreground_run_id = Some(11);

    let (app_tx, mut app_rx) = mpsc::unbounded_channel();
    let (agent_tx, _) = mpsc::unbounded_channel();
    let (first_reply_tx, first_reply_rx) = oneshot::channel();
    let (second_reply_tx, second_reply_rx) = oneshot::channel();

    app.receive_tool_approval_request(
        ava_tools::permission_middleware::ApprovalRequest {
            run_id: Some("11".to_string()),
            call: ava_types::ToolCall {
                id: "call-approve-1".to_string(),
                name: "bash".to_string(),
                arguments: serde_json::json!({"command": "cargo test -p ava-tui"}),
            },
            inspection: ava_permissions::inspector::InspectionResult {
                action: ava_permissions::Action::Ask,
                reason: "needs first approval".to_string(),
                risk_level: RiskLevel::High,
                tags: Vec::new(),
                warnings: vec!["danger".to_string()],
            },
            reply: first_reply_tx,
        },
        app_tx.clone(),
    )
    .await;

    app.receive_tool_approval_request(
        ava_tools::permission_middleware::ApprovalRequest {
            run_id: Some("11".to_string()),
            call: ava_types::ToolCall {
                id: "call-approve-2".to_string(),
                name: "bash".to_string(),
                arguments: serde_json::json!({"command": "cargo test -p ava-agent"}),
            },
            inspection: ava_permissions::inspector::InspectionResult {
                action: ava_permissions::Action::Ask,
                reason: "needs second approval".to_string(),
                risk_level: RiskLevel::High,
                tags: Vec::new(),
                warnings: vec!["danger".to_string()],
            },
            reply: second_reply_tx,
        },
        app_tx.clone(),
    )
    .await;

    assert_eq!(app.state.permission.queue.len(), 2);

    let first = app
        .state
        .permission
        .approve_current_once()
        .expect("first queued approval");
    let first_request_id = first.request_id.clone();
    let second_request_id = app
        .state
        .permission
        .queue
        .front()
        .expect("second queued approval still present")
        .request_id
        .clone();

    app.resolve_tool_approval_request(
        first.request_id,
        first.run_id,
        ava_tools::permission_middleware::ToolApproval::Allowed,
        app_tx.clone(),
    );

    let first_cleared = recv_interactive_cleared(&mut app_rx).await;
    match &first_cleared {
        AppEvent::InteractiveRequestCleared {
            request_id,
            request_kind,
            timed_out,
            run_id,
        } => {
            assert_eq!(request_id, &first_request_id);
            assert_eq!(*request_kind, InteractiveRequestKind::Approval);
            assert!(!timed_out);
            assert_eq!(run_id.as_deref(), Some("11"));
        }
        other => panic!("unexpected event: {other:?}"),
    }
    app.handle_event(first_cleared, app_tx.clone(), agent_tx.clone());

    assert_eq!(app.state.permission.queue.len(), 1);
    assert_eq!(
        app.state
            .permission
            .queue
            .front()
            .map(|request| request.request_id.as_str()),
        Some(second_request_id.as_str())
    );

    match first_reply_rx.await.expect("first approval answer") {
        ava_tools::permission_middleware::ToolApproval::Allowed => {}
        other => panic!("unexpected first approval result: {other:?}"),
    }

    let second = app
        .state
        .permission
        .approve_current_once()
        .expect("second queued approval");
    app.resolve_tool_approval_request(
        second.request_id,
        second.run_id,
        ava_tools::permission_middleware::ToolApproval::AllowedForSession,
        app_tx.clone(),
    );

    let second_cleared = recv_interactive_cleared(&mut app_rx).await;
    match &second_cleared {
        AppEvent::InteractiveRequestCleared {
            request_id,
            request_kind,
            timed_out,
            run_id,
        } => {
            assert_eq!(request_id, &second_request_id);
            assert_eq!(*request_kind, InteractiveRequestKind::Approval);
            assert!(!timed_out);
            assert_eq!(run_id.as_deref(), Some("11"));
        }
        other => panic!("unexpected event: {other:?}"),
    }
    app.handle_event(second_cleared, app_tx, agent_tx);

    assert!(app.state.permission.queue.is_empty());
    match second_reply_rx.await.expect("second approval answer") {
        ava_tools::permission_middleware::ToolApproval::AllowedForSession => {}
        other => panic!("unexpected second approval result: {other:?}"),
    }
}

#[tokio::test]
async fn queued_tool_approval_timeout_only_arms_visible_request_and_resets_modal_state() {
    let temp = tempdir().expect("tempdir");
    let db_path = temp.path().join("data.db");
    let mut app = App::test_new(&db_path);
    app.set_interactive_timeout_for_test(Duration::from_millis(10));
    app.foreground_run_id = Some(15);

    let (app_tx, mut app_rx) = mpsc::unbounded_channel();
    let (agent_tx, _) = mpsc::unbounded_channel();
    let (first_reply_tx, first_reply_rx) = oneshot::channel();
    let (second_reply_tx, mut second_reply_rx) = oneshot::channel();

    app.receive_tool_approval_request(
        ava_tools::permission_middleware::ApprovalRequest {
            run_id: Some("15".to_string()),
            call: ava_types::ToolCall {
                id: "call-timeout-1".to_string(),
                name: "bash".to_string(),
                arguments: serde_json::json!({"command": "cargo test -p ava-tui"}),
            },
            inspection: ava_permissions::inspector::InspectionResult {
                action: ava_permissions::Action::Ask,
                reason: "needs first approval".to_string(),
                risk_level: RiskLevel::High,
                tags: Vec::new(),
                warnings: vec!["danger".to_string()],
            },
            reply: first_reply_tx,
        },
        app_tx.clone(),
    )
    .await;

    app.receive_tool_approval_request(
        ava_tools::permission_middleware::ApprovalRequest {
            run_id: Some("15".to_string()),
            call: ava_types::ToolCall {
                id: "call-timeout-2".to_string(),
                name: "bash".to_string(),
                arguments: serde_json::json!({"command": "cargo test -p ava-agent"}),
            },
            inspection: ava_permissions::inspector::InspectionResult {
                action: ava_permissions::Action::Ask,
                reason: "needs second approval".to_string(),
                risk_level: RiskLevel::High,
                tags: Vec::new(),
                warnings: vec!["danger".to_string()],
            },
            reply: second_reply_tx,
        },
        app_tx.clone(),
    )
    .await;

    let first_request_id = app
        .state
        .permission
        .queue
        .front()
        .expect("first approval")
        .request_id
        .clone();
    let second_request_id = app
        .state
        .permission
        .queue
        .back()
        .expect("second approval")
        .request_id
        .clone();

    app.state.permission.current_stage = crate::state::permission::ApprovalStage::RejectionReason;
    app.state.permission.rejection_input = "stale rejection".to_string();

    let first_cleared = recv_interactive_cleared(&mut app_rx).await;
    match &first_cleared {
        AppEvent::InteractiveRequestCleared {
            request_id,
            request_kind,
            timed_out,
            run_id,
        } => {
            assert_eq!(request_id, &first_request_id);
            assert_eq!(*request_kind, InteractiveRequestKind::Approval);
            assert!(*timed_out);
            assert_eq!(run_id.as_deref(), Some("15"));
        }
        other => panic!("unexpected event: {other:?}"),
    }

    tokio::time::sleep(Duration::from_millis(20)).await;
    assert!(matches!(
        app_rx.try_recv(),
        Err(tokio::sync::mpsc::error::TryRecvError::Empty)
    ));
    assert!(matches!(
        second_reply_rx.try_recv(),
        Err(tokio::sync::oneshot::error::TryRecvError::Empty)
    ));

    app.handle_event(first_cleared, app_tx.clone(), agent_tx.clone());

    assert_eq!(app.state.permission.queue.len(), 1);
    assert_eq!(
        app.state
            .permission
            .queue
            .front()
            .map(|request| request.request_id.as_str()),
        Some(second_request_id.as_str())
    );
    assert_eq!(
        app.state.permission.current_stage,
        crate::state::permission::ApprovalStage::Preview
    );
    assert!(app.state.permission.rejection_input.is_empty());

    let second_cleared = recv_interactive_cleared(&mut app_rx).await;
    match &second_cleared {
        AppEvent::InteractiveRequestCleared {
            request_id,
            request_kind,
            timed_out,
            run_id,
        } => {
            assert_eq!(request_id, &second_request_id);
            assert_eq!(*request_kind, InteractiveRequestKind::Approval);
            assert!(*timed_out);
            assert_eq!(run_id.as_deref(), Some("15"));
        }
        other => panic!("unexpected event: {other:?}"),
    }

    app.handle_event(second_cleared, app_tx, agent_tx);

    assert!(app.state.permission.queue.is_empty());
    match first_reply_rx.await.expect("first approval answer") {
        ava_tools::permission_middleware::ToolApproval::Rejected(Some(reason)) => {
            assert_eq!(reason, "Timed out waiting for user approval in TUI");
        }
        other => panic!("unexpected first approval result: {other:?}"),
    }
    match second_reply_rx.await.expect("second approval answer") {
        ava_tools::permission_middleware::ToolApproval::Rejected(Some(reason)) => {
            assert_eq!(reason, "Timed out waiting for user approval in TUI");
        }
        other => panic!("unexpected second approval result: {other:?}"),
    }
}

#[tokio::test]
async fn question_requests_supersede_existing_pending_sender() {
    let temp = tempdir().expect("tempdir");
    let db_path = temp.path().join("data.db");
    let mut app = App::test_new(&db_path);
    app.foreground_run_id = Some(55);

    let (app_tx, mut app_rx) = mpsc::unbounded_channel();
    let (agent_tx, _) = mpsc::unbounded_channel();
    let (first_reply_tx, first_reply_rx) = oneshot::channel();
    let (second_reply_tx, second_reply_rx) = oneshot::channel();

    app.receive_question_request(
        ava_tools::core::question::QuestionRequest {
            run_id: Some("55".to_string()),
            question: "First question?".to_string(),
            options: Vec::new(),
            reply: first_reply_tx,
        },
        app_tx.clone(),
    )
    .await;

    let first_request_id = app
        .state
        .question
        .as_ref()
        .expect("first question")
        .request_id
        .clone();

    app.receive_question_request(
        ava_tools::core::question::QuestionRequest {
            run_id: Some("55".to_string()),
            question: "Second question?".to_string(),
            options: Vec::new(),
            reply: second_reply_tx,
        },
        app_tx.clone(),
    )
    .await;

    let cleared = recv_interactive_cleared(&mut app_rx).await;
    match &cleared {
        AppEvent::InteractiveRequestCleared {
            request_id,
            request_kind,
            timed_out,
            run_id,
        } => {
            assert_eq!(request_id, &first_request_id);
            assert_eq!(*request_kind, InteractiveRequestKind::Question);
            assert!(!timed_out);
            assert_eq!(run_id.as_deref(), Some("55"));
        }
        other => panic!("unexpected event: {other:?}"),
    }

    app.handle_event(cleared, app_tx.clone(), agent_tx.clone());

    let second_request_id = app
        .state
        .question
        .as_ref()
        .expect("second question")
        .request_id
        .clone();
    assert_ne!(second_request_id, first_request_id);
    assert_eq!(
        app.state
            .question
            .as_ref()
            .map(|question| question.question.as_str()),
        Some("Second question?")
    );

    app.state.question.as_mut().expect("question state").input = "Second answer".to_string();
    app.handle_question_key(
        crossterm::event::KeyEvent::from(KeyCode::Enter),
        app_tx.clone(),
    );

    let resolved = recv_interactive_cleared(&mut app_rx).await;
    app.handle_event(resolved, app_tx, agent_tx);

    assert_eq!(first_reply_rx.await.expect("first answer"), "");
    assert_eq!(
        second_reply_rx.await.expect("second answer"),
        "Second answer"
    );
}

#[tokio::test]
async fn question_supersession_and_cancellation_are_scoped_to_origin_run() {
    let temp = tempdir().expect("tempdir");
    let db_path = temp.path().join("data.db");
    let mut app = App::test_new(&db_path);
    app.foreground_run_id = Some(88);

    let (app_tx, mut app_rx) = mpsc::unbounded_channel();
    let (agent_tx, _) = mpsc::unbounded_channel();
    let (first_reply_tx, mut first_reply_rx) = oneshot::channel();
    let (second_reply_tx, mut second_reply_rx) = oneshot::channel();
    let (third_reply_tx, third_reply_rx) = oneshot::channel();

    app.receive_question_request(
        ava_tools::core::question::QuestionRequest {
            run_id: Some("41".to_string()),
            question: "Background question".to_string(),
            options: Vec::new(),
            reply: first_reply_tx,
        },
        app_tx.clone(),
    )
    .await;

    let first_request_id = app
        .state
        .question
        .as_ref()
        .expect("first question")
        .request_id
        .clone();

    app.receive_question_request(
        ava_tools::core::question::QuestionRequest {
            run_id: Some("42".to_string()),
            question: "Foreground question".to_string(),
            options: Vec::new(),
            reply: second_reply_tx,
        },
        app_tx.clone(),
    )
    .await;

    tokio::time::sleep(Duration::from_millis(20)).await;
    assert!(matches!(
        app_rx.try_recv(),
        Err(tokio::sync::mpsc::error::TryRecvError::Empty)
    ));
    assert!(matches!(
        first_reply_rx.try_recv(),
        Err(tokio::sync::oneshot::error::TryRecvError::Empty)
    ));

    app.receive_question_request(
        ava_tools::core::question::QuestionRequest {
            run_id: Some("41".to_string()),
            question: "Replacement background question".to_string(),
            options: Vec::new(),
            reply: third_reply_tx,
        },
        app_tx.clone(),
    )
    .await;

    let first_cleared = recv_interactive_cleared(&mut app_rx).await;
    match &first_cleared {
        AppEvent::InteractiveRequestCleared {
            request_id,
            request_kind,
            timed_out,
            run_id,
        } => {
            assert_eq!(request_id, &first_request_id);
            assert_eq!(*request_kind, InteractiveRequestKind::Question);
            assert!(!timed_out);
            assert_eq!(run_id.as_deref(), Some("41"));
        }
        other => panic!("unexpected event: {other:?}"),
    }
    app.handle_event(first_cleared, app_tx.clone(), agent_tx.clone());

    assert_eq!(
        app.state
            .question
            .as_ref()
            .map(|question| question.question.as_str()),
        Some("Replacement background question")
    );
    assert!(matches!(
        second_reply_rx.try_recv(),
        Err(tokio::sync::oneshot::error::TryRecvError::Empty)
    ));

    app.cancel_pending_interactive_requests(app_tx.clone(), "cleanup", Some("42".to_string()));
    let second_cleared = recv_interactive_cleared(&mut app_rx).await;
    app.handle_event(second_cleared, app_tx.clone(), agent_tx.clone());

    app.cancel_pending_interactive_requests(app_tx.clone(), "cleanup", Some("41".to_string()));
    let third_cleared = recv_interactive_cleared(&mut app_rx).await;
    app.handle_event(third_cleared, app_tx, agent_tx);

    assert_eq!(first_reply_rx.await.expect("first answer"), "");
    assert_eq!(second_reply_rx.await.expect("second answer"), "");
    assert_eq!(third_reply_rx.await.expect("third answer"), "");
}

#[tokio::test]
async fn plan_requests_supersede_existing_pending_sender() {
    let temp = tempdir().expect("tempdir");
    let db_path = temp.path().join("data.db");
    let mut app = App::test_new(&db_path);
    app.foreground_run_id = Some(56);

    let (app_tx, mut app_rx) = mpsc::unbounded_channel();
    let (agent_tx, _) = mpsc::unbounded_channel();
    let (first_reply_tx, first_reply_rx) = oneshot::channel();
    let (second_reply_tx, second_reply_rx) = oneshot::channel();

    app.receive_plan_request(
        ava_tools::core::plan::PlanRequest {
            run_id: Some("56".to_string()),
            plan: sample_plan("First plan"),
            reply: first_reply_tx,
        },
        app_tx.clone(),
    )
    .await;

    let first_request_id = app
        .state
        .plan_approval
        .as_ref()
        .expect("first plan")
        .request_id
        .clone();

    app.receive_plan_request(
        ava_tools::core::plan::PlanRequest {
            run_id: Some("56".to_string()),
            plan: sample_plan("Second plan"),
            reply: second_reply_tx,
        },
        app_tx.clone(),
    )
    .await;

    let cleared = recv_interactive_cleared(&mut app_rx).await;
    match &cleared {
        AppEvent::InteractiveRequestCleared {
            request_id,
            request_kind,
            timed_out,
            run_id,
        } => {
            assert_eq!(request_id, &first_request_id);
            assert_eq!(*request_kind, InteractiveRequestKind::Plan);
            assert!(!timed_out);
            assert_eq!(run_id.as_deref(), Some("56"));
        }
        other => panic!("unexpected event: {other:?}"),
    }

    app.handle_event(cleared, app_tx.clone(), agent_tx.clone());

    let second_request_id = app
        .state
        .plan_approval
        .as_ref()
        .expect("second plan")
        .request_id
        .clone();
    assert_ne!(second_request_id, first_request_id);
    assert_eq!(
        app.state
            .plan_approval
            .as_ref()
            .map(|plan| plan.plan.summary.as_str()),
        Some("Second plan")
    );

    app.handle_plan_approval_key(
        crossterm::event::KeyEvent::from(KeyCode::Char('e')),
        app_tx.clone(),
    );

    let resolved = recv_interactive_cleared(&mut app_rx).await;
    app.handle_event(resolved, app_tx, agent_tx);

    assert_eq!(
        first_reply_rx.await.expect("first decision"),
        ava_types::PlanDecision::Rejected {
            feedback: "Superseded by a newer TUI plan request".to_string(),
        }
    );
    assert_eq!(
        second_reply_rx.await.expect("second decision"),
        ava_types::PlanDecision::Approved
    );
}

#[tokio::test]
async fn plan_supersession_and_cancellation_are_scoped_to_origin_run() {
    let temp = tempdir().expect("tempdir");
    let db_path = temp.path().join("data.db");
    let mut app = App::test_new(&db_path);
    app.foreground_run_id = Some(89);

    let (app_tx, mut app_rx) = mpsc::unbounded_channel();
    let (agent_tx, _) = mpsc::unbounded_channel();
    let (first_reply_tx, mut first_reply_rx) = oneshot::channel();
    let (second_reply_tx, mut second_reply_rx) = oneshot::channel();
    let (third_reply_tx, third_reply_rx) = oneshot::channel();

    app.receive_plan_request(
        ava_tools::core::plan::PlanRequest {
            run_id: Some("41".to_string()),
            plan: sample_plan("Background plan"),
            reply: first_reply_tx,
        },
        app_tx.clone(),
    )
    .await;

    let first_request_id = app
        .state
        .plan_approval
        .as_ref()
        .expect("first plan")
        .request_id
        .clone();

    app.receive_plan_request(
        ava_tools::core::plan::PlanRequest {
            run_id: Some("42".to_string()),
            plan: sample_plan("Foreground plan"),
            reply: second_reply_tx,
        },
        app_tx.clone(),
    )
    .await;

    tokio::time::sleep(Duration::from_millis(20)).await;
    assert!(matches!(
        app_rx.try_recv(),
        Err(tokio::sync::mpsc::error::TryRecvError::Empty)
    ));
    assert!(matches!(
        first_reply_rx.try_recv(),
        Err(tokio::sync::oneshot::error::TryRecvError::Empty)
    ));

    app.receive_plan_request(
        ava_tools::core::plan::PlanRequest {
            run_id: Some("41".to_string()),
            plan: sample_plan("Replacement background plan"),
            reply: third_reply_tx,
        },
        app_tx.clone(),
    )
    .await;

    let first_cleared = recv_interactive_cleared(&mut app_rx).await;
    match &first_cleared {
        AppEvent::InteractiveRequestCleared {
            request_id,
            request_kind,
            timed_out,
            run_id,
        } => {
            assert_eq!(request_id, &first_request_id);
            assert_eq!(*request_kind, InteractiveRequestKind::Plan);
            assert!(!timed_out);
            assert_eq!(run_id.as_deref(), Some("41"));
        }
        other => panic!("unexpected event: {other:?}"),
    }
    app.handle_event(first_cleared, app_tx.clone(), agent_tx.clone());

    assert_eq!(
        app.state
            .plan_approval
            .as_ref()
            .map(|plan| plan.plan.summary.as_str()),
        Some("Replacement background plan")
    );
    assert!(matches!(
        second_reply_rx.try_recv(),
        Err(tokio::sync::oneshot::error::TryRecvError::Empty)
    ));

    app.cancel_pending_interactive_requests(app_tx.clone(), "cleanup", Some("42".to_string()));
    let second_cleared = recv_interactive_cleared(&mut app_rx).await;
    app.handle_event(second_cleared, app_tx.clone(), agent_tx.clone());

    app.cancel_pending_interactive_requests(app_tx.clone(), "cleanup", Some("41".to_string()));
    let third_cleared = recv_interactive_cleared(&mut app_rx).await;
    app.handle_event(third_cleared, app_tx, agent_tx);

    assert_eq!(
        first_reply_rx.await.expect("first decision"),
        ava_types::PlanDecision::Rejected {
            feedback: "Superseded by a newer TUI plan request".to_string(),
        }
    );
    assert_eq!(
        second_reply_rx.await.expect("second decision"),
        ava_types::PlanDecision::Rejected {
            feedback: "cleanup".to_string(),
        }
    );
    assert_eq!(
        third_reply_rx.await.expect("third decision"),
        ava_types::PlanDecision::Rejected {
            feedback: "cleanup".to_string(),
        }
    );
}

#[tokio::test]
async fn background_approval_survives_new_foreground_start_without_existing_foreground_run() {
    let temp = tempdir().expect("tempdir");
    let db_path = temp.path().join("data.db");
    let mut app = App::test_new(&db_path);

    let (app_tx, mut app_rx) = mpsc::unbounded_channel();
    let (agent_tx, _) = mpsc::unbounded_channel();
    let (reply_tx, mut reply_rx) = oneshot::channel();

    app.receive_tool_approval_request(
        ava_tools::permission_middleware::ApprovalRequest {
            run_id: Some("41".to_string()),
            call: ava_types::ToolCall {
                id: "call-bg-approve".to_string(),
                name: "bash".to_string(),
                arguments: serde_json::json!({"command": "cargo test -p ava-tui"}),
            },
            inspection: ava_permissions::inspector::InspectionResult {
                action: ava_permissions::Action::Ask,
                reason: "background approval".to_string(),
                risk_level: RiskLevel::High,
                tags: Vec::new(),
                warnings: vec!["danger".to_string()],
            },
            reply: reply_tx,
        },
        app_tx.clone(),
    )
    .await;

    app.submit_goal(
        "start a fresh foreground run".to_string(),
        app_tx.clone(),
        agent_tx,
    );

    tokio::time::sleep(Duration::from_millis(20)).await;
    assert!(matches!(
        app_rx.try_recv(),
        Err(tokio::sync::mpsc::error::TryRecvError::Empty)
    ));
    assert!(matches!(
        reply_rx.try_recv(),
        Err(tokio::sync::oneshot::error::TryRecvError::Empty)
    ));
    assert_eq!(app.state.permission.queue.len(), 1);
    assert_eq!(app.foreground_run_id, Some(1));
    assert!(app.state.agent.is_running);
}

#[tokio::test]
async fn background_question_survives_new_foreground_start_without_existing_foreground_run() {
    let temp = tempdir().expect("tempdir");
    let db_path = temp.path().join("data.db");
    let mut app = App::test_new(&db_path);

    let (app_tx, mut app_rx) = mpsc::unbounded_channel();
    let (agent_tx, _) = mpsc::unbounded_channel();
    let (reply_tx, mut reply_rx) = oneshot::channel();

    app.receive_question_request(
        ava_tools::core::question::QuestionRequest {
            run_id: Some("41".to_string()),
            question: "Background question".to_string(),
            options: Vec::new(),
            reply: reply_tx,
        },
        app_tx.clone(),
    )
    .await;

    app.submit_goal(
        "start a fresh foreground run".to_string(),
        app_tx.clone(),
        agent_tx,
    );

    tokio::time::sleep(Duration::from_millis(20)).await;
    assert!(matches!(
        app_rx.try_recv(),
        Err(tokio::sync::mpsc::error::TryRecvError::Empty)
    ));
    assert!(matches!(
        reply_rx.try_recv(),
        Err(tokio::sync::oneshot::error::TryRecvError::Empty)
    ));
    assert_eq!(
        app.state
            .question
            .as_ref()
            .map(|question| question.question.as_str()),
        Some("Background question")
    );
    assert_eq!(app.state.active_modal, Some(ModalType::Question));
    assert_eq!(app.foreground_run_id, Some(1));
    assert!(app.state.agent.is_running);
}

#[tokio::test]
async fn background_plan_survives_new_foreground_start_without_existing_foreground_run() {
    let temp = tempdir().expect("tempdir");
    let db_path = temp.path().join("data.db");
    let mut app = App::test_new(&db_path);

    let (app_tx, mut app_rx) = mpsc::unbounded_channel();
    let (agent_tx, _) = mpsc::unbounded_channel();
    let (reply_tx, mut reply_rx) = oneshot::channel();

    app.receive_plan_request(
        ava_tools::core::plan::PlanRequest {
            run_id: Some("41".to_string()),
            plan: sample_plan("Background plan"),
            reply: reply_tx,
        },
        app_tx.clone(),
    )
    .await;

    app.submit_goal(
        "start a fresh foreground run".to_string(),
        app_tx.clone(),
        agent_tx,
    );

    tokio::time::sleep(Duration::from_millis(20)).await;
    assert!(matches!(
        app_rx.try_recv(),
        Err(tokio::sync::mpsc::error::TryRecvError::Empty)
    ));
    assert!(matches!(
        reply_rx.try_recv(),
        Err(tokio::sync::oneshot::error::TryRecvError::Empty)
    ));
    assert_eq!(
        app.state
            .plan_approval
            .as_ref()
            .map(|plan| plan.plan.summary.as_str()),
        Some("Background plan")
    );
    assert_eq!(app.state.active_modal, Some(ModalType::PlanApproval));
    assert_eq!(app.foreground_run_id, Some(1));
    assert!(app.state.agent.is_running);
}

#[tokio::test]
async fn approval_from_other_run_is_queued_behind_visible_question_until_question_clears() {
    let temp = tempdir().expect("tempdir");
    let db_path = temp.path().join("data.db");
    let mut app = App::test_new(&db_path);

    let (app_tx, mut app_rx) = mpsc::unbounded_channel();
    let (agent_tx, _) = mpsc::unbounded_channel();
    let (question_reply_tx, question_reply_rx) = oneshot::channel();
    let (approval_reply_tx, approval_reply_rx) = oneshot::channel();

    app.receive_question_request(
        ava_tools::core::question::QuestionRequest {
            run_id: Some("41".to_string()),
            question: "Visible question?".to_string(),
            options: Vec::new(),
            reply: question_reply_tx,
        },
        app_tx.clone(),
    )
    .await;

    app.receive_tool_approval_request(
        sample_approval_request(
            "42",
            "call-approval-question",
            "cargo test -p ava-tui",
            approval_reply_tx,
        ),
        app_tx.clone(),
    )
    .await;

    let approval_request_id = app
        .state
        .permission
        .queue
        .front()
        .expect("queued approval")
        .request_id
        .clone();

    assert_eq!(app.state.active_modal, Some(ModalType::Question));
    assert!(app.state.permission.queue.len() == 1);
    assert!(matches!(
        app.queued_interactive_modals.front(),
        Some(QueuedInteractiveModal::Approval(request_id)) if request_id == &approval_request_id
    ));

    app.state.question.as_mut().expect("question state").input = "Done".to_string();
    app.handle_question_key(
        crossterm::event::KeyEvent::from(KeyCode::Enter),
        app_tx.clone(),
    );

    assert_eq!(app.state.active_modal, Some(ModalType::ToolApproval));

    let question_cleared = recv_interactive_cleared(&mut app_rx).await;
    app.handle_event(question_cleared, app_tx.clone(), agent_tx.clone());

    app.state.permission.current_stage = crate::state::permission::ApprovalStage::ActionSelect;
    app.handle_tool_approval_key(
        crossterm::event::KeyEvent::from(KeyCode::Char('a')),
        app_tx.clone(),
    );

    let approval_cleared = recv_interactive_cleared(&mut app_rx).await;
    app.handle_event(approval_cleared, app_tx, agent_tx);

    assert_eq!(question_reply_rx.await.expect("question answer"), "Done");
    assert_eq!(
        approval_reply_rx.await.expect("approval answer"),
        ava_tools::permission_middleware::ToolApproval::Allowed
    );
}

#[tokio::test]
async fn approval_from_other_run_is_queued_behind_visible_plan_until_plan_clears() {
    let temp = tempdir().expect("tempdir");
    let db_path = temp.path().join("data.db");
    let mut app = App::test_new(&db_path);

    let (app_tx, mut app_rx) = mpsc::unbounded_channel();
    let (agent_tx, _) = mpsc::unbounded_channel();
    let (plan_reply_tx, plan_reply_rx) = oneshot::channel();
    let (approval_reply_tx, approval_reply_rx) = oneshot::channel();

    app.receive_plan_request(
        ava_tools::core::plan::PlanRequest {
            run_id: Some("41".to_string()),
            plan: sample_plan("Visible plan"),
            reply: plan_reply_tx,
        },
        app_tx.clone(),
    )
    .await;

    app.receive_tool_approval_request(
        sample_approval_request(
            "42",
            "call-approval-plan",
            "cargo test -p ava-agent",
            approval_reply_tx,
        ),
        app_tx.clone(),
    )
    .await;

    let approval_request_id = app
        .state
        .permission
        .queue
        .front()
        .expect("queued approval")
        .request_id
        .clone();

    assert_eq!(app.state.active_modal, Some(ModalType::PlanApproval));
    assert!(matches!(
        app.queued_interactive_modals.front(),
        Some(QueuedInteractiveModal::Approval(request_id)) if request_id == &approval_request_id
    ));

    app.handle_plan_approval_key(
        crossterm::event::KeyEvent::from(KeyCode::Char('e')),
        app_tx.clone(),
    );

    assert_eq!(app.state.active_modal, Some(ModalType::ToolApproval));

    let plan_cleared = recv_interactive_cleared(&mut app_rx).await;
    app.handle_event(plan_cleared, app_tx.clone(), agent_tx.clone());

    app.state.permission.current_stage = crate::state::permission::ApprovalStage::ActionSelect;
    app.handle_tool_approval_key(
        crossterm::event::KeyEvent::from(KeyCode::Char('a')),
        app_tx.clone(),
    );

    let approval_cleared = recv_interactive_cleared(&mut app_rx).await;
    app.handle_event(approval_cleared, app_tx, agent_tx);

    assert_eq!(
        plan_reply_rx.await.expect("plan decision"),
        ava_types::PlanDecision::Approved
    );
    assert_eq!(
        approval_reply_rx.await.expect("approval answer"),
        ava_tools::permission_middleware::ToolApproval::Allowed
    );
}

#[tokio::test]
async fn question_from_other_run_is_queued_behind_visible_approval_until_approval_clears() {
    let temp = tempdir().expect("tempdir");
    let db_path = temp.path().join("data.db");
    let mut app = App::test_new(&db_path);

    let (app_tx, mut app_rx) = mpsc::unbounded_channel();
    let (agent_tx, _) = mpsc::unbounded_channel();
    let (approval_reply_tx, approval_reply_rx) = oneshot::channel();
    let (question_reply_tx, question_reply_rx) = oneshot::channel();

    app.receive_tool_approval_request(
        sample_approval_request(
            "41",
            "call-visible-approval",
            "cargo test -p ava-tui",
            approval_reply_tx,
        ),
        app_tx.clone(),
    )
    .await;

    app.receive_question_request(
        ava_tools::core::question::QuestionRequest {
            run_id: Some("42".to_string()),
            question: "Queued question?".to_string(),
            options: Vec::new(),
            reply: question_reply_tx,
        },
        app_tx.clone(),
    )
    .await;

    assert_eq!(app.state.active_modal, Some(ModalType::ToolApproval));
    assert!(app.state.question.is_none());
    assert_eq!(app.queued_interactive_modals.len(), 1);

    app.state.permission.current_stage = crate::state::permission::ApprovalStage::ActionSelect;
    app.handle_tool_approval_key(
        crossterm::event::KeyEvent::from(KeyCode::Char('a')),
        app_tx.clone(),
    );

    let approval_cleared = recv_interactive_cleared(&mut app_rx).await;
    app.handle_event(approval_cleared, app_tx.clone(), agent_tx.clone());

    assert_eq!(app.state.active_modal, Some(ModalType::Question));
    assert_eq!(
        app.state
            .question
            .as_ref()
            .map(|question| question.question.as_str()),
        Some("Queued question?")
    );

    app.state.question.as_mut().expect("question state").input = "Answer".to_string();
    app.handle_question_key(
        crossterm::event::KeyEvent::from(KeyCode::Enter),
        app_tx.clone(),
    );

    let question_cleared = recv_interactive_cleared(&mut app_rx).await;
    app.handle_event(question_cleared, app_tx, agent_tx);

    assert_eq!(
        approval_reply_rx.await.expect("approval answer"),
        ava_tools::permission_middleware::ToolApproval::Allowed
    );
    assert_eq!(question_reply_rx.await.expect("question answer"), "Answer");
}

#[tokio::test]
async fn plan_from_other_run_is_queued_behind_visible_approval_until_approval_clears() {
    let temp = tempdir().expect("tempdir");
    let db_path = temp.path().join("data.db");
    let mut app = App::test_new(&db_path);

    let (app_tx, mut app_rx) = mpsc::unbounded_channel();
    let (agent_tx, _) = mpsc::unbounded_channel();
    let (approval_reply_tx, approval_reply_rx) = oneshot::channel();
    let (plan_reply_tx, plan_reply_rx) = oneshot::channel();

    app.receive_tool_approval_request(
        sample_approval_request(
            "41",
            "call-visible-approval-plan",
            "cargo test -p ava-agent",
            approval_reply_tx,
        ),
        app_tx.clone(),
    )
    .await;

    app.receive_plan_request(
        ava_tools::core::plan::PlanRequest {
            run_id: Some("42".to_string()),
            plan: sample_plan("Queued plan"),
            reply: plan_reply_tx,
        },
        app_tx.clone(),
    )
    .await;

    assert_eq!(app.state.active_modal, Some(ModalType::ToolApproval));
    assert!(app.state.plan_approval.is_none());
    assert_eq!(app.queued_interactive_modals.len(), 1);

    app.state.permission.current_stage = crate::state::permission::ApprovalStage::ActionSelect;
    app.handle_tool_approval_key(
        crossterm::event::KeyEvent::from(KeyCode::Char('a')),
        app_tx.clone(),
    );

    let approval_cleared = recv_interactive_cleared(&mut app_rx).await;
    app.handle_event(approval_cleared, app_tx.clone(), agent_tx.clone());

    assert_eq!(app.state.active_modal, Some(ModalType::PlanApproval));
    assert_eq!(
        app.state
            .plan_approval
            .as_ref()
            .map(|plan| plan.plan.summary.as_str()),
        Some("Queued plan")
    );

    app.handle_plan_approval_key(
        crossterm::event::KeyEvent::from(KeyCode::Char('e')),
        app_tx.clone(),
    );

    let plan_cleared = recv_interactive_cleared(&mut app_rx).await;
    app.handle_event(plan_cleared, app_tx, agent_tx);

    assert_eq!(
        approval_reply_rx.await.expect("approval answer"),
        ava_tools::permission_middleware::ToolApproval::Allowed
    );
    assert_eq!(
        plan_reply_rx.await.expect("plan decision"),
        ava_types::PlanDecision::Approved
    );
}

#[tokio::test]
async fn queued_question_timeout_does_not_start_until_hidden_request_is_promoted() {
    let temp = tempdir().expect("tempdir");
    let db_path = temp.path().join("data.db");
    let mut app = App::test_new(&db_path);
    app.set_interactive_timeout_for_test(Duration::from_millis(60));

    let (app_tx, mut app_rx) = mpsc::unbounded_channel();
    let (agent_tx, _) = mpsc::unbounded_channel();
    let (approval_reply_tx, approval_reply_rx) = oneshot::channel();
    let (question_reply_tx, mut question_reply_rx) = oneshot::channel();

    app.receive_tool_approval_request(
        sample_approval_request(
            "41",
            "call-timeout-approval",
            "cargo test -p ava-tui",
            approval_reply_tx,
        ),
        app_tx.clone(),
    )
    .await;

    app.receive_question_request(
        ava_tools::core::question::QuestionRequest {
            run_id: Some("42".to_string()),
            question: "Hidden timeout?".to_string(),
            options: Vec::new(),
            reply: question_reply_tx,
        },
        app_tx.clone(),
    )
    .await;

    tokio::time::sleep(Duration::from_millis(35)).await;
    assert!(matches!(
        app_rx.try_recv(),
        Err(tokio::sync::mpsc::error::TryRecvError::Empty)
    ));
    assert!(matches!(
        question_reply_rx.try_recv(),
        Err(tokio::sync::oneshot::error::TryRecvError::Empty)
    ));

    app.state.permission.current_stage = crate::state::permission::ApprovalStage::ActionSelect;
    app.handle_tool_approval_key(
        crossterm::event::KeyEvent::from(KeyCode::Char('a')),
        app_tx.clone(),
    );

    let approval_cleared = recv_interactive_cleared(&mut app_rx).await;
    app.handle_event(approval_cleared, app_tx.clone(), agent_tx.clone());
    assert_eq!(app.state.active_modal, Some(ModalType::Question));

    tokio::time::sleep(Duration::from_millis(35)).await;
    assert!(matches!(
        app_rx.try_recv(),
        Err(tokio::sync::mpsc::error::TryRecvError::Empty)
    ));
    assert!(matches!(
        question_reply_rx.try_recv(),
        Err(tokio::sync::oneshot::error::TryRecvError::Empty)
    ));

    let question_cleared = recv_interactive_cleared(&mut app_rx).await;
    match &question_cleared {
        AppEvent::InteractiveRequestCleared {
            request_kind,
            timed_out,
            ..
        } => {
            assert_eq!(*request_kind, InteractiveRequestKind::Question);
            assert!(*timed_out);
        }
        other => panic!("unexpected event: {other:?}"),
    }
    app.handle_event(question_cleared, app_tx, agent_tx);

    assert_eq!(
        approval_reply_rx.await.expect("approval answer"),
        ava_tools::permission_middleware::ToolApproval::Allowed
    );
    assert_eq!(question_reply_rx.await.expect("question answer"), "");
}

#[tokio::test]
async fn queued_plan_timeout_does_not_start_until_hidden_request_is_promoted() {
    let temp = tempdir().expect("tempdir");
    let db_path = temp.path().join("data.db");
    let mut app = App::test_new(&db_path);
    app.set_interactive_timeout_for_test(Duration::from_millis(60));

    let (app_tx, mut app_rx) = mpsc::unbounded_channel();
    let (agent_tx, _) = mpsc::unbounded_channel();
    let (approval_reply_tx, approval_reply_rx) = oneshot::channel();
    let (plan_reply_tx, mut plan_reply_rx) = oneshot::channel();

    app.receive_tool_approval_request(
        sample_approval_request(
            "41",
            "call-timeout-approval-plan",
            "cargo test -p ava-agent",
            approval_reply_tx,
        ),
        app_tx.clone(),
    )
    .await;

    app.receive_plan_request(
        ava_tools::core::plan::PlanRequest {
            run_id: Some("42".to_string()),
            plan: sample_plan("Hidden plan timeout"),
            reply: plan_reply_tx,
        },
        app_tx.clone(),
    )
    .await;

    tokio::time::sleep(Duration::from_millis(35)).await;
    assert!(matches!(
        app_rx.try_recv(),
        Err(tokio::sync::mpsc::error::TryRecvError::Empty)
    ));
    assert!(matches!(
        plan_reply_rx.try_recv(),
        Err(tokio::sync::oneshot::error::TryRecvError::Empty)
    ));

    app.state.permission.current_stage = crate::state::permission::ApprovalStage::ActionSelect;
    app.handle_tool_approval_key(
        crossterm::event::KeyEvent::from(KeyCode::Char('a')),
        app_tx.clone(),
    );

    let approval_cleared = recv_interactive_cleared(&mut app_rx).await;
    app.handle_event(approval_cleared, app_tx.clone(), agent_tx.clone());
    assert_eq!(app.state.active_modal, Some(ModalType::PlanApproval));

    tokio::time::sleep(Duration::from_millis(35)).await;
    assert!(matches!(
        app_rx.try_recv(),
        Err(tokio::sync::mpsc::error::TryRecvError::Empty)
    ));
    assert!(matches!(
        plan_reply_rx.try_recv(),
        Err(tokio::sync::oneshot::error::TryRecvError::Empty)
    ));

    let plan_cleared = recv_interactive_cleared(&mut app_rx).await;
    match &plan_cleared {
        AppEvent::InteractiveRequestCleared {
            request_kind,
            timed_out,
            ..
        } => {
            assert_eq!(*request_kind, InteractiveRequestKind::Plan);
            assert!(*timed_out);
        }
        other => panic!("unexpected event: {other:?}"),
    }
    app.handle_event(plan_cleared, app_tx, agent_tx);

    assert_eq!(
        approval_reply_rx.await.expect("approval answer"),
        ava_tools::permission_middleware::ToolApproval::Allowed
    );
    assert_eq!(
        plan_reply_rx.await.expect("plan answer"),
        ava_types::PlanDecision::Rejected {
            feedback: "Timed out waiting for plan response in TUI".to_string(),
        }
    );
}

#[tokio::test]
async fn question_from_other_run_is_queued_behind_visible_plan_until_plan_clears() {
    let temp = tempdir().expect("tempdir");
    let db_path = temp.path().join("data.db");
    let mut app = App::test_new(&db_path);

    let (app_tx, mut app_rx) = mpsc::unbounded_channel();
    let (agent_tx, _) = mpsc::unbounded_channel();
    let (plan_reply_tx, plan_reply_rx) = oneshot::channel();
    let (question_reply_tx, question_reply_rx) = oneshot::channel();

    app.receive_plan_request(
        ava_tools::core::plan::PlanRequest {
            run_id: Some("41".to_string()),
            plan: sample_plan("Visible plan"),
            reply: plan_reply_tx,
        },
        app_tx.clone(),
    )
    .await;

    app.receive_question_request(
        ava_tools::core::question::QuestionRequest {
            run_id: Some("42".to_string()),
            question: "Queued question?".to_string(),
            options: Vec::new(),
            reply: question_reply_tx,
        },
        app_tx.clone(),
    )
    .await;

    assert_eq!(app.state.active_modal, Some(ModalType::PlanApproval));
    assert_eq!(
        app.state
            .plan_approval
            .as_ref()
            .map(|plan| plan.plan.summary.as_str()),
        Some("Visible plan")
    );
    assert!(app.state.question.is_none());
    assert_eq!(app.queued_interactive_modals.len(), 1);

    app.handle_plan_approval_key(
        crossterm::event::KeyEvent::from(KeyCode::Char('e')),
        app_tx.clone(),
    );

    assert_eq!(app.state.active_modal, Some(ModalType::Question));
    assert_eq!(
        app.state
            .question
            .as_ref()
            .map(|question| question.question.as_str()),
        Some("Queued question?")
    );
    assert!(app.state.plan_approval.is_none());

    let plan_cleared = recv_interactive_cleared(&mut app_rx).await;
    app.handle_event(plan_cleared, app_tx.clone(), agent_tx.clone());
    assert_eq!(app.state.active_modal, Some(ModalType::Question));

    app.state.question.as_mut().expect("question state").input = "Answer".to_string();
    app.handle_question_key(
        crossterm::event::KeyEvent::from(KeyCode::Enter),
        app_tx.clone(),
    );

    let question_cleared = recv_interactive_cleared(&mut app_rx).await;
    app.handle_event(question_cleared, app_tx, agent_tx);

    assert_eq!(
        plan_reply_rx.await.expect("plan decision"),
        ava_types::PlanDecision::Approved
    );
    assert_eq!(question_reply_rx.await.expect("question answer"), "Answer");
}

#[tokio::test]
async fn plan_from_other_run_is_queued_behind_visible_question_until_question_clears() {
    let temp = tempdir().expect("tempdir");
    let db_path = temp.path().join("data.db");
    let mut app = App::test_new(&db_path);

    let (app_tx, mut app_rx) = mpsc::unbounded_channel();
    let (agent_tx, _) = mpsc::unbounded_channel();
    let (question_reply_tx, question_reply_rx) = oneshot::channel();
    let (plan_reply_tx, plan_reply_rx) = oneshot::channel();

    app.receive_question_request(
        ava_tools::core::question::QuestionRequest {
            run_id: Some("41".to_string()),
            question: "Visible question?".to_string(),
            options: Vec::new(),
            reply: question_reply_tx,
        },
        app_tx.clone(),
    )
    .await;

    app.receive_plan_request(
        ava_tools::core::plan::PlanRequest {
            run_id: Some("42".to_string()),
            plan: sample_plan("Queued plan"),
            reply: plan_reply_tx,
        },
        app_tx.clone(),
    )
    .await;

    assert_eq!(app.state.active_modal, Some(ModalType::Question));
    assert_eq!(
        app.state
            .question
            .as_ref()
            .map(|question| question.question.as_str()),
        Some("Visible question?")
    );
    assert!(app.state.plan_approval.is_none());
    assert_eq!(app.queued_interactive_modals.len(), 1);

    app.state.question.as_mut().expect("question state").input = "Done".to_string();
    app.handle_question_key(
        crossterm::event::KeyEvent::from(KeyCode::Enter),
        app_tx.clone(),
    );

    assert_eq!(app.state.active_modal, Some(ModalType::PlanApproval));
    assert_eq!(
        app.state
            .plan_approval
            .as_ref()
            .map(|plan| plan.plan.summary.as_str()),
        Some("Queued plan")
    );
    assert!(app.state.question.is_none());

    let question_cleared = recv_interactive_cleared(&mut app_rx).await;
    app.handle_event(question_cleared, app_tx.clone(), agent_tx.clone());
    assert_eq!(app.state.active_modal, Some(ModalType::PlanApproval));

    app.handle_plan_approval_key(
        crossterm::event::KeyEvent::from(KeyCode::Char('e')),
        app_tx.clone(),
    );

    let plan_cleared = recv_interactive_cleared(&mut app_rx).await;
    app.handle_event(plan_cleared, app_tx, agent_tx);

    assert_eq!(question_reply_rx.await.expect("question answer"), "Done");
    assert_eq!(
        plan_reply_rx.await.expect("plan decision"),
        ava_types::PlanDecision::Approved
    );
}

#[tokio::test]
async fn submitting_new_goal_clears_stale_plan_request_from_previous_run() {
    let temp = tempdir().expect("tempdir");
    let db_path = temp.path().join("data.db");
    let mut app = App::test_new(&db_path);
    app.foreground_run_id = Some(21);

    let (app_tx, mut app_rx) = mpsc::unbounded_channel();
    let (agent_tx, _) = mpsc::unbounded_channel();
    let (reply_tx, reply_rx) = oneshot::channel();

    app.receive_plan_request(
        ava_tools::core::plan::PlanRequest {
            run_id: Some("21".to_string()),
            plan: sample_plan("Stale plan should clear before next run"),
            reply: reply_tx,
        },
        app_tx.clone(),
    )
    .await;

    let request_id = app
        .state
        .plan_approval
        .as_ref()
        .expect("pending plan")
        .request_id
        .clone();

    app.submit_goal(
        "start a fresh foreground run".to_string(),
        app_tx.clone(),
        agent_tx.clone(),
    );

    let cleared = recv_interactive_cleared(&mut app_rx).await;

    match &cleared {
        AppEvent::InteractiveRequestCleared {
            request_id: cleared_id,
            request_kind,
            timed_out,
            run_id,
        } => {
            assert_eq!(cleared_id, &request_id);
            assert_eq!(*request_kind, InteractiveRequestKind::Plan);
            assert!(!timed_out);
            assert_eq!(run_id.as_deref(), Some("21"));
        }
        other => panic!("unexpected event: {other:?}"),
    }

    app.handle_event(cleared, app_tx, agent_tx);

    assert!(app.state.plan_approval.is_none());
    assert_eq!(app.state.active_modal, None);
    assert_eq!(app.foreground_run_id, Some(1));
    assert!(app.state.agent.is_running);
    assert_eq!(
        reply_rx.await.expect("stale plan response"),
        ava_types::PlanDecision::Rejected {
            feedback: "Superseded by a new TUI run".to_string(),
        }
    );
}
