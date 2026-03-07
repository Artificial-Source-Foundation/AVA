use crate::config::cli::CliArgs;
use ava_agent::stack::{AgentStack, AgentStackConfig};
use ava_agent::AgentEvent;
use color_eyre::eyre::{eyre, Result};
use tokio::sync::mpsc;
use tokio_util::sync::CancellationToken;

pub async fn run_headless(cli: CliArgs) -> Result<()> {
    let goal = cli
        .goal
        .ok_or_else(|| eyre!("No goal provided. Usage: ava \"your goal here\""))?;

    let data_dir = dirs::home_dir()
        .unwrap_or_default()
        .join(".ava");

    let stack = AgentStack::new(AgentStackConfig {
        data_dir,
        provider: cli.provider,
        model: cli.model,
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
    let handle = tokio::spawn(async move {
        stack.run(&goal, cli.max_turns, Some(tx), cancel).await
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
