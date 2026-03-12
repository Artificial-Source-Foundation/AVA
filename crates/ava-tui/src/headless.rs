use crate::config::cli::CliArgs;
use ava_agent::message_queue::MessageQueue;
use ava_agent::stack::{AgentStack, AgentStackConfig};
use ava_agent::AgentEvent;
use ava_praxis::{Budget, Director, DirectorConfig, PraxisEvent, Task, TaskType, Workflow, WorkflowExecutor};
use ava_llm::provider::LLMProvider;
use ava_platform::StandardPlatform;
use ava_types::{ImageContent, MessageTier, QueuedMessage};
use color_eyre::eyre::{eyre, Result};
use std::collections::HashMap;
use std::sync::Arc;
use tokio::io::AsyncBufReadExt;
use tokio::sync::mpsc;
use tokio_util::sync::CancellationToken;
use tracing::{debug, instrument};

#[instrument(skip(cli))]
pub async fn run_headless(cli: CliArgs) -> Result<()> {
    // Continuous voice mode in headless
    if cli.voice {
        #[cfg(feature = "voice")]
        return run_voice_loop(cli).await;

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

    if let Some(workflow_name) = cli.workflow.clone() {
        return run_workflow(cli, &goal, &workflow_name).await;
    }

    if cli.multi_agent {
        return run_multi_agent(cli, &goal).await;
    }

    run_single_agent(cli, &goal).await
}

async fn run_single_agent(cli: CliArgs, goal: &str) -> Result<()> {
    let data_dir = dirs::home_dir().unwrap_or_default().join(".ava");

    let (provider, model) = cli.resolve_provider_model().await?;
    if provider.is_none() {
        return Err(eyre!(crate::config::cli::NO_PROVIDER_ERROR));
    }

    let (stack, _question_rx) = AgentStack::new(AgentStackConfig {
        data_dir,
        provider,
        model,
        max_turns: cli.max_turns,
        max_budget_usd: cli.max_budget_usd,
        yolo: cli.auto_approve,
        ..Default::default()
    })
    .await?;

    // Create message queue for mid-stream messaging
    let (message_queue, message_tx) = MessageQueue::new();

    // Pre-populate from CLI flags
    populate_queue_from_cli(&cli, &message_tx);

    // Spawn stdin reader for interactive mid-stream input (unless in JSON mode with no TTY)
    let json_mode = cli.json;
    let cancel = CancellationToken::new();
    let cancel_for_stdin = cancel.clone();
    spawn_stdin_reader(message_tx, json_mode, cancel_for_stdin);

    let (tx, mut rx) = mpsc::unbounded_channel();

    let cancel_clone = cancel.clone();
    tokio::spawn(async move {
        tokio::signal::ctrl_c().await.ok();
        eprintln!("\nAborting...");
        cancel_clone.cancel();
    });

    let goal_owned = goal.to_string();
    let max_turns = cli.max_turns;
    let cli_images = load_cli_images(&cli.image);
    let handle = tokio::spawn(async move {
        stack.run(&goal_owned, max_turns, Some(tx), cancel, Vec::new(), Some(message_queue), cli_images).await
    });

    if json_mode {
        while let Some(event) = rx.recv().await {
            let json = match &event {
                AgentEvent::Token(t) => serde_json::json!({"type": "token", "content": t}),
                AgentEvent::Thinking(t) => serde_json::json!({"type": "thinking", "content": t}),
                AgentEvent::ToolCall(tc) => {
                    serde_json::json!({"type": "tool_call", "tool": tc.name, "arguments": tc.arguments})
                }
                AgentEvent::ToolResult(tr) => {
                    serde_json::json!({"type": "tool_result", "content": tr.content})
                }
                AgentEvent::Progress(p) => serde_json::json!({"type": "progress", "message": p}),
                AgentEvent::Complete(_) => serde_json::json!({"type": "complete"}),
                AgentEvent::Error(e) => serde_json::json!({"type": "error", "message": e}),
                AgentEvent::ToolStats(s) => serde_json::json!({"type": "tool_stats", "stats": s}),
                AgentEvent::TokenUsage { input_tokens, output_tokens, cost_usd } => {
                    serde_json::json!({"type": "token_usage", "input_tokens": input_tokens, "output_tokens": output_tokens, "cost_usd": cost_usd})
                }
                AgentEvent::SubAgentComplete { session_id, messages, description, .. } => {
                    serde_json::json!({"type": "sub_agent_complete", "session_id": session_id, "description": description, "message_count": messages.len()})
                }
            };
            println!("{json}");
        }
    } else {
        while let Some(event) = rx.recv().await {
            match &event {
                AgentEvent::Token(t) => print!("{t}"),
                AgentEvent::ToolCall(tc) => eprintln!("[tool: {}({})]", tc.name, tc.arguments),
                AgentEvent::ToolResult(tr) => eprintln!("[result: {}]", tr.content),
                AgentEvent::Progress(p) => eprintln!("[{p}]"),
                AgentEvent::Complete(_) => break,
                AgentEvent::Thinking(t) => eprintln!("[thinking: {t}]"),
                AgentEvent::ToolStats(_) | AgentEvent::TokenUsage { .. } | AgentEvent::SubAgentComplete { .. } => {}
                AgentEvent::Error(e) => {
                    eprintln!("[error: {e}]");
                    break;
                }
            }
        }
        println!();
    }

    let result = handle.await??;

    if json_mode {
        println!(
            "{}",
            serde_json::json!({
                "type": "summary",
                "success": result.success,
                "turns": result.turns,
            })
        );
    } else {
        eprintln!(
            "[Done] success={}, turns={}",
            result.success, result.turns
        );
    }

    std::process::exit(if result.success { 0 } else { 1 });
}

async fn run_workflow(cli: CliArgs, goal: &str, workflow_name: &str) -> Result<()> {
    let workflow = Workflow::from_name(workflow_name).ok_or_else(|| {
        eyre!(
            "Unknown workflow '{}'. Available: plan-code-review, code-review, plan-code",
            workflow_name
        )
    })?;

    let data_dir = dirs::home_dir().unwrap_or_default().join(".ava");
    let (provider, model) = cli.resolve_provider_model().await?;
    if provider.is_none() {
        return Err(eyre!(crate::config::cli::NO_PROVIDER_ERROR));
    }

    let (stack, _question_rx) = AgentStack::new(AgentStackConfig {
        data_dir,
        provider,
        model,
        max_turns: cli.max_turns,
        max_budget_usd: cli.max_budget_usd,
        yolo: cli.auto_approve,
        ..Default::default()
    })
    .await?;

    let provider = resolve_provider(&stack).await?;
    let platform = Arc::new(StandardPlatform);

    let budget = Budget {
        max_tokens: 128_000,
        max_turns: if cli.max_turns == 0 { 200 } else { cli.max_turns },
        max_cost_usd: if cli.max_budget_usd > 0.0 { cli.max_budget_usd } else { 10.0 },
    };

    let executor = WorkflowExecutor::new(workflow, budget, provider, platform);

    let cancel = CancellationToken::new();
    let cancel_clone = cancel.clone();
    tokio::spawn(async move {
        tokio::signal::ctrl_c().await.ok();
        eprintln!("\nAborting...");
        cancel_clone.cancel();
    });

    let (tx, mut rx) = mpsc::unbounded_channel();
    let json_mode = cli.json;
    let goal_owned = goal.to_string();

    let handle = tokio::spawn(async move {
        executor.execute(&goal_owned, cancel, tx).await
    });

    while let Some(event) = rx.recv().await {
        if json_mode {
            let json = serde_json::to_string(&event).unwrap_or_default();
            println!("{json}");
        } else {
            match &event {
                PraxisEvent::PhaseStarted { phase_index, phase_count, phase_name, role } => {
                    eprintln!("[workflow] Phase {}/{}: {} ({})", phase_index + 1, phase_count, phase_name, role);
                }
                PraxisEvent::PhaseCompleted { phase_index, phase_name, turns, output_preview } => {
                    eprintln!("[workflow] Phase {} ({}) complete — {} turns", phase_index + 1, phase_name, turns);
                    if !output_preview.is_empty() {
                        eprintln!("[workflow]   preview: {}", output_preview);
                    }
                }
                PraxisEvent::IterationStarted { iteration, max_iterations } => {
                    eprintln!("[workflow] Iteration {}/{}", iteration, max_iterations);
                }
                PraxisEvent::WorkflowComplete { phases_completed, total_phases, iterations, total_turns } => {
                    eprintln!(
                        "[workflow] Complete: {}/{} phases, {} iterations, {} turns",
                        phases_completed, total_phases, iterations, total_turns
                    );
                }
                PraxisEvent::WorkerToken { token, .. } => {
                    print!("{token}");
                }
                PraxisEvent::WorkerProgress { turn, .. } => {
                    eprintln!("[workflow] turn {turn}");
                }
                _ => {
                    // Forward other praxis events in JSON for debugging
                }
            }
        }
    }

    let session = handle.await??;
    let success = !session.messages.is_empty();

    if !json_mode {
        println!();
        eprintln!("[Done] success={}, messages={}", success, session.messages.len());
    }

    std::process::exit(if success { 0 } else { 1 });
}

async fn run_multi_agent(cli: CliArgs, goal: &str) -> Result<()> {
    let data_dir = dirs::home_dir().unwrap_or_default().join(".ava");

    let (provider, model) = cli.resolve_provider_model().await?;
    if provider.is_none() {
        return Err(eyre!(crate::config::cli::NO_PROVIDER_ERROR));
    }

    // Build an AgentStack to get a resolved provider
    let (stack, _question_rx) = AgentStack::new(AgentStackConfig {
        data_dir,
        provider,
        model,
        max_turns: cli.max_turns,
        max_budget_usd: cli.max_budget_usd,
        yolo: cli.auto_approve,
        ..Default::default()
    })
    .await?;

    let provider = resolve_provider(&stack).await?;

    let platform = Arc::new(StandardPlatform);
    let mut director = Director::new(DirectorConfig {
        budget: Budget {
            max_tokens: 128_000,
            max_turns: if cli.max_turns == 0 { 200 } else { cli.max_turns },
            max_cost_usd: if cli.max_budget_usd > 0.0 { cli.max_budget_usd } else { 10.0 },
        },
        default_provider: provider,
        domain_providers: HashMap::new(),
        platform: Some(platform),
    });

    let task = Task {
        description: goal.to_string(),
        task_type: TaskType::Simple,
        files: vec![],
    };

    let worker = director.delegate(task)?;

    let cancel = CancellationToken::new();
    let cancel_clone = cancel.clone();
    tokio::spawn(async move {
        tokio::signal::ctrl_c().await.ok();
        eprintln!("\nAborting...");
        cancel_clone.cancel();
    });

    let (tx, mut rx) = mpsc::unbounded_channel();
    let json_mode = cli.json;

    let handle = tokio::spawn(async move {
        director.coordinate(vec![worker], cancel, tx).await
    });

    // Stream events
    while let Some(event) = rx.recv().await {
        if json_mode {
            let json = serde_json::to_string(&event).unwrap_or_default();
            println!("{json}");
        } else {
            match &event {
                PraxisEvent::WorkerStarted { lead, task_description, .. } => {
                    eprintln!("[director] worker started: {lead} — {task_description}");
                }
                PraxisEvent::WorkerProgress { turn, max_turns, .. } => {
                    eprintln!("[director] turn {turn}/{max_turns}");
                }
                PraxisEvent::WorkerToken { token, .. } => {
                    print!("{token}");
                }
                PraxisEvent::WorkerCompleted { success, turns, .. } => {
                    eprintln!("[director] worker completed: success={success}, turns={turns}");
                }
                PraxisEvent::WorkerFailed { error, .. } => {
                    eprintln!("[director] worker failed: {error}");
                }
                PraxisEvent::AllComplete { total_workers, succeeded, failed } => {
                    eprintln!(
                        "[director] all complete: {succeeded}/{total_workers} succeeded, {failed} failed"
                    );
                }
                PraxisEvent::Summary { total_workers, succeeded, failed, total_turns } => {
                    eprintln!(
                        "[director] summary: {succeeded}/{total_workers} workers, {total_turns} turns, {failed} failures"
                    );
                }
                // Workflow events not expected in multi-agent mode
                _ => {}
            }
        }
    }

    let session = handle.await??;
    let success = !session.messages.is_empty();

    if !json_mode {
        println!();
        eprintln!(
            "[Done] success={}, messages={}",
            success,
            session.messages.len()
        );
    }

    std::process::exit(if success { 0 } else { 1 });
}

/// Load images from CLI --image paths. Logs warnings and skips files that fail.
fn load_cli_images(paths: &[String]) -> Vec<ImageContent> {
    let mut images = Vec::new();
    for path_str in paths {
        let path = std::path::Path::new(path_str);
        match ImageContent::from_file(path) {
            Ok(img) => {
                debug!(path = %path.display(), media_type = %img.media_type, "Loaded image for prompt");
                images.push(img);
            }
            Err(e) => {
                eprintln!("[warning] {e}");
            }
        }
    }
    images
}

/// Pre-populate the message queue from CLI flags (--follow-up, --later, --later-group).
fn populate_queue_from_cli(cli: &CliArgs, tx: &mpsc::UnboundedSender<QueuedMessage>) {
    // --follow-up messages → Tier 2
    for msg in &cli.follow_up {
        debug!(text = %msg, "Pre-queuing follow-up message from CLI");
        let _ = tx.send(QueuedMessage {
            text: msg.clone(),
            tier: MessageTier::FollowUp,
        });
    }

    // --later messages → Tier 3, auto-assign sequential groups
    for (i, msg) in cli.later.iter().enumerate() {
        let group = (i + 1) as u32;
        debug!(text = %msg, group, "Pre-queuing post-complete message from CLI");
        let _ = tx.send(QueuedMessage {
            text: msg.clone(),
            tier: MessageTier::PostComplete { group },
        });
    }

    // --later-group N "message" → Tier 3, explicit group
    // Clap gives us pairs: [group, message, group, message, ...]
    let pairs = cli.later_group.chunks(2);
    for chunk in pairs {
        if chunk.len() == 2 {
            if let Ok(group) = chunk[0].parse::<u32>() {
                debug!(text = %chunk[1], group, "Pre-queuing post-complete message (explicit group) from CLI");
                let _ = tx.send(QueuedMessage {
                    text: chunk[1].clone(),
                    tier: MessageTier::PostComplete { group },
                });
            } else {
                eprintln!(
                    "[warning] --later-group: invalid group number '{}', skipping",
                    chunk[0]
                );
            }
        }
    }
}

/// Parse a stdin line into a tier and text.
///
/// Prefix rules:
/// - `!text`  → Steering (Tier 1)
/// - `>text`  → Follow-up (Tier 2)
/// - `>>text` or `>>N text` → Post-complete (Tier 3, optional group N)
/// - plain text → Steering (default, most intuitive for interactive use)
fn parse_stdin_message(line: &str) -> Option<QueuedMessage> {
    let line = line.trim();
    if line.is_empty() {
        return None;
    }

    if let Some(rest) = line.strip_prefix(">>") {
        // Post-complete: >>N text or >>text
        let rest = rest.trim_start();
        // Try to parse a leading number as group
        let mut chars = rest.chars().peekable();
        let mut digits = String::new();
        while let Some(&c) = chars.peek() {
            if c.is_ascii_digit() {
                digits.push(c);
                chars.next();
            } else {
                break;
            }
        }
        if !digits.is_empty() {
            let group: u32 = digits.parse().unwrap_or(1);
            let text: String = chars.collect();
            let text = text.trim().to_string();
            if text.is_empty() {
                return None;
            }
            Some(QueuedMessage {
                text,
                tier: MessageTier::PostComplete { group },
            })
        } else {
            // No group number — default to group 1
            let text = rest.to_string();
            if text.is_empty() {
                return None;
            }
            Some(QueuedMessage {
                text,
                tier: MessageTier::PostComplete { group: 1 },
            })
        }
    } else if let Some(rest) = line.strip_prefix('>') {
        // Follow-up
        let text = rest.trim().to_string();
        if text.is_empty() {
            return None;
        }
        Some(QueuedMessage {
            text,
            tier: MessageTier::FollowUp,
        })
    } else if let Some(rest) = line.strip_prefix('!') {
        // Steering
        let text = rest.trim().to_string();
        if text.is_empty() {
            return None;
        }
        Some(QueuedMessage {
            text,
            tier: MessageTier::Steering,
        })
    } else {
        // Plain text → Steering (default)
        Some(QueuedMessage {
            text: line.to_string(),
            tier: MessageTier::Steering,
        })
    }
}

/// Parse a JSON stdin line into a QueuedMessage.
///
/// Expected format: `{"tier": "steering"|"follow-up"|"post-complete", "text": "...", "group": N}`
fn parse_json_stdin_message(line: &str) -> Option<QueuedMessage> {
    let v: serde_json::Value = serde_json::from_str(line).ok()?;
    let text = v.get("text")?.as_str()?.to_string();
    if text.is_empty() {
        return None;
    }
    let tier_str = v.get("tier").and_then(|t| t.as_str()).unwrap_or("steering");
    let tier = match tier_str {
        "steering" => MessageTier::Steering,
        "follow-up" | "followup" | "follow_up" => MessageTier::FollowUp,
        "post-complete" | "postcomplete" | "post_complete" => {
            let group = v.get("group").and_then(|g| g.as_u64()).unwrap_or(1) as u32;
            MessageTier::PostComplete { group }
        }
        _ => MessageTier::Steering,
    };
    Some(QueuedMessage { text, tier })
}

/// Spawn an async stdin reader that parses lines and sends them to the message queue.
/// Handles both plain-text and JSON modes. Gracefully handles EOF (piped input).
fn spawn_stdin_reader(
    tx: mpsc::UnboundedSender<QueuedMessage>,
    json_mode: bool,
    cancel: CancellationToken,
) {
    tokio::spawn(async move {
        let stdin = tokio::io::stdin();
        let reader = tokio::io::BufReader::new(stdin);
        let mut lines = reader.lines();

        loop {
            tokio::select! {
                _ = cancel.cancelled() => break,
                result = lines.next_line() => {
                    match result {
                        Ok(Some(line)) => {
                            let msg = if json_mode {
                                parse_json_stdin_message(&line)
                            } else {
                                parse_stdin_message(&line)
                            };
                            if let Some(msg) = msg {
                                debug!(tier = ?msg.tier, text = %msg.text, "Received stdin message");
                                if tx.send(msg).is_err() {
                                    // Channel closed (agent finished)
                                    break;
                                }
                            }
                        }
                        Ok(None) => break, // EOF
                        Err(_) => break,    // stdin error
                    }
                }
            }
        }
    });
}

async fn resolve_provider(stack: &AgentStack) -> Result<Arc<dyn LLMProvider>> {
    let (provider_name, model_name) = stack.current_model().await;
    let provider = stack
        .router
        .route_required(&provider_name, &model_name)
        .await?;
    Ok(provider)
}

/// Continuous voice loop for headless mode: record -> transcribe -> run agent -> repeat.
#[cfg(feature = "voice")]
async fn run_voice_loop(cli: CliArgs) -> Result<()> {
    use crate::event::AppEvent;

    let data_dir = dirs::home_dir().unwrap_or_default().join(".ava");
    let (provider, model) = cli.resolve_provider_model().await?;
    if provider.is_none() {
        return Err(eyre!(crate::config::cli::NO_PROVIDER_ERROR));
    }

    let voice_config = ava_config::VoiceConfig::default();
    let transcriber = crate::transcribe::create_transcriber(&voice_config).await?;

    eprintln!("[voice] Continuous voice mode. Speak into your microphone. Ctrl+C to exit.");

    let cancel = CancellationToken::new();
    let cancel_clone = cancel.clone();
    tokio::spawn(async move {
        tokio::signal::ctrl_c().await.ok();
        eprintln!("\n[voice] Stopping...");
        cancel_clone.cancel();
    });

    loop {
        if cancel.is_cancelled() {
            break;
        }

        eprintln!("[voice] Listening...");

        // Record
        let (audio_tx, mut audio_rx) = mpsc::unbounded_channel();
        let mut recorder = crate::audio::AudioRecorder::start(
            audio_tx,
            voice_config.silence_threshold,
            voice_config.silence_duration_secs,
            voice_config.max_duration_secs,
        )
        .map_err(|e| eyre!("{e}"))?;

        // Wait for silence or max duration
        loop {
            match audio_rx.recv().await {
                Some(AppEvent::VoiceSilenceDetected) => break,
                Some(AppEvent::VoiceError(e)) => {
                    eprintln!("[voice] Error: {e}");
                    break;
                }
                Some(_) => {} // amplitude events
                None => break,
            }
        }

        let wav = recorder.stop().map_err(|e| eyre!("{e}"))?;
        eprintln!("[voice] Transcribing...");

        let text = match transcriber
            .transcribe(wav, voice_config.language.as_deref())
            .await
        {
            Ok(t) => t,
            Err(e) => {
                eprintln!("[voice] Transcription error: {e}");
                continue;
            }
        };

        let text = text.trim().to_string();
        if text.is_empty() {
            eprintln!("[voice] (no speech detected)");
            continue;
        }

        eprintln!("[voice] Goal: {text}");

        // Run agent
        let (stack, _question_rx) = AgentStack::new(AgentStackConfig {
            data_dir: data_dir.clone(),
            provider: provider.clone(),
            model: model.clone(),
            max_turns: cli.max_turns,
            max_budget_usd: cli.max_budget_usd,
            yolo: cli.auto_approve,
            ..Default::default()
        })
        .await?;

        let (tx, mut rx) = mpsc::unbounded_channel();
        let agent_cancel = CancellationToken::new();

        let goal = text.clone();
        let max_turns = cli.max_turns;
        let handle = tokio::spawn(async move {
            stack.run(&goal, max_turns, Some(tx), agent_cancel, Vec::new(), None, Vec::new()).await
        });

        while let Some(event) = rx.recv().await {
            match &event {
                AgentEvent::Token(t) => print!("{t}"),
                AgentEvent::Complete(_) => break,
                AgentEvent::Error(e) => {
                    eprintln!("[error: {e}]");
                    break;
                }
                _ => {}
            }
        }
        println!();

        let result = handle.await??;
        eprintln!(
            "[voice] Done — success={}, turns={}",
            result.success, result.turns
        );
    }

    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    // ── parse_stdin_message tests ────────────────────────────────────────────

    #[test]
    fn test_parse_steering_with_bang_prefix() {
        let msg = parse_stdin_message("!stop, use trait objects instead").unwrap();
        assert_eq!(msg.tier, MessageTier::Steering);
        assert_eq!(msg.text, "stop, use trait objects instead");
    }

    #[test]
    fn test_parse_plain_text_defaults_to_steering() {
        let msg = parse_stdin_message("do something different").unwrap();
        assert_eq!(msg.tier, MessageTier::Steering);
        assert_eq!(msg.text, "do something different");
    }

    #[test]
    fn test_parse_follow_up_with_gt_prefix() {
        let msg = parse_stdin_message(">also check the tests when done").unwrap();
        assert_eq!(msg.tier, MessageTier::FollowUp);
        assert_eq!(msg.text, "also check the tests when done");
    }

    #[test]
    fn test_parse_follow_up_with_space() {
        let msg = parse_stdin_message("> run tests after").unwrap();
        assert_eq!(msg.tier, MessageTier::FollowUp);
        assert_eq!(msg.text, "run tests after");
    }

    #[test]
    fn test_parse_post_complete_without_group() {
        let msg = parse_stdin_message(">>review the final code").unwrap();
        assert_eq!(msg.tier, MessageTier::PostComplete { group: 1 });
        assert_eq!(msg.text, "review the final code");
    }

    #[test]
    fn test_parse_post_complete_with_group() {
        let msg = parse_stdin_message(">>2 commit everything").unwrap();
        assert_eq!(msg.tier, MessageTier::PostComplete { group: 2 });
        assert_eq!(msg.text, "commit everything");
    }

    #[test]
    fn test_parse_post_complete_group_with_spaces() {
        let msg = parse_stdin_message(">>  3  final review").unwrap();
        assert_eq!(msg.tier, MessageTier::PostComplete { group: 3 });
        assert_eq!(msg.text, "final review");
    }

    #[test]
    fn test_parse_empty_line_returns_none() {
        assert!(parse_stdin_message("").is_none());
        assert!(parse_stdin_message("   ").is_none());
    }

    #[test]
    fn test_parse_empty_after_prefix_returns_none() {
        assert!(parse_stdin_message("!").is_none());
        assert!(parse_stdin_message("! ").is_none());
        assert!(parse_stdin_message(">").is_none());
        assert!(parse_stdin_message(">>").is_none());
    }

    // ── parse_json_stdin_message tests ───────────────────────────────────────

    #[test]
    fn test_parse_json_steering() {
        let msg = parse_json_stdin_message(r#"{"tier": "steering", "text": "change approach"}"#).unwrap();
        assert_eq!(msg.tier, MessageTier::Steering);
        assert_eq!(msg.text, "change approach");
    }

    #[test]
    fn test_parse_json_follow_up() {
        let msg = parse_json_stdin_message(r#"{"tier": "follow-up", "text": "run tests"}"#).unwrap();
        assert_eq!(msg.tier, MessageTier::FollowUp);
        assert_eq!(msg.text, "run tests");
    }

    #[test]
    fn test_parse_json_post_complete_with_group() {
        let msg = parse_json_stdin_message(r#"{"tier": "post-complete", "text": "commit", "group": 3}"#).unwrap();
        assert_eq!(msg.tier, MessageTier::PostComplete { group: 3 });
        assert_eq!(msg.text, "commit");
    }

    #[test]
    fn test_parse_json_defaults_group_to_1() {
        let msg = parse_json_stdin_message(r#"{"tier": "post-complete", "text": "review"}"#).unwrap();
        assert_eq!(msg.tier, MessageTier::PostComplete { group: 1 });
    }

    #[test]
    fn test_parse_json_defaults_tier_to_steering() {
        let msg = parse_json_stdin_message(r#"{"text": "urgent change"}"#).unwrap();
        assert_eq!(msg.tier, MessageTier::Steering);
    }

    #[test]
    fn test_parse_json_empty_text_returns_none() {
        assert!(parse_json_stdin_message(r#"{"tier": "steering", "text": ""}"#).is_none());
    }

    #[test]
    fn test_parse_json_invalid_json_returns_none() {
        assert!(parse_json_stdin_message("not json at all").is_none());
    }

    #[test]
    fn test_parse_json_missing_text_returns_none() {
        assert!(parse_json_stdin_message(r#"{"tier": "steering"}"#).is_none());
    }

    // ── populate_queue_from_cli tests ────────────────────────────────────────

    #[test]
    fn test_populate_follow_up_from_cli() {
        let cli = CliArgs {
            follow_up: vec!["run tests".to_string(), "check compilation".to_string()],
            later: vec![],
            later_group: vec![],
            ..default_cli()
        };
        let (mut queue, tx) = MessageQueue::new();
        populate_queue_from_cli(&cli, &tx);
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
        populate_queue_from_cli(&cli, &tx);
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
            later_group: vec!["3".to_string(), "final step".to_string(), "1".to_string(), "first step".to_string()],
            ..default_cli()
        };
        let (mut queue, tx) = MessageQueue::new();
        populate_queue_from_cli(&cli, &tx);
        drop(tx);
        queue.poll();

        // BTreeMap orders by key, so group 1 comes first
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
        populate_queue_from_cli(&cli, &tx);
        drop(tx);
        queue.poll();
        assert_eq!(queue.pending_count(), (0, 1, 2));
    }

    /// Minimal CliArgs for tests — only the mid-stream fields matter.
    fn default_cli() -> CliArgs {
        use clap::Parser;
        CliArgs::parse_from(["ava", "test-goal", "--headless", "--provider", "mock", "--model", "test"])
    }
}
