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

    let (stack, _question_rx, approval_rx, _plan_rx) = AgentStack::new(AgentStackConfig {
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
                AgentEvent::Token(t) => serde_json::json!({"type": "text", "content": t}),
                AgentEvent::Thinking(t) => {
                    serde_json::json!({"type": "thinking", "content": t})
                }
                AgentEvent::ToolCall(tc) => {
                    serde_json::json!({"type": "tool_call", "tool": tc.name, "args": tc.arguments})
                }
                AgentEvent::ToolResult(tr) => {
                    serde_json::json!({"type": "tool_result", "tool": tr.call_id, "content": tr.content, "is_error": tr.is_error})
                }
                AgentEvent::Progress(p) => serde_json::json!({"type": "progress", "message": p}),
                AgentEvent::Complete(_) => serde_json::json!({"type": "complete"}),
                AgentEvent::Error(e) => {
                    // Errors go to stderr in JSON mode too
                    eprintln!("{}", serde_json::json!({"type": "error", "message": e}));
                    continue;
                }
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
                AgentEvent::DiffPreview {
                    file,
                    diff_text,
                    additions,
                    deletions,
                } => {
                    serde_json::json!({"type": "diff_preview", "file": file.display().to_string(), "diff": diff_text, "additions": additions, "deletions": deletions})
                }
            };
            println!("{json}");
        }
    } else {
        let mut turn: u32 = 0;
        let mut in_text = false;
        while let Some(event) = rx.recv().await {
            match &event {
                AgentEvent::Token(t) => {
                    if !in_text {
                        in_text = true;
                    }
                    print!("{t}");
                }
                AgentEvent::ToolCall(tc) => {
                    if in_text {
                        println!();
                        in_text = false;
                    }
                    // Compact summary: tool name + first meaningful arg
                    let summary = summarize_tool_args(&tc.name, &tc.arguments);
                    eprintln!("[tool: {}] {}", tc.name, summary);
                }
                AgentEvent::ToolResult(_) => {
                    // Tool results are internal — omit from text output for cleaner scripting
                }
                AgentEvent::Progress(p) => {
                    if in_text {
                        println!();
                        in_text = false;
                    }
                    // Progress messages include turn transitions
                    if p.starts_with("Turn ") || p.starts_with("turn ") {
                        turn += 1;
                        eprintln!("[turn {turn}]");
                    } else {
                        eprintln!("[{p}]");
                    }
                }
                AgentEvent::Complete(_) => break,
                AgentEvent::Thinking(_) => {
                    // Thinking content is internal — omit from text output
                }
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
                AgentEvent::DiffPreview {
                    file,
                    additions,
                    deletions,
                    ..
                } => {
                    if in_text {
                        println!();
                        in_text = false;
                    }
                    eprintln!(
                        "[diff: {} +{} -{}]",
                        file.display(),
                        additions,
                        deletions
                    );
                }
                AgentEvent::Error(e) => {
                    if in_text {
                        println!();
                        in_text = false;
                    }
                    eprintln!("[error: {e}]");
                    break;
                }
            }
        }
        if in_text {
            println!();
        }
    }

    let result = handle.await??;
    let cost_summary = crate::session_summary::cost_summary(&result.session);
    let route_summary = crate::session_summary::route_summary(&result.session);

    if json_mode {
        println!(
            "{}",
            serde_json::json!({
                "type": "complete",
                "success": result.success,
                "turns": result.turns,
                "cost": cost_summary.as_ref().and_then(|s| if s.total_usd == 0.0 { None } else { Some(s.total_usd) }),
                "input_tokens": cost_summary.as_ref().map(|s| s.input_tokens),
                "output_tokens": cost_summary.as_ref().map(|s| s.output_tokens),
                "routing": route_summary,
            })
        );
    } else {
        // Print routing info at the start of the summary
        if let Some(ref route) = route_summary {
            eprintln!("[routing: {route}]");
        }
        let spend = cost_summary.map(|summary| {
            let token_part = format!(
                "tokens={}/{} in/out",
                summary.input_tokens, summary.output_tokens
            );
            if summary.total_usd == 0.0 {
                // Subscription provider (Copilot, ChatGPT) — no per-token cost
                format!(" {token_part}")
            } else if let Some(budget) = summary.budget_usd {
                if budget > 0.0 {
                    format!(" cost=${:.2}/{budget:.2} {token_part}", summary.total_usd)
                } else {
                    format!(" cost=${:.2} {token_part}", summary.total_usd)
                }
            } else {
                format!(" cost=${:.2} {token_part}", summary.total_usd)
            }
        });
        eprintln!(
            "[Done] success={}, turns={}{}",
            result.success,
            result.turns,
            spend.unwrap_or_default(),
        );
    }

    std::process::exit(if result.success { 0 } else { 1 });
}

/// Produce a compact one-line summary of tool arguments for headless text output.
/// Extracts the most meaningful field (e.g., path for read/write, command for bash).
fn summarize_tool_args(tool_name: &str, args: &serde_json::Value) -> String {
    let Some(obj) = args.as_object() else {
        return args.to_string();
    };
    // Pick the most informative field based on tool name
    let key = match tool_name {
        "read" | "write" | "edit" | "glob" => "file_path",
        "bash" => "command",
        "grep" => "pattern",
        _ => {
            // Fall back to first string field
            if let Some((k, v)) = obj.iter().find(|(_, v)| v.is_string()) {
                return format!("{}={}", k, truncate_str(v.as_str().unwrap_or(""), 80));
            }
            return String::new();
        }
    };
    match obj.get(key).and_then(|v| v.as_str()) {
        Some(val) => truncate_str(val, 120),
        None => String::new(),
    }
}

/// Truncate a string to `max_len` chars, appending "..." if truncated.
fn truncate_str(s: &str, max_len: usize) -> String {
    if s.len() <= max_len {
        s.to_string()
    } else {
        format!("{}...", &s[..max_len])
    }
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
