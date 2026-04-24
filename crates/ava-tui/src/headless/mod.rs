use crate::config::cli::CliArgs;
use crate::state::session::SessionState;
use ava_permissions::tags::RiskLevel;
use ava_tools::permission_middleware::{ApprovalRequest, ToolApproval};
use ava_types::Message;
use color_eyre::eyre::{eyre, Result};
use tracing::{instrument, warn};
use uuid::Uuid;

mod input;
mod single;
mod watch;

#[cfg(feature = "voice")]
mod voice;

pub(crate) fn spawn_auto_approve_requests(
    mut approval_rx: tokio::sync::mpsc::UnboundedReceiver<ApprovalRequest>,
) {
    tokio::spawn(async move {
        while let Some(req) = approval_rx.recv().await {
            let decision = headless_tool_approval(&req);
            let _ = req.reply.send(decision);
        }
    });
}

fn headless_tool_approval(req: &ApprovalRequest) -> ToolApproval {
    match req.inspection.risk_level {
        RiskLevel::Safe | RiskLevel::Low | RiskLevel::Medium => ToolApproval::Allowed,
        RiskLevel::High | RiskLevel::Critical => ToolApproval::Rejected(Some(format!(
            "Headless mode rejected dangerous action '{}': {} (risk: {:?})",
            req.call.name, req.inspection.reason, req.inspection.risk_level
        ))),
    }
}

#[derive(Debug, Clone, PartialEq, Eq)]
struct HeadlessResumeRestorePlan {
    apply_primary_agent: bool,
    primary_agent_id: Option<String>,
    primary_agent_prompt: Option<String>,
    restore_model: Option<(String, String)>,
}

fn headless_resume_restore_from_metadata(
    metadata: &serde_json::Value,
    cli_agent_override: Option<&str>,
    cli_provider_model_override: bool,
) -> HeadlessResumeRestorePlan {
    let apply_primary_agent = cli_agent_override.is_none();

    let primary_agent_id = apply_primary_agent
        .then(|| {
            metadata
                .as_object()
                .and_then(|meta| meta.get("primaryAgentId").and_then(|v| v.as_str()))
                .map(|value| value.to_string())
        })
        .flatten();

    let primary_agent_prompt = apply_primary_agent
        .then(|| {
            metadata
                .as_object()
                .and_then(|meta| meta.get("primaryAgentPrompt").and_then(|v| v.as_str()))
                .map(|value| value.to_string())
        })
        .flatten();

    let restore_model = if cli_agent_override.is_some() || cli_provider_model_override {
        None
    } else {
        metadata.as_object().and_then(|meta| {
            Some((
                meta.get("provider")?.as_str()?.to_string(),
                meta.get("model")?.as_str()?.to_string(),
            ))
        })
    };

    HeadlessResumeRestorePlan {
        apply_primary_agent,
        primary_agent_id,
        primary_agent_prompt,
        restore_model,
    }
}

fn apply_headless_resume_metadata(
    startup: &mut crate::config::cli::StartupSelection,
    metadata: &serde_json::Value,
    cli_agent_override: Option<&str>,
    cli_provider_model_override: bool,
) -> Option<(String, String)> {
    let restore_plan = headless_resume_restore_from_metadata(
        metadata,
        cli_agent_override,
        cli_provider_model_override,
    );

    if restore_plan.apply_primary_agent {
        startup.primary_agent_id = restore_plan.primary_agent_id;
        startup.primary_agent_prompt = restore_plan.primary_agent_prompt;
    }

    // Keep startup provider/model unchanged until AgentStack exists.
    // Resume model restore should degrade safely if stale.
    restore_plan.restore_model
}

fn headless_resume_context(
    resume_session: Option<&ava_types::Session>,
) -> (Option<Uuid>, Vec<Message>) {
    let resume_session_id = resume_session.map(|session| session.id);
    let resume_history = resume_session
        .map(|session| session.messages.clone())
        .unwrap_or_default();
    (resume_session_id, resume_history)
}

