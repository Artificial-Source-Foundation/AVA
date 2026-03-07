use ava_agent::stack::{AgentStack, AgentStackConfig};
use ava_agent::AgentEvent;
use ava_llm::providers::mock::MockProvider;
use color_eyre::Result;
use std::sync::Arc;
use tokio_util::sync::CancellationToken;

#[tokio::main]
async fn main() -> Result<()> {
    color_eyre::install()?;
    let temp_dir = tempfile::tempdir()?;

    let stack = AgentStack::new(AgentStackConfig {
        data_dir: temp_dir.path().to_path_buf(),
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

    let cancel = CancellationToken::new();

    println!("Running agent with mock provider...");
    let result = stack.run("Say hello", 3, None, cancel).await?;
    let _ = AgentEvent::Progress("smoke".to_string());
    println!(
        "\nSmoke test result: success={}, turns={}",
        result.success, result.turns
    );
    Ok(())
}
