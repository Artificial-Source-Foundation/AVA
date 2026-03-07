use ava_agent::stack::{AgentStack, AgentStackConfig};
use ava_agent::AgentEvent;
use ava_llm::providers::mock::MockProvider;
use color_eyre::Result;
use std::sync::Arc;
use tokio_util::sync::CancellationToken;

#[tokio::main]
async fn main() -> Result<()> {
    color_eyre::install()?;

    let args: Vec<String> = std::env::args().collect();
    let real = args.iter().any(|a| a == "--real");
    let provider = args
        .iter()
        .position(|a| a == "--provider")
        .and_then(|i| args.get(i + 1))
        .map(|s| s.as_str())
        .unwrap_or("openrouter");
    let model = args
        .iter()
        .position(|a| a == "--model")
        .and_then(|i| args.get(i + 1))
        .map(|s| s.as_str())
        .unwrap_or("anthropic/claude-sonnet-4");

    if real {
        println!("Running agent with REAL provider: {provider} / {model}");
        let data_dir = dirs::home_dir().unwrap().join(".ava");

        let stack = AgentStack::new(AgentStackConfig {
            data_dir,
            provider: Some(provider.to_string()),
            model: Some(model.to_string()),
            max_turns: 1,
            yolo: true,
            ..Default::default()
        })
        .await?;

        let cancel = CancellationToken::new();

        let result = stack
            .run("Say hello in one sentence.", 1, None, cancel)
            .await?;
        println!(
            "Result: success={}, turns={}",
            result.success, result.turns
        );
    } else {
        println!("Running agent with mock provider...");
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
        let _ = AgentEvent::Progress("smoke".to_string());
        let result = stack.run("Say hello", 3, None, cancel).await?;
        println!(
            "Smoke test result: success={}, turns={}",
            result.success, result.turns
        );
    }

    Ok(())
}