#[allow(dead_code)] // Used by the optional `voice` feature module.
fn update_headless_resume_context_from_session(
    resume_session_id: &mut Option<Uuid>,
    resume_history: &mut Vec<Message>,
    session: &ava_types::Session,
) {
    *resume_session_id = Some(session.id);
    *resume_history = session.messages.clone();
}

fn apply_headless_legacy_primary_agent_prompt_fallback(
    startup: &mut crate::config::cli::StartupSelection,
    previous_primary_agent_id: Option<&str>,
    previous_primary_agent_prompt: Option<String>,
    resolved_prompt: Option<String>,
) {
    if startup.primary_agent_prompt.is_some() {
        return;
    }

    if let Some(prompt) = resolved_prompt {
        startup.primary_agent_prompt = Some(prompt);
        return;
    }

    if previous_primary_agent_id == startup.primary_agent_id.as_deref() {
        startup.primary_agent_prompt = previous_primary_agent_prompt;
    }
}

async fn resolve_primary_agent_prompt_from_config(primary_agent_id: &str) -> Option<String> {
    match crate::config::cli::resolve_startup_selection(None, None, Some(primary_agent_id)).await {
        Ok(startup) => startup.primary_agent_prompt,
        Err(err) => {
            warn!(
                primary_agent_id,
                error = %err,
                "failed to rehydrate legacy primary agent prompt during headless resume"
            );
            None
        }
    }
}

pub(super) struct HeadlessStartupSelection {
    pub startup: crate::config::cli::StartupSelection,
    pub resume_session_id: Option<Uuid>,
    pub resume_history: Vec<Message>,
    pub resume_restore_model: Option<(String, String)>,
}

pub(super) async fn resolve_headless_startup_selection(
    cli: &CliArgs,
) -> Result<HeadlessStartupSelection> {
    let data_dir = ava_config::data_dir().unwrap_or_else(|_| std::path::PathBuf::from("."));
    std::fs::create_dir_all(&data_dir)?;
    let db_path = ava_config::app_db_path().unwrap_or_else(|_| data_dir.join("data.db"));
    let mut session_state = SessionState::new(&db_path)?;
    let resume_session =
        session_state.resolve_startup_session(cli.resume, cli.session.as_deref())?;

    let mut startup = cli.resolve_startup_selection().await?;
    let (resume_session_id, resume_history) = headless_resume_context(resume_session.as_ref());
    let mut resume_restore_model = None;

    if let Some(session) = resume_session.as_ref() {
        let previous_primary_agent_id = startup.primary_agent_id.clone();
        let previous_primary_agent_prompt = startup.primary_agent_prompt.clone();

        resume_restore_model = apply_headless_resume_metadata(
            &mut startup,
            &session.metadata,
            cli.agent.as_deref(),
            cli.provider.is_some() || cli.model.is_some(),
        );

        if cli.agent.is_none()
            && startup.primary_agent_id.is_some()
            && startup.primary_agent_prompt.is_none()
        {
            if let Some(restored_primary_agent_id) = startup.primary_agent_id.as_deref() {
                let resolved_prompt =
                    resolve_primary_agent_prompt_from_config(restored_primary_agent_id).await;
                apply_headless_legacy_primary_agent_prompt_fallback(
                    &mut startup,
                    previous_primary_agent_id.as_deref(),
                    previous_primary_agent_prompt,
                    resolved_prompt,
                );
            }
        }
    }

    Ok(HeadlessStartupSelection {
        startup,
        resume_session_id,
        resume_history,
        resume_restore_model,
    })
}

#[instrument(skip(cli))]
pub async fn run_headless(cli: CliArgs) -> Result<()> {
    if cli.watch {
        return watch::run_watch_mode(cli).await;
    }

    if cli.voice {
        #[cfg(feature = "voice")]
        return voice::run_voice_loop(cli).await;

        #[cfg(not(feature = "voice"))]
        return Err(eyre!(
            "Voice input requires the 'voice' feature. Rebuild with: cargo build --features voice"
        ));
    }

    let goal = cli
        .goal
        .as_ref()
        .ok_or_else(|| eyre!("No goal provided. Usage: ava \"your goal here\""))?
        .clone();

    single::run_single_agent(cli, &goal).await
}

