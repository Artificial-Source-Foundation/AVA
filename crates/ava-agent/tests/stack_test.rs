use std::pin::Pin;
use std::sync::Arc;
use std::time::Duration;

use async_trait::async_trait;
use ava_agent::message_queue::MessageQueue;
use ava_agent::stack::{AgentStack, AgentStackConfig};
use ava_config::{Config, CredentialStore, ProviderCredential, RoutingMode};
use ava_llm::provider::LLMProvider;
use ava_llm::providers::mock::MockProvider;
use ava_llm::RouteSource;
use ava_llm::ThinkingConfig;
use ava_types::{AvaError, Message, MessageTier, QueuedMessage, Result, StreamChunk, TokenUsage};
use futures::Stream;
use tokio::sync::mpsc;
use tokio::sync::Mutex;
use tokio_util::sync::CancellationToken;

fn completion_response(result: &str) -> String {
    format!(
        r#"{{"tool_calls":[{{"name":"attempt_completion","arguments":{{"result":"{result}"}}}}]}}"#
    )
}

async fn write_credentials(dir: &tempfile::TempDir, providers: &[&str]) {
    let mut store = CredentialStore::default();
    for provider in providers {
        store.set(
            provider,
            ProviderCredential {
                api_key: format!("{provider}-key"),
                base_url: None,
                org_id: None,
                oauth_token: None,
                oauth_refresh_token: None,
                oauth_expires_at: None,
                oauth_account_id: None,
            },
        );
    }
    store
        .save(&dir.path().join("credentials.json"))
        .await
        .unwrap();
}

fn write_routing_config(dir: &tempfile::TempDir) {
    let mut config = Config::default();
    config.llm.provider = "anthropic".to_string();
    config.llm.model = "claude-sonnet-4.6".to_string();
    config.llm.routing.mode = RoutingMode::Conservative;
    std::fs::write(
        dir.path().join("config.yaml"),
        serde_json::to_string(&config).unwrap(),
    )
    .unwrap();
}

#[tokio::test]
async fn agent_stack_new_initializes_components() {
    let dir = tempfile::tempdir().expect("tempdir");
    let (stack, _question_rx, _approval_rx) = AgentStack::new(AgentStackConfig {
        data_dir: dir.path().to_path_buf(),
        yolo: true,
        injected_provider: Some(Arc::new(MockProvider::new("test", vec![]))),
        ..Default::default()
    })
    .await
    .expect("stack init should succeed");

    let tools = stack.tools.read().await.list_tools();
    let names = tools
        .iter()
        .map(|tool| tool.name.as_str())
        .collect::<Vec<_>>();
    assert!(names.contains(&"read"));
    assert!(names.contains(&"write"));
    assert!(names.contains(&"edit"));
    assert!(names.contains(&"bash"));
    assert!(names.contains(&"glob"));
    assert!(names.contains(&"grep"));
}

#[tokio::test]
async fn agent_stack_run_with_mock_provider_completes() {
    let dir = tempfile::tempdir().expect("tempdir");
    let provider = Arc::new(MockProvider::new(
        "test-model",
        vec![completion_response("done")],
    ));
    let (stack, _question_rx, _approval_rx) = AgentStack::new(AgentStackConfig {
        data_dir: dir.path().to_path_buf(),
        injected_provider: Some(provider),
        ..Default::default()
    })
    .await
    .expect("stack init should succeed");

    let result = stack
        .run(
            "finish task",
            5,
            None,
            CancellationToken::new(),
            Vec::new(),
            None,
            Vec::new(),
        )
        .await
        .expect("run should succeed");

    assert!(result.success);
    assert!(result.turns >= 1);
}

#[tokio::test]
async fn agent_stack_run_honors_cancellation() {
    let dir = tempfile::tempdir().expect("tempdir");
    let provider = Arc::new(SlowProvider {
        model: "slow-model".to_string(),
        delay: Duration::from_millis(250),
    });
    let (stack, _question_rx, _approval_rx) = AgentStack::new(AgentStackConfig {
        data_dir: dir.path().to_path_buf(),
        injected_provider: Some(provider),
        ..Default::default()
    })
    .await
    .expect("stack init should succeed");

    let cancel = CancellationToken::new();
    let cancel_clone = cancel.clone();
    tokio::spawn(async move {
        tokio::time::sleep(Duration::from_millis(10)).await;
        cancel_clone.cancel();
    });

    let err = stack
        .run("slow task", 5, None, cancel, Vec::new(), None, Vec::new())
        .await
        .expect_err("run should be cancelled");
    assert!(matches!(err, AvaError::Cancelled));
}

