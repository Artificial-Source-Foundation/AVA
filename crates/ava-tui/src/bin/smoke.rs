use ava_agent::stack::{AgentStack, AgentStackConfig};
use ava_agent::AgentEvent;
use ava_llm::providers::mock::MockProvider;
use ava_permissions::tags::RiskLevel;
use ava_tools::permission_middleware::ToolApproval;
use color_eyre::eyre::eyre;
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

    let (stack, _question_rx, mut approval_rx, _plan_rx) = AgentStack::new(AgentStackConfig {
        data_dir: temp_dir.path().to_path_buf(),
        yolo: true,
        non_interactive_approvals: true,
        injected_provider: Some(Arc::new(MockProvider::new(
            "mock-model",
            vec![
                r#"{"tool_calls":[{"name":"bash","arguments":{"command":"printf safe-smoke"}}]}"#
                    .to_string(),
                r#"{"tool_calls":[{"name":"bash","arguments":{"command":"rm -rf /tmp/ava-smoke-dangerous-path"}}]}"#
                    .to_string(),
                r#"{"tool_calls":[{"name":"attempt_completion","arguments":{"result":"Smoke flow complete"}}]}"#
                    .to_string(),
            ],
        ))),
        working_dir: Some(temp_dir.path().to_path_buf()),
        max_turns: 4,
        ..Default::default()
    })
    .await?;

    tokio::spawn(async move {
        while let Some(req) = approval_rx.recv().await {
            let decision = match req.inspection.risk_level {
                RiskLevel::Safe | RiskLevel::Low | RiskLevel::Medium => ToolApproval::Allowed,
                RiskLevel::High | RiskLevel::Critical => ToolApproval::Rejected(Some(format!(
                    "Headless mode rejected dangerous action '{}': {} (risk: {:?})",
                    req.call.name, req.inspection.reason, req.inspection.risk_level
                ))),
            };
            let _ = req.reply.send(decision);
        }
    });

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
                None,
            )
            .await
    });

    let mut saw_safe_result = false;
    let mut saw_dangerous_rejection = false;
    while let Some(event) = rx.recv().await {
        match event {
            AgentEvent::Token(t) => print!("{t}"),
            AgentEvent::ToolCall(tc) => eprintln!("[tool: {}]", tc.name),
            AgentEvent::ToolResult(tr) => {
                if !tr.is_error && tr.content.contains("safe-smoke") {
                    saw_safe_result = true;
                }
                if tr.is_error
                    && tr
                        .content
                        .contains("Headless mode rejected dangerous action")
                {
                    saw_dangerous_rejection = true;
                }
                eprintln!("[result: {}]", tr.content)
            }
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
            | AgentEvent::StreamingEditProgress { .. }
            | AgentEvent::StreamSilenceWarning { .. }
            | AgentEvent::RetryHeartbeat { .. }
            | AgentEvent::FallbackModelSwitch { .. } => {}
            AgentEvent::Error(e) => {
                eprintln!("[error: {e}]");
                break;
            }
        }
    }

    let result = handle.await??;
    if !result.success {
        return Err(eyre!("smoke run did not complete successfully"));
    }
    if !saw_safe_result {
        return Err(eyre!(
            "smoke run did not observe the safe unattended tool path"
        ));
    }
    if !saw_dangerous_rejection {
        return Err(eyre!(
            "smoke run did not observe the dangerous non-interactive rejection path"
        ));
    }
    println!(
        "\nSmoke test result: success={}, turns={}",
        result.success, result.turns
    );

    Ok(())
}