#[cfg(test)]
mod tests {
    use super::*;
    use ava_agent::message_queue::MessageQueue;
    use ava_permissions::inspector::InspectionResult;
    use ava_permissions::Action;
    use ava_types::MessageTier;
    use ava_types::ToolCall;
    use serde_json::json;
    use std::path::Path;

    fn test_tool_call(name: &str) -> ToolCall {
        ToolCall {
            id: format!("call-{name}"),
            name: name.to_string(),
            arguments: json!({}),
        }
    }

    fn test_inspection(risk_level: RiskLevel, reason: &str) -> InspectionResult {
        InspectionResult {
            action: Action::Ask,
            reason: reason.to_string(),
            risk_level,
            tags: Vec::new(),
            warnings: Vec::new(),
        }
    }

    async fn resolve_headless_approval(risk_level: RiskLevel) -> ToolApproval {
        let (tx, rx) = tokio::sync::mpsc::unbounded_channel();
        spawn_auto_approve_requests(rx);

        let (reply_tx, reply_rx) = tokio::sync::oneshot::channel();
        tx.send(ApprovalRequest {
            run_id: None,
            call: test_tool_call("bash"),
            inspection: test_inspection(risk_level, "needs approval"),
            reply: reply_tx,
        })
        .expect("send headless approval request");

        reply_rx.await.expect("receive headless approval decision")
    }

    #[tokio::test]
    async fn headless_auto_approves_safe_requests() {
        assert_eq!(
            resolve_headless_approval(RiskLevel::Medium).await,
            ToolApproval::Allowed
        );
    }

    #[tokio::test]
    async fn headless_rejects_dangerous_requests() {
        let decision = resolve_headless_approval(RiskLevel::High).await;

        assert!(matches!(
            decision,
            ToolApproval::Rejected(Some(reason))
                if reason.contains("Headless mode rejected dangerous action")
                    && reason.contains("risk: High")
        ));
    }

    #[tokio::test]
    async fn headless_keeps_critical_requests_blocked() {
        let decision = resolve_headless_approval(RiskLevel::Critical).await;

        assert!(matches!(
            decision,
            ToolApproval::Rejected(Some(reason))
                if reason.contains("Headless mode rejected dangerous action")
                    && reason.contains("risk: Critical")
        ));
    }