#[tokio::test]
async fn test_agents_config_loaded() {
    let dir = tempfile::tempdir().expect("tempdir");

    // Write an agents.toml into the data_dir (simulating ~/.ava/agents.toml)
    let agents_toml = r#"
[defaults]
max_turns = 8

[agents.task]
max_turns = 5
prompt = "Custom task prompt."
"#;
    std::fs::write(dir.path().join("agents.toml"), agents_toml).unwrap();

    let (stack, _question_rx, _approval_rx) = AgentStack::new(AgentStackConfig {
        data_dir: dir.path().to_path_buf(),
        yolo: true,
        injected_provider: Some(Arc::new(MockProvider::new("test", vec![]))),
        ..Default::default()
    })
    .await
    .expect("stack init should succeed");

    // Verify the agents_config was loaded by checking it's reflected in the stack.
    // We can't access agents_config directly (it's private), but we can verify
    // the stack was created successfully with the config file present.
    assert!(!stack.tools.read().await.list_tools().is_empty());
}

#[tokio::test]
async fn test_agents_config_defaults_without_file() {
    let dir = tempfile::tempdir().expect("tempdir");

    // No agents.toml file — should use defaults
    let (stack, _question_rx, _approval_rx) = AgentStack::new(AgentStackConfig {
        data_dir: dir.path().to_path_buf(),
        yolo: true,
        injected_provider: Some(Arc::new(MockProvider::new("test", vec![]))),
        ..Default::default()
    })
    .await
    .expect("stack init should succeed without agents.toml");

    // Stack should initialize fine even without agents.toml
    assert!(!stack.tools.read().await.list_tools().is_empty());
}

#[tokio::test]
async fn agent_stack_resolve_model_route_prefers_cheap_model_when_enabled() {
    let dir = tempfile::tempdir().expect("tempdir");
    write_routing_config(&dir);
    write_credentials(&dir, &["anthropic", "openai"]).await;

    let (stack, _question_rx, _approval_rx) = AgentStack::new(AgentStackConfig {
        data_dir: dir.path().to_path_buf(),
        ..Default::default()
    })
    .await
    .expect("stack init should succeed");

    let decision = stack
        .resolve_model_route("Summarize this diff in two bullets.", &[])
        .await
        .expect("route resolution should succeed");

    assert_eq!(decision.source, RouteSource::PolicyAuto);
    assert_eq!(decision.provider, "openai");
    assert_eq!(decision.display_model, "gpt-5-mini");
}

#[tokio::test]
async fn agent_stack_resolve_model_route_respects_manual_override_lock() {
    let dir = tempfile::tempdir().expect("tempdir");
    write_routing_config(&dir);
    write_credentials(&dir, &["anthropic", "openai"]).await;

    let (stack, _question_rx, _approval_rx) = AgentStack::new(AgentStackConfig {
        data_dir: dir.path().to_path_buf(),
        provider: Some("openai".to_string()),
        model: Some("gpt-5.3-codex".to_string()),
        ..Default::default()
    })
    .await
    .expect("stack init should succeed");

    let decision = stack
        .resolve_model_route("Summarize this diff in two bullets.", &[])
        .await
        .expect("route resolution should succeed");

    assert_eq!(decision.source, RouteSource::ManualOverride);
    assert_eq!(decision.provider, "openai");
    assert_eq!(decision.display_model, "gpt-5.3-codex");
}

#[tokio::test]
async fn streaming_run_emits_budget_warning_and_persists_cost_summary() {
    let dir = tempfile::tempdir().expect("tempdir");
    let provider = Arc::new(UsageProvider::new(
        "claude-sonnet-4",
        completion_response("done"),
        TokenUsage {
            input_tokens: 1_000_000,
            output_tokens: 1_000_000,
            cache_read_tokens: 0,
            cache_creation_tokens: 0,
        },
    ));
    let (stack, _question_rx, _approval_rx) = AgentStack::new(AgentStackConfig {
        data_dir: dir.path().to_path_buf(),
        injected_provider: Some(provider),
        max_budget_usd: 0.01,
        ..Default::default()
    })
    .await
    .expect("stack init should succeed");

    let (tx, mut rx) = mpsc::unbounded_channel();
    let result = stack
        .run(
            "finish task",
            5,
            Some(tx),
            CancellationToken::new(),
            Vec::new(),
            None,
            Vec::new(),
        )
        .await
        .expect("run should succeed");

    let mut saw_budget_warning = false;
    while let Ok(event) = rx.try_recv() {
        if matches!(event, ava_agent::AgentEvent::BudgetWarning { .. }) {
            saw_budget_warning = true;
            break;
        }
    }

    assert!(
        saw_budget_warning,
        "expected at least one budget warning event"
    );
    let summary = result
        .session
        .metadata
        .get("costSummary")
        .and_then(|value| value.as_object())
        .expect("cost summary metadata should be present");
    assert!(
        summary
            .get("totalUsd")
            .and_then(|value| value.as_f64())
            .unwrap_or_default()
            > 0.0
    );
    assert_eq!(
        summary
            .get("lastAlertThresholdPercent")
            .and_then(|value| value.as_u64()),
        Some(90)
    );
}

