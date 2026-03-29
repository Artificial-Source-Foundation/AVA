use ava_agent::stack::{AgentStack, AgentStackConfig};
use ava_agent::AgentEvent;
use ava_llm::providers::mock::MockProvider;
use color_eyre::Result;
use std::sync::Arc;
use tokio::sync::mpsc;
use tokio_util::sync::CancellationToken;

// For real provider testing, use:
//   cargo run --bin ava -- "Say hello" --headless --provider openrouter --model anthropic/claude-sonnet-4

#[tokio::main]
async fn main() -> Result<()> {
    color_eyre::install()?;

    println!("Running agent with mock provider...");
    let temp_dir = tempfile::tempdir()?;

    let (stack, _question_rx, _approval_rx, _plan_rx) = AgentStack::new(AgentStackConfig {
        data_dir: temp_dir.path().to_path_buf(),
        yolo: true,
        injected_provider: Some(Arc::new(MockProvider::new(
            "mock-model",
            vec![
                "Hello from smoke test".to_string(),
                "Continuing smoke test".to_string(),
                "Final smoke response".to_string(),
            ],
        ))),
        max_turns: 3,
        ..Default::default()
    })
    .await?;

    let (tx, mut rx) = mpsc::unbounded_channel();
    let cancel = CancellationToken::new();

    // AgentStack is Send — use tokio::spawn instead of spawn_local
    let handle = tokio::spawn(async move {
        stack
            .run(
                "Say hello",
                3,
                Some(tx),
                cancel,
                Vec::new(),
                None,
                Vec::new(),
                None,
            )
            .await
    });

    while let Some(event) = rx.recv().await {
        match event {
            AgentEvent::Token(t) => print!("{t}"),
            AgentEvent::ToolCall(tc) => eprintln!("[tool: {}]", tc.name),
            AgentEvent::ToolResult(tr) => eprintln!("[result: {}]", tr.content),
            AgentEvent::Progress(p) => eprintln!("[{p}]"),
            AgentEvent::Complete(_) => break,
            AgentEvent::Thinking(_)
            | AgentEvent::BudgetWarning { .. }
            | AgentEvent::ToolStats(_)
            | AgentEvent::TokenUsage { .. }
            | AgentEvent::SubAgentComplete { .. }
            | AgentEvent::DiffPreview { .. }
            | AgentEvent::MCPToolsChanged { .. }
            | AgentEvent::Checkpoint(_)
            | AgentEvent::ContextCompacted { .. }
            | AgentEvent::SnapshotTaken { .. }
            | AgentEvent::PlanStepComplete { .. }
            | AgentEvent::StreamingEditProgress { .. } => {}
            AgentEvent::Error(e) => {
                eprintln!("[error: {e}]");
                break;
            }
        }
    }

    let result = handle.await??;
    println!(
        "\nSmoke test result: success={}, turns={}",
        result.success, result.turns
    );

    Ok(())
}
