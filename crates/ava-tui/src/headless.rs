use crate::config::cli::CliArgs;
use ava_agent::stack::{AgentStack, AgentStackConfig};
use ava_agent::AgentEvent;
use ava_commander::{Budget, Commander, CommanderConfig, CommanderEvent, Task, TaskType};
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
    let goal = cli
        .goal
        .as_ref()
        .ok_or_else(|| eyre!("No goal provided. Usage: ava \"your goal here\""))?
        .clone();

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

    let stack = AgentStack::new(AgentStackConfig {
        data_dir,
        provider,
        model,
        max_turns: cli.max_turns,
        yolo: cli.yolo,
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
        stack.run(&goal_owned, max_turns, Some(tx), cancel).await
    });

    if json_mode {
        while let Some(event) = rx.recv().await {
            let json = match &event {
                AgentEvent::Token(t) => serde_json::json!({"type": "token", "content": t}),
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
                AgentEvent::ToolStats(_) => {}
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

async fn run_multi_agent(cli: CliArgs, goal: &str) -> Result<()> {
    let data_dir = dirs::home_dir().unwrap_or_default().join(".ava");

    let (provider, model) = cli.resolve_provider_model().await?;
    if provider.is_none() {
        return Err(eyre!(crate::config::cli::NO_PROVIDER_ERROR));
    }

    // Build an AgentStack to get a resolved provider
    let stack = AgentStack::new(AgentStackConfig {
        data_dir,
        provider,
        model,
        max_turns: cli.max_turns,
        yolo: cli.yolo,
        ..Default::default()
    })
    .await?;

    let provider = resolve_provider(&stack).await?;

    let platform = Arc::new(StandardPlatform);
    let mut commander = Commander::new(CommanderConfig {
        budget: Budget {
            max_tokens: 128_000,
            max_turns: cli.max_turns,
            max_cost_usd: 10.0,
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