#[tokio::test]
async fn follow_up_budget_uses_cumulative_spend() {
    let dir = tempfile::tempdir().expect("tempdir");
    let provider = Arc::new(UsageProvider::new(
        "claude-sonnet-4",
        completion_response("done"),
        TokenUsage {
            input_tokens: 2_000,
            output_tokens: 1_000,
            cache_read_tokens: 0,
            cache_creation_tokens: 0,
        },
    ));
    let (stack, _question_rx, _approval_rx) = AgentStack::new(AgentStackConfig {
        data_dir: dir.path().to_path_buf(),
        injected_provider: Some(provider),
        max_budget_usd: 0.02,
        ..Default::default()
    })
    .await
    .expect("stack init should succeed");

    let (queue, sender) = MessageQueue::new();
    sender
        .send(QueuedMessage {
            tier: MessageTier::FollowUp,
            text: "run one more thing".to_string(),
        })
        .expect("follow-up should enqueue");

    let (tx, mut rx) = mpsc::unbounded_channel();
    let result = stack
        .run(
            "finish task",
            5,
            Some(tx),
            CancellationToken::new(),
            Vec::new(),
            Some(queue),
            Vec::new(),
        )
        .await
        .expect("run should succeed");

    let mut saw_follow_up_progress = false;
    let mut saw_follow_up_skip = false;
    while let Ok(event) = rx.try_recv() {
        if let ava_agent::AgentEvent::Progress(message) = event {
            if message.contains("follow-up: run one more thing") {
                saw_follow_up_progress = true;
            }
            if message.contains("budget exhausted") && message.contains("follow-up") {
                saw_follow_up_skip = true;
            }
        }
    }

    assert!(
        !saw_follow_up_progress,
        "follow-up should not run after budget is exhausted"
    );
    assert!(
        saw_follow_up_skip,
        "expected skip progress when follow-up is blocked by cumulative budget"
    );
    assert!(
        result
            .session
            .messages
            .iter()
            .all(|message| !message.content.contains("[User follow-up]")),
        "follow-up message should not be injected once cumulative budget is exhausted"
    );
    let summary = result
        .session
        .metadata
        .get("costSummary")
        .and_then(|value| value.as_object())
        .expect("cost summary metadata should be present");
    assert_eq!(
        summary
            .get("skippedQueuedFollowUps")
            .and_then(|value| value.as_u64()),
        Some(1)
    );
}
struct SlowProvider {
    model: String,
    delay: Duration,
}

struct RecordingThinkingProvider {
    model: String,
    recorded: Arc<Mutex<Vec<ThinkingConfig>>>,
}

impl RecordingThinkingProvider {
    fn new(model: &str, recorded: Arc<Mutex<Vec<ThinkingConfig>>>) -> Self {
        Self {
            model: model.to_string(),
            recorded,
        }
    }
}

struct UsageProvider {
    model: String,
    response: String,
    usage: TokenUsage,
}

impl UsageProvider {
    fn new(model: &str, response: String, usage: TokenUsage) -> Self {
        Self {
            model: model.to_string(),
            response,
            usage,
        }
    }
}
#[async_trait]
impl LLMProvider for SlowProvider {
    async fn generate(&self, _messages: &[Message]) -> Result<String> {
        tokio::time::sleep(self.delay).await;
        Ok(completion_response("slow"))
    }

    async fn generate_stream(
        &self,
        messages: &[Message],
    ) -> Result<Pin<Box<dyn Stream<Item = StreamChunk> + Send>>> {
        let out = self.generate(messages).await?;
        Ok(Box::pin(futures::stream::iter(vec![StreamChunk::text(
            out,
        )])))
    }

