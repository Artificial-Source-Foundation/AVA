use crate::config::cli::CliArgs;
use ava_agent::stack::{AgentStack, AgentStackConfig};
use ava_agent::AgentEvent;
use ava_commander::{Budget, Commander, CommanderConfig, CommanderEvent, Task, TaskType, Workflow, WorkflowExecutor};
use ava_llm::provider::LLMProvider;
use ava_platform::StandardPlatform;
use color_eyre::eyre::{eyre, Result};
use std::collections::HashMap;
use std::sync::Arc;
use tokio::sync::mpsc;
use tokio_util::sync::CancellationToken;
use tracing::instrument;

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

    let (tx, mut rx) = mpsc::unbounded_channel();
    let cancel = CancellationToken::new();

    let cancel_clone = cancel.clone();
    tokio::spawn(async move {
        tokio::signal::ctrl_c().await.ok();
        eprintln!("\nAborting...");
        cancel_clone.cancel();
    });

    let json_mode = cli.json;
    let goal_owned = goal.to_string();
    let max_turns = cli.max_turns;
    let handle = tokio::spawn(async move {
        stack.run(&goal_owned, max_turns, Some(tx), cancel, Vec::new()).await
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
                CommanderEvent::PhaseStarted { phase_index, phase_count, phase_name, role } => {
                    eprintln!("[workflow] Phase {}/{}: {} ({})", phase_index + 1, phase_count, phase_name, role);
                }
                CommanderEvent::PhaseCompleted { phase_index, phase_name, turns, output_preview } => {
                    eprintln!("[workflow] Phase {} ({}) complete — {} turns", phase_index + 1, phase_name, turns);
                    if !output_preview.is_empty() {
                        eprintln!("[workflow]   preview: {}", output_preview);
                    }
                }
                CommanderEvent::IterationStarted { iteration, max_iterations } => {
                    eprintln!("[workflow] Iteration {}/{}", iteration, max_iterations);
                }
                CommanderEvent::WorkflowComplete { phases_completed, total_phases, iterations, total_turns } => {
                    eprintln!(
                        "[workflow] Complete: {}/{} phases, {} iterations, {} turns",
                        phases_completed, total_phases, iterations, total_turns
                    );
                }
                CommanderEvent::WorkerToken { token, .. } => {
                    print!("{token}");
                }
                CommanderEvent::WorkerProgress { turn, .. } => {
                    eprintln!("[workflow] turn {turn}");
                }
                _ => {
                    // Forward other commander events in JSON for debugging
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
    let mut commander = Commander::new(CommanderConfig {
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

    let worker = commander.delegate(task)?;

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
        commander.coordinate(vec![worker], cancel, tx).await
    });

    // Stream events
    while let Some(event) = rx.recv().await {
        if json_mode {
            let json = serde_json::to_string(&event).unwrap_or_default();
            println!("{json}");
        } else {
            match &event {
                CommanderEvent::WorkerStarted { lead, task_description, .. } => {
                    eprintln!("[commander] worker started: {lead} — {task_description}");
                }
                CommanderEvent::WorkerProgress { turn, max_turns, .. } => {
                    eprintln!("[commander] turn {turn}/{max_turns}");
                }
                CommanderEvent::WorkerToken { token, .. } => {
                    print!("{token}");
                }
                CommanderEvent::WorkerCompleted { success, turns, .. } => {
                    eprintln!("[commander] worker completed: success={success}, turns={turns}");
                }
                CommanderEvent::WorkerFailed { error, .. } => {
                    eprintln!("[commander] worker failed: {error}");
                }
                CommanderEvent::AllComplete { total_workers, succeeded, failed } => {
                    eprintln!(
                        "[commander] all complete: {succeeded}/{total_workers} succeeded, {failed} failed"
                    );
                }
                CommanderEvent::Summary { total_workers, succeeded, failed, total_turns } => {
                    eprintln!(
                        "[commander] summary: {succeeded}/{total_workers} workers, {total_turns} turns, {failed} failures"
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
            stack.run(&goal, max_turns, Some(tx), agent_cancel, Vec::new()).await
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
