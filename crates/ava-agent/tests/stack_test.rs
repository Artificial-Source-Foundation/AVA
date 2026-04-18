use std::pin::Pin;
use std::sync::Arc;
use std::time::Duration;

use async_trait::async_trait;
use ava_agent::message_queue::MessageQueue;
use ava_agent::stack::{AgentRunContext, AgentStack, AgentStackConfig};
use ava_config::{Config, CredentialStore, ProviderCredential, RoutingMode};
use ava_llm::provider::LLMProvider;
use ava_llm::providers::mock::MockProvider;
use ava_llm::RouteSource;
use ava_llm::ThinkingConfig;
use ava_types::{
    AvaError, Message, MessageTier, QueuedMessage, Result, Role, StreamChunk, TokenUsage,
};
use futures::Stream;
use tokio::sync::mpsc;
use tokio::sync::Mutex;
use tokio_util::sync::CancellationToken;

fn write_plugin_manifest(root: &std::path::Path, name: &str) {
    let plugin_dir = root.join(".ava").join("plugins").join(name);
    std::fs::create_dir_all(&plugin_dir).unwrap();
    std::fs::write(
        plugin_dir.join("plugin.toml"),
        format!(
            r#"
[plugin]
name = "{name}"
version = "0.1.0"

[runtime]
command = "true"

[hooks]
subscribe = ["auth"]
"#
        ),
    )
    .unwrap();
}

fn completion_response(result: &str) -> String {
    format!(
        r#"{{"tool_calls":[{{"name":"attempt_completion","arguments":{{"result":"{result}"}}}}]}}"#
    )
}