    #[test]
    fn headless_resume_restore_uses_session_metadata_without_cli_overrides() {
        let metadata = serde_json::json!({
            "provider": "openrouter",
            "model": "anthropic/claude-sonnet-4",
            "primaryAgentId": "architect",
            "primaryAgentPrompt": "You are the architect profile"
        });

        let restore_plan = headless_resume_restore_from_metadata(&metadata, None, false);

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
    fn headless_resume_restore_skips_session_metadata_when_cli_agent_override_present() {
        let metadata = serde_json::json!({
            "provider": "openrouter",
            "model": "anthropic/claude-sonnet-4",
            "primaryAgentId": "architect",
            "primaryAgentPrompt": "You are the architect profile"
        });

        let restore_plan = headless_resume_restore_from_metadata(&metadata, Some("coder"), false);

        assert!(!restore_plan.apply_primary_agent);
        assert!(restore_plan.primary_agent_id.is_none());
        assert!(restore_plan.primary_agent_prompt.is_none());
        assert!(restore_plan.restore_model.is_none());
    }

    #[test]
    fn headless_resume_restore_skips_session_model_when_cli_provider_or_model_override_present() {
        let metadata = serde_json::json!({
            "provider": "openrouter",
            "model": "anthropic/claude-sonnet-4",
            "primaryAgentId": "architect",
            "primaryAgentPrompt": "You are the architect profile"
        });

        let restore_plan = headless_resume_restore_from_metadata(&metadata, None, true);

        assert!(restore_plan.apply_primary_agent);
        assert_eq!(restore_plan.primary_agent_id.as_deref(), Some("architect"));
        assert_eq!(
            restore_plan.primary_agent_prompt.as_deref(),
            Some("You are the architect profile")
        );
        assert!(restore_plan.restore_model.is_none());
    }

    #[test]
    fn apply_headless_resume_metadata_updates_startup_selection() {
        let mut startup = crate::config::cli::StartupSelection {
            provider: Some("openai".to_string()),
            model: Some("gpt-5.3-codex".to_string()),
            primary_agent_id: Some("coder".to_string()),
            primary_agent_prompt: Some("You are coder".to_string()),
        };
        let metadata = serde_json::json!({
            "provider": "openrouter",
            "model": "anthropic/claude-sonnet-4",
            "primaryAgentId": "architect",
            "primaryAgentPrompt": "You are the architect profile"
        });

        let restore_model = apply_headless_resume_metadata(&mut startup, &metadata, None, false);

        assert_eq!(startup.provider.as_deref(), Some("openai"));
        assert_eq!(startup.model.as_deref(), Some("gpt-5.3-codex"));
        assert_eq!(startup.primary_agent_id.as_deref(), Some("architect"));
        assert_eq!(
            startup.primary_agent_prompt.as_deref(),
            Some("You are the architect profile")
        );
        assert_eq!(
            restore_model,
            Some((
                "openrouter".to_string(),
                "anthropic/claude-sonnet-4".to_string()
            ))
        );
    }

    #[test]
    fn apply_headless_resume_metadata_respects_explicit_cli_overrides() {
        let mut startup = crate::config::cli::StartupSelection {
            provider: Some("openai".to_string()),
            model: Some("gpt-5.3-codex".to_string()),
            primary_agent_id: Some("coder".to_string()),
            primary_agent_prompt: Some("You are coder".to_string()),
        };
        let metadata = serde_json::json!({
            "provider": "openrouter",
            "model": "anthropic/claude-sonnet-4",
            "primaryAgentId": "architect",
            "primaryAgentPrompt": "You are the architect profile"
        });

        let restore_model =
            apply_headless_resume_metadata(&mut startup, &metadata, Some("coder"), true);

        assert_eq!(startup.provider.as_deref(), Some("openai"));
        assert_eq!(startup.model.as_deref(), Some("gpt-5.3-codex"));
        assert_eq!(startup.primary_agent_id.as_deref(), Some("coder"));
        assert_eq!(
            startup.primary_agent_prompt.as_deref(),
            Some("You are coder")
        );
        assert!(restore_model.is_none());
    }

    #[test]
    fn apply_headless_legacy_primary_agent_prompt_fallback_uses_resolved_prompt_when_available() {
        let mut startup = crate::config::cli::StartupSelection {
            provider: Some("openrouter".to_string()),
            model: Some("anthropic/claude-sonnet-4".to_string()),
            primary_agent_id: Some("architect".to_string()),
            primary_agent_prompt: None,
        };

        apply_headless_legacy_primary_agent_prompt_fallback(
            &mut startup,
            Some("architect"),
            Some("stale prompt".to_string()),
            Some("resolved prompt".to_string()),
        );

        assert_eq!(
            startup.primary_agent_prompt.as_deref(),
            Some("resolved prompt")
        );
    }

    #[test]
    fn apply_headless_legacy_primary_agent_prompt_fallback_reuses_previous_prompt_for_same_agent() {
        let mut startup = crate::config::cli::StartupSelection {
            provider: Some("openrouter".to_string()),
            model: Some("anthropic/claude-sonnet-4".to_string()),
            primary_agent_id: Some("architect".to_string()),
            primary_agent_prompt: None,
        };

        apply_headless_legacy_primary_agent_prompt_fallback(
            &mut startup,
            Some("architect"),
            Some("You are the architect profile".to_string()),
            None,
        );

        assert_eq!(
            startup.primary_agent_prompt.as_deref(),
            Some("You are the architect profile")
        );
    }

    #[test]
    fn headless_resume_context_preserves_session_id_and_history() {
        let mut session = ava_types::Session::new();
        session.add_message(ava_types::Message::new(ava_types::Role::User, "first"));
        session.add_message(ava_types::Message::new(
            ava_types::Role::Assistant,
            "second",
        ));

        let (session_id, history) = headless_resume_context(Some(&session));

        assert_eq!(session_id, Some(session.id));
        assert_eq!(history, session.messages);
    }

    #[test]
    fn update_headless_resume_context_from_session_replaces_resume_history_and_session_id() {
        let mut resume_session_id = None;
        let mut resume_history = vec![ava_types::Message::new(ava_types::Role::User, "stale")];

        let mut session = ava_types::Session::new();
        session.add_message(ava_types::Message::new(ava_types::Role::User, "fresh"));
        session.add_message(ava_types::Message::new(
            ava_types::Role::Assistant,
            "response",
        ));

        update_headless_resume_context_from_session(
            &mut resume_session_id,
            &mut resume_history,
            &session,
        );

        assert_eq!(resume_session_id, Some(session.id));
        assert_eq!(resume_history, session.messages);
    }

    #[test]
    fn watcher_detects_comment_directive_lines() {
        let content = "// ava: fix this function\n# ava: add tests\n-- ava: update docs";
        let directives = watch::extract_comment_directives(content);
        assert_eq!(
            directives,
            vec![
                "fix this function".to_string(),
                "add tests".to_string(),
                "update docs".to_string(),
            ]
        );
    }

    #[test]
    fn comment_directive_ignores_non_directive_lines() {
        let content =
            "// TODO: not a trigger\nlet x = 1;\n#ava missing space\n* ava: markdown list";
        let directives = watch::extract_comment_directives(content);
        assert!(directives.is_empty());
    }

    #[test]
    fn watcher_ignore_paths_filters_internal_dirs() {
        assert!(watch::should_ignore_watch_path(Path::new(
            "/repo/.git/config"
        )));
        assert!(watch::should_ignore_watch_path(Path::new(
            "/repo/.ava/state.json"
        )));
        assert!(watch::should_ignore_watch_path(Path::new(
            "/repo/target/debug/app"
        )));
        assert!(watch::should_ignore_watch_path(Path::new(
            "/repo/node_modules/pkg/index.js"
        )));
        assert!(!watch::should_ignore_watch_path(Path::new(
            "/repo/src/lib.rs"
        )));
    }

    #[test]
    fn watcher_trigger_event_kind_accepts_modify_and_create() {
        use notify::event::{CreateKind, EventKind, ModifyKind};

        assert!(watch::is_trigger_event_kind(&EventKind::Modify(
            ModifyKind::Any
        )));
        assert!(watch::is_trigger_event_kind(&EventKind::Create(
            CreateKind::File
        )));
        assert!(!watch::is_trigger_event_kind(&EventKind::Create(
            CreateKind::Folder
        )));
    }

    #[test]
    fn test_parse_steering_with_bang_prefix() {
        let msg = input::parse_stdin_message("!stop, use trait objects instead").unwrap();
        assert_eq!(msg.tier, MessageTier::Steering);
        assert_eq!(msg.text, "stop, use trait objects instead");
    }

    #[test]
    fn test_parse_plain_text_defaults_to_steering() {
        let msg = input::parse_stdin_message("do something different").unwrap();
        assert_eq!(msg.tier, MessageTier::Steering);
        assert_eq!(msg.text, "do something different");
    }

    #[test]
    fn test_parse_follow_up_with_gt_prefix() {
        let msg = input::parse_stdin_message(">also check the tests when done").unwrap();
        assert_eq!(msg.tier, MessageTier::FollowUp);
        assert_eq!(msg.text, "also check the tests when done");
    }

    #[test]
    fn test_parse_follow_up_with_space() {
        let msg = input::parse_stdin_message("> run tests after").unwrap();
        assert_eq!(msg.tier, MessageTier::FollowUp);
        assert_eq!(msg.text, "run tests after");
    }

    #[test]
    fn test_parse_post_complete_without_group() {
        let msg = input::parse_stdin_message(">>review the final code").unwrap();
        assert_eq!(msg.tier, MessageTier::PostComplete { group: 1 });
        assert_eq!(msg.text, "review the final code");
    }

    #[test]
    fn test_parse_post_complete_with_group() {
        let msg = input::parse_stdin_message(">>2 commit everything").unwrap();
        assert_eq!(msg.tier, MessageTier::PostComplete { group: 2 });
        assert_eq!(msg.text, "commit everything");
    }

    #[test]
    fn test_parse_post_complete_group_with_spaces() {
        let msg = input::parse_stdin_message(">>  3  final review").unwrap();
        assert_eq!(msg.tier, MessageTier::PostComplete { group: 3 });
        assert_eq!(msg.text, "final review");
    }

    #[test]
    fn test_parse_empty_line_returns_none() {
        assert!(input::parse_stdin_message("").is_none());
        assert!(input::parse_stdin_message("   ").is_none());
    }

    #[test]
    fn test_parse_empty_after_prefix_returns_none() {
        assert!(input::parse_stdin_message("!").is_none());
        assert!(input::parse_stdin_message("! ").is_none());
        assert!(input::parse_stdin_message(">").is_none());
        assert!(input::parse_stdin_message(">>").is_none());
    }

    #[test]
    fn test_parse_json_steering() {
        let msg =
            input::parse_json_stdin_message(r#"{"tier": "steering", "text": "change approach"}"#)
                .unwrap();
        assert_eq!(msg.tier, MessageTier::Steering);
        assert_eq!(msg.text, "change approach");
    }

    #[test]
    fn test_parse_json_follow_up() {
        let msg = input::parse_json_stdin_message(r#"{"tier": "follow-up", "text": "run tests"}"#)
            .unwrap();
        assert_eq!(msg.tier, MessageTier::FollowUp);
        assert_eq!(msg.text, "run tests");
    }

    #[test]
    fn test_parse_json_accepts_canonical_queue_command_names() {
        let steer =
            input::parse_json_stdin_message(r#"{"tier": "steer_agent", "text": "pivot"}"#).unwrap();
        assert_eq!(steer.tier, MessageTier::Steering);

        let follow =
            input::parse_json_stdin_message(r#"{"tier": "follow_up_agent", "text": "run tests"}"#)
                .unwrap();
        assert_eq!(follow.tier, MessageTier::FollowUp);

        let post = input::parse_json_stdin_message(
            r#"{"tier": "post_complete_agent", "text": "review", "group": 4}"#,
        )
        .unwrap();
        assert_eq!(post.tier, MessageTier::PostComplete { group: 4 });
    }

    #[test]
    fn test_parse_json_post_complete_with_group() {
        let msg = input::parse_json_stdin_message(
            r#"{"tier": "post-complete", "text": "commit", "group": 3}"#,
        )
        .unwrap();
        assert_eq!(msg.tier, MessageTier::PostComplete { group: 3 });
        assert_eq!(msg.text, "commit");
    }

    #[test]
    fn test_parse_json_defaults_group_to_1() {
        let msg = input::parse_json_stdin_message(r#"{"tier": "post-complete", "text": "review"}"#)
            .unwrap();
        assert_eq!(msg.tier, MessageTier::PostComplete { group: 1 });
    }

    #[test]
    fn test_parse_json_accepts_group_zero() {
        let msg = input::parse_json_stdin_message(
            r#"{"tier": "post_complete_agent", "text": "review", "group": 0}"#,
        )
        .unwrap();
        assert_eq!(msg.tier, MessageTier::PostComplete { group: 0 });
    }

    #[test]
    fn test_parse_json_defaults_tier_to_steering() {
        let msg = input::parse_json_stdin_message(r#"{"text": "urgent change"}"#).unwrap();
        assert_eq!(msg.tier, MessageTier::Steering);
    }

    #[test]
    fn test_parse_json_empty_text_returns_none() {
        assert!(input::parse_json_stdin_message(r#"{"tier": "steering", "text": ""}"#).is_none());
    }

    #[test]
    fn test_parse_json_invalid_json_returns_none() {
        assert!(input::parse_json_stdin_message("not json at all").is_none());
    }

    #[test]
    fn test_parse_json_missing_text_returns_none() {
        assert!(input::parse_json_stdin_message(r#"{"tier": "steering"}"#).is_none());
    }

    #[test]
    fn test_parse_json_unknown_tier_returns_none() {
        assert!(
            input::parse_json_stdin_message(r#"{"tier": "unknown", "text": "hello"}"#).is_none()
        );
    }

    #[test]
    fn test_parse_json_invalid_group_returns_none() {
        assert!(input::parse_json_stdin_message(
            r#"{"tier": "post_complete_agent", "text": "hello", "group": "oops"}"#,
        )
        .is_none());
    }

    #[test]
    fn test_parse_json_overflow_group_returns_none() {
        assert!(input::parse_json_stdin_message(
            r#"{"tier": "post_complete_agent", "text": "hello", "group": 4294967296}"#,
        )
        .is_none());
    }

    #[test]
    fn test_populate_follow_up_from_cli() {
        let cli = CliArgs {
            follow_up: vec!["run tests".to_string(), "check compilation".to_string()],
            later: vec![],
            later_group: vec![],
            ..default_cli()
        };
        let (mut queue, tx) = MessageQueue::new();
        input::populate_queue_from_cli(&cli, &tx);
        drop(tx);
        queue.poll();
        assert_eq!(queue.pending_count(), (0, 2, 0));
        let msgs = queue.drain_follow_up();
        assert_eq!(msgs, vec!["run tests", "check compilation"]);
    }

    #[test]
    fn test_populate_later_auto_groups() {
        let cli = CliArgs {
            follow_up: vec![],
            later: vec!["review code".to_string(), "commit if clean".to_string()],
            later_group: vec![],
            ..default_cli()
        };
        let (mut queue, tx) = MessageQueue::new();
        input::populate_queue_from_cli(&cli, &tx);
        drop(tx);
        queue.poll();
        assert_eq!(queue.pending_count(), (0, 0, 2));

        let (g1, msgs1) = queue.next_post_complete_group().unwrap();
        assert_eq!(g1, 1);
        assert_eq!(msgs1, vec!["review code"]);

        let (g2, msgs2) = queue.next_post_complete_group().unwrap();
        assert_eq!(g2, 2);
        assert_eq!(msgs2, vec!["commit if clean"]);
    }

    #[test]
    fn test_populate_later_group_explicit() {
        let cli = CliArgs {
            follow_up: vec![],
            later: vec![],
            later_group: vec![
                "3".to_string(),
                "final step".to_string(),
                "1".to_string(),
                "first step".to_string(),
            ],
            ..default_cli()
        };
        let (mut queue, tx) = MessageQueue::new();
        input::populate_queue_from_cli(&cli, &tx);
        drop(tx);
        queue.poll();

        let (g1, msgs1) = queue.next_post_complete_group().unwrap();
        assert_eq!(g1, 1);
        assert_eq!(msgs1, vec!["first step"]);

        let (g3, msgs3) = queue.next_post_complete_group().unwrap();
        assert_eq!(g3, 3);
        assert_eq!(msgs3, vec!["final step"]);
    }

    #[test]
    fn test_populate_mixed_flags() {
        let cli = CliArgs {
            follow_up: vec!["follow".to_string()],
            later: vec!["later".to_string()],
            later_group: vec!["5".to_string(), "explicit".to_string()],
            ..default_cli()
        };
        let (mut queue, tx) = MessageQueue::new();
        input::populate_queue_from_cli(&cli, &tx);
        drop(tx);
        queue.poll();
        assert_eq!(queue.pending_count(), (0, 1, 2));
    }

    fn default_cli() -> CliArgs {
        use clap::Parser;
        CliArgs::parse_from([
            "ava",
            "test-goal",
            "--headless",
            "--provider",
            "mock",
            "--model",
            "test",
        ])
    }
}
