use super::input::{populate_queue_from_cli, spawn_stdin_reader};
use super::spawn_auto_approve_requests;
use crate::config::cli::CliArgs;
use ava_agent::message_queue::MessageQueue;
use ava_agent::stack::{AgentStack, AgentStackConfig};
use ava_agent::AgentEvent;
use ava_types::ImageContent;
use color_eyre::eyre::{eyre, Result};
use std::path::Path;
use std::sync::Arc;
use tokio::sync::mpsc;
use tokio_util::sync::CancellationToken;
use tracing::debug;

pub(super) async fn run_single_agent(cli: CliArgs, goal: &str) -> Result<()> {
    let data_dir = dirs::home_dir().unwrap_or_default().join(".ava");
    let runtime_lean = cli.runtime_lean_settings();

    let (provider, model) = cli.resolve_provider_model().await?;
    if provider.is_none() {
        return Err(eyre!(crate::config::cli::NO_PROVIDER_ERROR));
    }

    if runtime_lean.auto_lean {
        tracing::info!(
            goal_chars = goal.len(),
            max_turns = cli.max_turns,
            "auto-lean runtime enabled for simple headless goal"
        );
    }

    let (stack, _question_rx, approval_rx, _plan_rx) = AgentStack::new(AgentStackConfig {
        data_dir,
        provider,
        model,
        max_turns: cli.max_turns,
        max_budget_usd: cli.max_budget_usd,
        yolo: cli.auto_approve,
        include_project_instructions: runtime_lean.include_project_instructions,
        eager_codebase_indexing: runtime_lean.eager_codebase_indexing,
        ..Default::default()
    })
    .await?;
    spawn_auto_approve_requests(approval_rx);

    // Apply thinking level from CLI flag
    let thinking_level = match cli.thinking.as_str() {
        "low" => ava_types::ThinkingLevel::Low,
        "medium" | "med" => ava_types::ThinkingLevel::Medium,
        "high" => ava_types::ThinkingLevel::High,
        "max" | "xhigh" => ava_types::ThinkingLevel::Max,
        _ => ava_types::ThinkingLevel::Off,
    };
    if thinking_level != ava_types::ThinkingLevel::Off {
        if let Err(e) = stack.set_thinking_level(thinking_level).await {
            tracing::warn!("Failed to set thinking level: {e}");
        }
    }

    let (message_queue, message_tx) = MessageQueue::new();
    populate_queue_from_cli(&cli, &message_tx);

    let json_mode = cli.json;
    let show_thinking = thinking_level != ava_types::ThinkingLevel::Off;
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
    let stack = Arc::new(stack);
    let stack_for_run = stack.clone();
    let handle = tokio::spawn(async move {
        stack_for_run
            .run(
                &goal_owned,
                max_turns,
                Some(tx),
                cancel,
                Vec::new(),
                Some(message_queue),
                cli_images,
                None,
            )
            .await
    });

    let mut files_edited = false;
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
                    files_edited = true;
                    serde_json::json!({"type": "diff_preview", "file": file.display().to_string(), "diff": diff_text, "additions": additions, "deletions": deletions})
                }
                AgentEvent::MCPToolsChanged {
                    server_name,
                    tool_count,
                } => {
                    serde_json::json!({"type": "mcp_tools_changed", "server_name": server_name, "tool_count": tool_count})
                }
                AgentEvent::Checkpoint(_)
                | AgentEvent::SnapshotTaken { .. }
                | AgentEvent::PlanStepComplete { .. }
                | AgentEvent::StreamingEditProgress { .. } => continue,
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
                AgentEvent::Thinking(t) => {
                    if show_thinking && !t.is_empty() {
                        if in_text {
                            println!();
                            in_text = false;
                        }
                        // Print thinking to stderr in dim style so it doesn't
                        // pollute stdout (which carries the assistant response).
                        for line in t.lines() {
                            eprintln!("\x1b[2m{line}\x1b[0m");
                        }
                    }
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
                | AgentEvent::SubAgentComplete { .. }
                | AgentEvent::MCPToolsChanged { .. } => {}
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
                    files_edited = true;
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
                AgentEvent::Checkpoint(_)
                | AgentEvent::SnapshotTaken { .. }
                | AgentEvent::PlanStepComplete { .. }
                | AgentEvent::StreamingEditProgress { .. } => {
                    // Checkpoint / snapshot / plan step / streaming edit: handled elsewhere
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

    // Auto-review: when the agent edited files, review and auto-fix issues.
    // Triggered automatically when files were edited, or explicitly with --review.
    if result.success && (files_edited || cli.review) {
        let review_findings = run_post_completion_review(&cli).await;
        if let Some(findings) = review_findings {
            // Re-run the agent to fix the review findings
            eprintln!("\n[review] Auto-fixing issues...");
            let fix_goal = format!(
                "A code review found the following issues in your recent changes. Fix them:\n\n{}",
                findings
            );
            let (fix_tx, mut fix_rx) = mpsc::unbounded_channel();
            let fix_cancel = CancellationToken::new();
            let stack_for_fix = stack.clone();
            let fix_handle = tokio::spawn(async move {
                stack_for_fix
                    .run(
                        &fix_goal,
                        5, // max 5 turns for fixes
                        Some(fix_tx),
                        fix_cancel,
                        Vec::new(),
                        None,
                        Vec::new(),
                        None,
                    )
                    .await
            });

            // Drain fix events
            while let Some(event) = fix_rx.recv().await {
                match &event {
                    AgentEvent::Token(t) => print!("{t}"),
                    AgentEvent::ToolCall(tc) => {
                        let summary = summarize_tool_args(&tc.name, &tc.arguments);
                        eprintln!("[fix: {}] {}", tc.name, summary);
                    }
                    AgentEvent::DiffPreview {
                        file,
                        additions,
                        deletions,
                        ..
                    } => {
                        eprintln!(
                            "[fix-diff: {} +{} -{}]",
                            file.display(),
                            additions,
                            deletions
                        );
                    }
                    AgentEvent::Complete(_) => break,
                    AgentEvent::Error(e) => {
                        eprintln!("[fix-error: {e}]");
                        break;
                    }
                    _ => {}
                }
            }
            println!();

            if let Ok(Ok(fix_result)) = fix_handle.await {
                eprintln!(
                    "[review] Fix pass: success={}, turns={}",
                    fix_result.success, fix_result.turns
                );
            }
        }
    }

    std::process::exit(if result.success { 0 } else { 1 });
}

/// Run a code review on working directory changes after the agent completes.
/// Returns `Some(findings_text)` if actionable issues found, `None` otherwise.
async fn run_post_completion_review(cli: &CliArgs) -> Option<String> {
    use ava_platform::StandardPlatform;
    use ava_praxis::review::{
        build_review_system_prompt, collect_diff, format_text, parse_review_output,
        run_review_agent, DiffMode, Severity,
    };

    eprintln!("\n[review] Running post-completion code review...");

    let review_context = match collect_diff(&DiffMode::Working).await {
        Ok(ctx) if ctx.diff.is_empty() => {
            eprintln!("[review] No changes to review.");
            return None;
        }
        Ok(ctx) => ctx,
        Err(e) => {
            eprintln!("[review] Failed to collect diff: {e}");
            return None;
        }
    };

    eprintln!(
        "[review] {} file(s) changed, {} bytes of diff",
        review_context.stats.len(),
        review_context.diff.len()
    );

    let (provider, model) = match crate::config::cli::resolve_provider_model(
        cli.provider.as_deref(),
        cli.model.as_deref(),
    )
    .await
    {
        Ok(pm) => pm,
        Err(e) => {
            eprintln!("[review] Failed to resolve provider: {e}");
            return None;
        }
    };

    let data_dir = dirs::home_dir().unwrap_or_default().join(".ava");
    let (review_stack, _q, _a, _p) = match AgentStack::new(AgentStackConfig {
        data_dir,
        provider,
        model,
        max_turns: 5,
        yolo: true,
        ..Default::default()
    })
    .await
    {
        Ok(s) => s,
        Err(e) => {
            eprintln!("[review] Failed to create review stack: {e}");
            return None;
        }
    };

    let (provider_name, model_name) = review_stack.current_model().await;
    let resolved_provider = match review_stack
        .router
        .route_required(&provider_name, &model_name)
        .await
    {
        Ok(p) => p,
        Err(e) => {
            eprintln!("[review] Failed to route provider: {e}");
            return None;
        }
    };

    let system_prompt = build_review_system_prompt("bugs");
    let platform = Arc::new(StandardPlatform);

    let output = match run_review_agent(
        resolved_provider,
        platform,
        &review_context,
        &system_prompt,
        5,
    )
    .await
    {
        Ok(o) => o,
        Err(e) => {
            eprintln!("[review] Review agent failed: {e}");
            return None;
        }
    };

    let result = parse_review_output(&output);
    let has_actionable = result
        .issues
        .iter()
        .any(|i| matches!(i.severity, Severity::Critical | Severity::Warning));

    let formatted = format_text(&result);
    eprintln!("\n{formatted}");

    if has_actionable {
        eprintln!("[review] Found actionable issues — auto-fixing...");
        Some(formatted)
    } else {
        eprintln!("[review] No critical issues found.");
        None
    }
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