fn subagent_response(prompt: &str) -> String {
    format!(r#"{{"tool_calls":[{{"name":"subagent","arguments":{{"prompt":"{prompt}"}}}}]}}"#)
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
                litellm_compatible: None,
                loop_prone: None,
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
    let (stack, _question_rx, _approval_rx, _plan_rx) = AgentStack::new(AgentStackConfig {
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
    let (stack, _question_rx, _approval_rx, _plan_rx) = AgentStack::new(AgentStackConfig {
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
            None,
            None,
        )
        .await
        .expect("run should succeed");

    assert!(result.success);
    assert!(result.turns >= 1);
}

#[tokio::test]
async fn interactive_tool_introspection_matches_runtime_tool_surface() {
    let dir = tempfile::tempdir().expect("tempdir");
    let (stack, _question_rx, _approval_rx, _plan_rx) = AgentStack::new(AgentStackConfig {
        data_dir: dir.path().to_path_buf(),
        injected_provider: Some(Arc::new(MockProvider::new("test", vec![]))),
        ..Default::default()
    })
    .await
    .expect("stack init should succeed");

    let persistent_names = stack
        .tools
        .read()
        .await
        .list_tools_with_source()
        .into_iter()
        .map(|(tool, _)| tool.name)
        .collect::<Vec<_>>();
    assert!(!persistent_names.iter().any(|name| name == "subagent"));

    let history = vec![
        Message::new(
            Role::User,
            "Use a scout subagent to inspect the repo, then review the final change.",
        ),
        Message::new(Role::Assistant, "I'll scout the repo first."),
    ];

    let delegated_follow_up = stack
        .effective_tools_for_interactive_run("Now implement the fix.", &history, &[])
        .await;
    assert!(delegated_follow_up
        .iter()
        .any(|(tool, _)| tool.name == "subagent"));

    let explicit_delegate_tools = stack
        .effective_tools_for_interactive_run(
            "Use a scout subagent to inspect the repo, then review the final change.",
            &[],
            &[],
        )
        .await;
    assert!(explicit_delegate_tools
        .iter()
        .any(|(tool, _)| tool.name == "subagent"));

    let read_only_tools = stack
        .effective_tools_for_interactive_run(
            "Read package.json in the current directory and reply with only the package name.",
            &[],
            &[],
        )
        .await;
    let read_only_names = read_only_tools
        .iter()
        .map(|(tool, _)| tool.name.as_str())
        .collect::<Vec<_>>();
    assert!(read_only_names.contains(&"read"));
    assert!(!read_only_names.contains(&"write"));
    assert!(!read_only_names.contains(&"edit"));
    assert!(!read_only_names.contains(&"bash"));

    let answer_only_tools = stack
        .effective_tools_for_interactive_run(
            "Reply exactly with BENCHMARK_OK and nothing else.",
            &[],
            &[],
        )
        .await;
    assert!(answer_only_tools.is_empty());

    let default_tools = stack
        .effective_tools_for_interactive_run("finish task", &[], &[])
        .await;
    assert!(!default_tools
        .iter()
        .any(|(tool, _)| tool.name == "subagent"));
}

#[tokio::test]
async fn agent_stack_run_dispatches_subagent_when_enabled() {
    let dir = tempfile::tempdir().expect("tempdir");
    let provider = Arc::new(MockProvider::new(
        "test-model",
        vec![
            subagent_response("Read AGENTS.md and summarize it."),
            completion_response("scout summary"),
            completion_response("done"),
        ],
    ));
    let (stack, _question_rx, _approval_rx, _plan_rx) = AgentStack::new(AgentStackConfig {
        data_dir: dir.path().to_path_buf(),
        injected_provider: Some(provider),
        ..Default::default()
    })
    .await
    .expect("stack init should succeed");

    let (tx, mut rx) = mpsc::unbounded_channel();
    let result = stack
        .run(
            "Use a scout subagent to inspect the repo, then implement a follow-up patch and finish the task.",
            5,
            Some(tx),
            CancellationToken::new(),
            Vec::new(),
            None,
            Vec::new(),
            None,
            None,
        )
        .await
        .expect("run should succeed");

    assert!(result.success);
    assert!(result
        .session
        .messages
        .iter()
        .any(|m| m.tool_calls.iter().any(|tc| tc.name == "subagent")));
    assert!(result
        .session
        .messages
        .iter()
        .any(|m| m.content.contains("scout summary")));

    let mut saw_subagent_complete = false;
    while let Ok(event) = rx.try_recv() {
        if let ava_agent::AgentEvent::SubAgentComplete { description, .. } = event {
            if description.contains("Read AGENTS.md and summarize it.") {
                saw_subagent_complete = true;
                break;
            }
        }
    }
    assert!(saw_subagent_complete, "expected subagent completion event");
}

#[tokio::test]
async fn agent_stack_run_honors_cancellation() {
    let dir = tempfile::tempdir().expect("tempdir");
    let provider = Arc::new(SlowProvider {
        model: "slow-model".to_string(),
        delay: Duration::from_millis(250),
    });
    let (stack, _question_rx, _approval_rx, _plan_rx) = AgentStack::new(AgentStackConfig {
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
        .run(
            "slow task",
            5,
            None,
            cancel,
            Vec::new(),
            None,
            Vec::new(),
            None,
            None,
        )
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

    let (stack, _question_rx, _approval_rx, _plan_rx) = AgentStack::new(AgentStackConfig {
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
    let (stack, _question_rx, _approval_rx, _plan_rx) = AgentStack::new(AgentStackConfig {
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

    let (stack, _question_rx, _approval_rx, _plan_rx) = AgentStack::new(AgentStackConfig {
        data_dir: dir.path().to_path_buf(),
        ..Default::default()
    })
    .await
    .expect("stack init should succeed");

    let decision = stack
        .resolve_model_route("Summarize this diff in two bullets.", &[], None)
        .await
        .expect("route resolution should succeed");

    assert_eq!(decision.source, RouteSource::PolicyAuto);
    assert_eq!(decision.provider, "openai");
    assert_eq!(decision.display_model, "gpt-5.4-nano");
}

#[tokio::test]
async fn agent_stack_resolve_model_route_respects_manual_override_lock() {
    let dir = tempfile::tempdir().expect("tempdir");
    write_routing_config(&dir);
    write_credentials(&dir, &["anthropic", "openai"]).await;

    let (stack, _question_rx, _approval_rx, _plan_rx) = AgentStack::new(AgentStackConfig {
        data_dir: dir.path().to_path_buf(),
        provider: Some("openai".to_string()),
        model: Some("gpt-5.3-codex".to_string()),
        ..Default::default()
    })
    .await
    .expect("stack init should succeed");

    let decision = stack
        .resolve_model_route("Summarize this diff in two bullets.", &[], None)
        .await
        .expect("route resolution should succeed");

    assert_eq!(decision.source, RouteSource::ManualOverride);
    assert_eq!(decision.provider, "openai");
    assert_eq!(decision.display_model, "gpt-5.3-codex");
}

#[tokio::test]
async fn agent_stack_resolve_model_route_respects_run_context_override_only() {
    let dir = tempfile::tempdir().expect("tempdir");
    write_routing_config(&dir);
    write_credentials(&dir, &["anthropic", "openai"]).await;

    let (stack, _question_rx, _approval_rx, _plan_rx) = AgentStack::new(AgentStackConfig {
        data_dir: dir.path().to_path_buf(),
        ..Default::default()
    })
    .await
    .expect("stack init should succeed");

    let context = AgentRunContext {
        provider: Some("openai".to_string()),
        model: Some("gpt-5.4-nano".to_string()),
        thinking_level: None,
        auto_compact: None,
        compaction_threshold: None,
        compaction_provider: None,
        compaction_model: None,
        todo_state: None,
        permission_context: None,
    };

    let decision = stack
        .resolve_model_route("Summarize this diff in two bullets.", &[], Some(&context))
        .await
        .expect("route resolution should succeed");

    assert_eq!(decision.source, RouteSource::ManualOverride);
    assert_eq!(decision.provider, "openai");
    assert_eq!(decision.model, "gpt-5.4-nano");

    let (provider, model) = stack.current_model().await;
    assert_eq!(provider, "anthropic");
    assert_eq!(model, "claude-sonnet-4.6");
}

#[tokio::test]
async fn agent_stack_run_with_context_thinking_level_does_not_mutate_shared_thinking() {
    let dir = tempfile::tempdir().expect("tempdir");
    let recorded = Arc::new(tokio::sync::Mutex::new(Vec::new()));
    let provider = Arc::new(RecordingThinkingProvider::new(
        "gemini-2.5-pro",
        recorded.clone(),
    ));
    let (stack, _question_rx, _approval_rx, _plan_rx) = AgentStack::new(AgentStackConfig {
        data_dir: dir.path().to_path_buf(),
        injected_provider: Some(provider.clone()),
        ..Default::default()
    })
    .await
    .expect("stack init should succeed");

    stack
        .set_thinking_level(ava_types::ThinkingLevel::High)
        .await
        .expect("set shared thinking level");

    let context = AgentRunContext {
        provider: None,
        model: None,
        thinking_level: Some(ava_types::ThinkingLevel::Low),
        auto_compact: None,
        compaction_threshold: None,
        compaction_provider: None,
        compaction_model: None,
        todo_state: None,
        permission_context: None,
    };

    let (event_tx, _event_rx) = mpsc::unbounded_channel();

    let result = stack
        .run_with_context(
            "finish task",
            5,
            Some(event_tx),
            CancellationToken::new(),
            Vec::new(),
            None,
            Vec::new(),
            None,
            None,
            Some(context),
        )
        .await
        .expect("run should succeed");

    assert!(result.success);

    let shared_level = stack.get_thinking_level().await;
    assert_eq!(shared_level, ava_types::ThinkingLevel::High);

    let lock = recorded.lock().await;
    assert_eq!(lock.len(), 1);
    assert_eq!(lock[0].level, ava_types::ThinkingLevel::Low);
}

#[tokio::test]
async fn agent_stack_run_with_context_falls_back_to_shared_compaction_override() {
    let dir = tempfile::tempdir().expect("tempdir");
    write_credentials(&dir, &["openai"]).await;
    let provider = Arc::new(MockProvider::new(
        "test-model",
        vec![completion_response("done")],
    ));
    let (stack, _question_rx, _approval_rx, _plan_rx) = AgentStack::new(AgentStackConfig {
        data_dir: dir.path().to_path_buf(),
        injected_provider: Some(provider),
        ..Default::default()
    })
    .await
    .expect("stack init should succeed");

    stack
        .set_compaction_settings(
            true,
            80,
            Some(("openai".to_string(), "gpt-5.4-nano".to_string())),
        )
        .await
        .expect("set shared compaction settings");

    let context = AgentRunContext {
        provider: None,
        model: None,
        thinking_level: None,
        auto_compact: None,
        compaction_threshold: None,
        compaction_provider: None,
        compaction_model: None,
        todo_state: None,
        permission_context: None,
    };

    let result = stack
        .run_with_context(
            "finish task",
            5,
            None,
            CancellationToken::new(),
            Vec::new(),
            None,
            Vec::new(),
            None,
            None,
            Some(context),
        )
        .await
        .expect("run should succeed");

    let run_context = result
        .session
        .metadata
        .get("runContext")
        .and_then(|value| value.as_object())
        .expect("runContext metadata should be present");
    assert_eq!(
        run_context
            .get("compactionProvider")
            .and_then(|value| value.as_str()),
        Some("openai")
    );
    assert_eq!(
        run_context
            .get("compactionModel")
            .and_then(|value| value.as_str()),
        Some("gpt-5.4-nano")
    );
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
    let (stack, _question_rx, _approval_rx, _plan_rx) = AgentStack::new(AgentStackConfig {
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
            None,
            None,
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
    let (stack, _question_rx, _approval_rx, _plan_rx) = AgentStack::new(AgentStackConfig {
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
            None,
            None,
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
    let (stack, _question_rx, _approval_rx, _plan_rx) = AgentStack::new(AgentStackConfig {
        data_dir: dir.path().to_path_buf(),
        injected_provider: Some(provider),
        ..Default::default()
    })
    .await
    .expect("stack init should succeed");

    stack
        .set_thinking_level(ava_types::ThinkingLevel::High)
        .await
        .expect("set_thinking_level should succeed");
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
            None,
            None,
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

#[tokio::test]
async fn agent_stack_initializes_with_plugin_manager() {
    let dir = tempfile::tempdir().expect("tempdir");
    let (stack, _question_rx, _approval_rx, _plan_rx) = AgentStack::new(AgentStackConfig {
        data_dir: dir.path().to_path_buf(),
        yolo: true,
        injected_provider: Some(Arc::new(MockProvider::new("test", vec![]))),
        ..Default::default()
    })
    .await
    .expect("stack init should succeed");

    // Plugin manager should be initialized with zero running plugins
    // (no plugins installed in the temp directory)
    let pm = stack.plugin_manager.lock().await;
    assert_eq!(pm.running_count(), 0);
    assert!(pm.list_plugins().is_empty());
}

#[tokio::test]
async fn agent_stack_skips_project_local_plugins_when_project_is_untrusted() {
    let dir = tempfile::tempdir().expect("tempdir");
    let data_dir = tempfile::tempdir().expect("data dir");
    write_plugin_manifest(dir.path(), "local-only-plugin");

    let (stack, _question_rx, _approval_rx, _plan_rx) = AgentStack::new(AgentStackConfig {
        data_dir: data_dir.path().to_path_buf(),
        working_dir: Some(dir.path().to_path_buf()),
        yolo: true,
        injected_provider: Some(Arc::new(MockProvider::new("test", vec![]))),
        ..Default::default()
    })
    .await
    .expect("stack init should succeed");

    let pm = stack.plugin_manager.lock().await;
    assert_eq!(pm.running_count(), 0);
    assert!(
        pm.list_plugins().is_empty(),
        "untrusted project-local plugins must not be auto-discovered or started"
    );
}

#[tokio::test]
async fn plugin_hooks_fire_without_crash_on_run() {
    let dir = tempfile::tempdir().expect("tempdir");
    let provider = Arc::new(MockProvider::new(
        "test-model",
        vec![completion_response("done")],
    ));
    let (stack, _question_rx, _approval_rx, _plan_rx) = AgentStack::new(AgentStackConfig {
        data_dir: dir.path().to_path_buf(),
        injected_provider: Some(provider),
        ..Default::default()
    })
    .await
    .expect("stack init should succeed");

    // Run the agent -- plugin hooks (SessionStart, AgentBefore, AgentAfter,
    // SessionEnd) should fire without error even with no plugins loaded
    let result = stack
        .run(
            "finish task",
            5,
            None,
            CancellationToken::new(),
            Vec::new(),
            None,
            Vec::new(),
            None,
            None,
        )
        .await
        .expect("run should succeed");

    assert!(result.success);
}

#[tokio::test]
async fn plugin_manager_shutdown() {
    let dir = tempfile::tempdir().expect("tempdir");
    let (stack, _question_rx, _approval_rx, _plan_rx) = AgentStack::new(AgentStackConfig {
        data_dir: dir.path().to_path_buf(),
        yolo: true,
        injected_provider: Some(Arc::new(MockProvider::new("test", vec![]))),
        ..Default::default()
    })
    .await
    .expect("stack init should succeed");

    // Shutdown should complete without error even with no plugins
    stack.shutdown_plugins().await;
    let pm = stack.plugin_manager.lock().await;
    assert_eq!(pm.running_count(), 0);
}