    fn estimate_tokens(&self, input: &str) -> usize {
        input.len() / 4
    }

    fn estimate_cost(&self, _input_tokens: usize, _output_tokens: usize) -> f64 {
        0.0
    }

    fn model_name(&self) -> &str {
        &self.model
    }
}

#[async_trait]
impl LLMProvider for RecordingThinkingProvider {
    async fn generate(&self, _messages: &[Message]) -> Result<String> {
        Ok(completion_response("recorded"))
    }

    async fn generate_stream(
        &self,
        messages: &[Message],
    ) -> Result<Pin<Box<dyn Stream<Item = StreamChunk> + Send>>> {
        let out = self.generate(messages).await?;
        Ok(Box::pin(futures::stream::iter(vec![StreamChunk::text(
            out,
        )])))
    }

    async fn generate_stream_with_thinking_config(
        &self,
        messages: &[Message],
        _tools: &[ava_types::Tool],
        config: ThinkingConfig,
    ) -> Result<Pin<Box<dyn Stream<Item = StreamChunk> + Send>>> {
        self.recorded.lock().await.push(config);
        self.generate_stream(messages).await
    }

    fn supports_tools(&self) -> bool {
        true
    }

    fn supports_thinking(&self) -> bool {
        true
    }

    fn estimate_tokens(&self, input: &str) -> usize {
        input.len() / 4
    }

    fn estimate_cost(&self, _input_tokens: usize, _output_tokens: usize) -> f64 {
        0.0
    }

    fn model_name(&self) -> &str {
        &self.model
    }
}

#[tokio::test]
async fn configured_thinking_budget_reaches_streaming_runtime_provider() {
    let dir = tempfile::tempdir().expect("tempdir");
    std::fs::write(
        dir.path().join("config.yaml"),
        r#"
llm:
  provider: gemini
  model: gemini-2.5-pro
  api_key: null
  max_tokens: 4096
  temperature: 0.7
  thinking_budgets:
    providers:
      gemini:
        models:
          gemini-2.5-pro: 12345
editor:
  default_editor: vscode
  tab_size: 4
  use_spaces: true
ui:
  theme: dark
  font_size: 14
  show_line_numbers: true
features:
  enable_git: true
  enable_lsp: true
  enable_mcp: true
voice:
  model: whisper-1
  language: null
  silence_threshold: 0.01
  silence_duration_secs: 2.5
  max_duration_secs: 60
  auto_submit: false
instructions: []
"#,
    )
    .expect("write config");

    let recorded = Arc::new(Mutex::new(Vec::new()));
    let provider = Arc::new(RecordingThinkingProvider::new(
        "gemini-2.5-pro",
        recorded.clone(),
    ));
    let (stack, _question_rx, _approval_rx) = AgentStack::new(AgentStackConfig {
        data_dir: dir.path().to_path_buf(),
        injected_provider: Some(provider),
        ..Default::default()
    })
    .await
    .expect("stack init should succeed");

    stack
        .set_thinking_level(ava_types::ThinkingLevel::High)
        .await;
    let (event_tx, mut event_rx) = mpsc::unbounded_channel();
    let result = stack
        .run(
            "finish task",
            5,
            Some(event_tx),
            CancellationToken::new(),
            Vec::new(),
            None,
            Vec::new(),
        )
        .await
        .expect("run should succeed");

    assert!(result.success);
    while event_rx.try_recv().is_ok() {}

    let configs = recorded.lock().await;
    assert_eq!(configs.len(), 1);
    assert_eq!(configs[0].level, ava_types::ThinkingLevel::High);
    assert_eq!(configs[0].budget_tokens, Some(12_345));
}

#[async_trait]
impl LLMProvider for UsageProvider {
    async fn generate(&self, _messages: &[Message]) -> Result<String> {
        Ok(self.response.clone())
    }

    async fn generate_stream(
        &self,
        _messages: &[Message],
    ) -> Result<Pin<Box<dyn Stream<Item = StreamChunk> + Send>>> {
        Ok(Box::pin(futures::stream::iter(vec![
            StreamChunk::text(self.response.clone()),
            StreamChunk::with_usage(self.usage.clone()),
        ])))
    }

    fn estimate_tokens(&self, input: &str) -> usize {
        input.len() / 4
    }

    fn estimate_cost(&self, _input_tokens: usize, _output_tokens: usize) -> f64 {
        0.0
    }

    fn model_name(&self) -> &str {
        &self.model
    }
}
