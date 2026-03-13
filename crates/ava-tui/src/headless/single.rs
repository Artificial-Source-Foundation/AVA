use super::input::{populate_queue_from_cli, spawn_stdin_reader};
use super::spawn_auto_approve_requests;
use crate::config::cli::CliArgs;
use ava_agent::message_queue::MessageQueue;
use ava_agent::stack::{AgentStack, AgentStackConfig};
use ava_agent::AgentEvent;
use ava_types::ImageContent;
use color_eyre::eyre::{eyre, Result};
use std::path::Path;
use tokio::sync::mpsc;
use tokio_util::sync::CancellationToken;
use tracing::debug;

pub(super) async fn run_single_agent(cli: CliArgs, goal: &str) -> Result<()> {
    let data_dir = dirs::home_dir().unwrap_or_default().join(".ava");

    let (provider, model) = cli.resolve_provider_model().await?;
    if provider.is_none() {
        return Err(eyre!(crate::config::cli::NO_PROVIDER_ERROR));
    }

    let (stack, _question_rx, approval_rx) = AgentStack::new(AgentStackConfig {
        data_dir,
        provider,
        model,
        max_turns: cli.max_turns,
        max_budget_usd: cli.max_budget_usd,
        yolo: cli.auto_approve,
        ..Default::default()
    })
    .await?;
    spawn_auto_approve_requests(approval_rx);

    let (message_queue, message_tx) = MessageQueue::new();
    populate_queue_from_cli(&cli, &message_tx);

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
        stack
            .run(
                &goal_owned,
                max_turns,
                Some(tx),
                cancel,
                Vec::new(),
                Some(message_queue),
                cli_images,
            )
            .await
    });

    if json_mode {
        while let Some(event) = rx.recv().await {
            let json = match &event {
                AgentEvent::Token(t) => serde_json::json!({"type": "token", "content": t}),
                AgentEvent::Thinking(t) => {
                    serde_json::json!({"type": "thinking", "content": t})
                }
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
                AgentEvent::TokenUsage {
                    input_tokens,
                    output_tokens,
                    cost_usd,
                } => {
                    serde_json::json!({"type": "token_usage", "input_tokens": input_tokens, "output_tokens": output_tokens, "cost_usd": cost_usd})
                }
                AgentEvent::BudgetWarning {
                    threshold_percent,
                    current_cost_usd,
                    max_budget_usd,
                } => {
                    serde_json::json!({"type": "budget_warning", "threshold_percent": threshold_percent, "current_cost_usd": current_cost_usd, "max_budget_usd": max_budget_usd})
                }
                AgentEvent::SubAgentComplete {
                    session_id,
                    messages,
                    description,
                    ..
                } => {
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
                AgentEvent::BudgetWarning {
                    threshold_percent,
                    current_cost_usd,
                    max_budget_usd,
                } => eprintln!(
                    "[budget warning: ${current_cost_usd:.2}/${max_budget_usd:.2} ({threshold_percent}%)]"
                ),
                AgentEvent::ToolStats(_)
                | AgentEvent::TokenUsage { .. }
                | AgentEvent::SubAgentComplete { .. } => {}
                AgentEvent::Error(e) => {
                    eprintln!("[error: {e}]");
                    break;
                }
            }
        }
        println!();
    }

    let result = handle.await??;
    let cost_summary = crate::session_summary::cost_summary(&result.session);
    let route_summary = crate::session_summary::route_summary(&result.session);

    if json_mode {
        println!(
            "{}",
            serde_json::json!({
                "type": "summary",
                "success": result.success,
                "turns": result.turns,
                "costSummary": cost_summary.map(|summary| serde_json::json!({
                    "totalUsd": summary.total_usd,
                    "budgetUsd": summary.budget_usd,
                    "inputTokens": summary.input_tokens,
                    "outputTokens": summary.output_tokens,
                })),
                "routing": route_summary,
            })
        );
    } else {
        let spend = cost_summary.map(|summary| match summary.budget_usd {
            Some(budget) if budget > 0.0 => {
                format!(
                    " cost=${:.2}/${budget:.2} tokens={} in/{} out",
                    summary.total_usd, summary.input_tokens, summary.output_tokens
                )
            }
            _ => format!(
                " cost=${:.2} tokens={} in/{} out",
                summary.total_usd, summary.input_tokens, summary.output_tokens
            ),
        });
        let route = route_summary.map(|summary| format!(" route={summary}"));
        eprintln!(
            "[Done] success={}, turns={}{}{}",
            result.success,
            result.turns,
            spend.unwrap_or_default(),
            route.unwrap_or_default(),
        );
    }

    std::process::exit(if result.success { 0 } else { 1 });
}

pub(super) fn load_cli_images(paths: &[String]) -> Vec<ImageContent> {
    let mut images = Vec::new();
    for path_str in paths {
        let path = Path::new(path_str);
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
