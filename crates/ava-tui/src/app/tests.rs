use super::*;
use ava_agent::control_plane::commands::{queue_message_tier, ControlPlaneCommand};
use ava_agent::control_plane::events::{required_backend_event_kinds, CanonicalEventKind};
use ava_agent::control_plane::interactive::InteractiveRequestKind;
use ava_permissions::tags::RiskLevel;
use std::time::Duration;
use tempfile::tempdir;
use tokio::sync::oneshot;

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

#[tokio::test]
async fn question_requests_use_shared_timeout_and_clear_lifecycle() {
    let temp = tempdir().expect("tempdir");
    let db_path = temp.path().join("data.db");
    let mut app = App::test_new(&db_path);
    app.set_interactive_timeout_for_test(Duration::from_millis(5));
    app.foreground_run_id = Some(77);

    let (app_tx, mut app_rx) = mpsc::unbounded_channel();
    let (agent_tx, _) = mpsc::unbounded_channel();
    let (reply_tx, mut reply_rx) = oneshot::channel();

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
    let (reply_tx, mut reply_rx) = oneshot::channel();

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
    let (reply_tx, mut reply_rx) = oneshot::channel();

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
    let (reply_tx, mut reply_rx) = oneshot::channel();

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
    let (reply_tx, mut reply_rx) = oneshot::channel();

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
    let (first_reply_tx, mut first_reply_rx) = oneshot::channel();
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
    let (first_reply_tx, mut first_reply_rx) = oneshot::channel();
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
    let (first_reply_tx, mut first_reply_rx) = oneshot::channel();
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
    let (first_reply_tx, mut first_reply_rx) = oneshot::channel();
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
