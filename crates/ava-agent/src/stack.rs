use std::path::PathBuf;
use std::pin::Pin;
use std::sync::Arc;

use async_trait::async_trait;
use ava_config::ConfigManager;
use ava_context::ContextManager;
use ava_llm::provider::LLMProvider;
use ava_llm::ModelRouter;
use ava_memory::MemorySystem;
use ava_platform::StandardPlatform;
use ava_session::SessionManager;
use ava_tools::core::register_core_tools;
use ava_tools::registry::ToolRegistry;
use ava_types::{AvaError, Message, Result, Session};
use futures::{Stream, StreamExt};
use tokio::sync::mpsc;
use tokio_util::sync::CancellationToken;

use crate::agent_loop::{AgentConfig, AgentEvent, AgentLoop};

pub struct AgentStack {
    pub router: ModelRouter,
    pub tools: Arc<ToolRegistry>,
    pub session_manager: SessionManager,
    pub memory: MemorySystem,
    pub config: ConfigManager,
    pub platform: Arc<StandardPlatform>,
    provider_override: Option<String>,
    model_override: Option<String>,
    max_turns: usize,
    #[allow(dead_code)]
    yolo: bool,
    injected_provider: Option<Arc<dyn LLMProvider>>,
}

pub struct AgentStackConfig {
    pub data_dir: PathBuf,
    pub provider: Option<String>,
    pub model: Option<String>,
    pub max_turns: usize,
    pub yolo: bool,
    pub injected_provider: Option<Arc<dyn LLMProvider>>,
}

#[derive(Debug)]
pub struct AgentRunResult {
    pub success: bool,
    pub turns: usize,
    pub session: Session,
}

impl Default for AgentStackConfig {
    fn default() -> Self {
        Self {
            data_dir: dirs::home_dir().unwrap_or_default().join(".ava"),
            provider: None,
            model: None,
            max_turns: 20,
            yolo: false,
            injected_provider: None,
        }
    }
}

impl AgentStack {
    pub async fn new(config: AgentStackConfig) -> Result<Self> {
        tokio::fs::create_dir_all(&config.data_dir)
            .await
            .map_err(|e| AvaError::IoError(e.to_string()))?;

        let db_path = config.data_dir.join("data.db");
        let config_path = config.data_dir.join("config.yaml");
        let credentials_path = config.data_dir.join("credentials.json");

        let platform = Arc::new(StandardPlatform);
        let tools = Arc::new(build_tool_registry(platform.clone()));

        let config_mgr = ConfigManager::load_from_paths(config_path, credentials_path).await?;
        let credentials = config_mgr.credentials().await;
        let router = ModelRouter::new(credentials);

        let session_manager = SessionManager::new(&db_path)?;
        let memory = MemorySystem::new(&db_path)
            .map_err(|e| AvaError::DatabaseError(e.to_string()))?;

        Ok(Self {
            router,
            tools,
            session_manager,
            memory,
            config: config_mgr,
            platform,
            provider_override: config.provider,
            model_override: config.model,
            max_turns: config.max_turns,
            yolo: config.yolo,
            injected_provider: config.injected_provider,
        })
    }

    pub async fn run(
        &self,
        goal: &str,
        max_turns: usize,
        event_tx: Option<mpsc::UnboundedSender<AgentEvent>>,
        cancel: CancellationToken,
    ) -> Result<AgentRunResult> {
        let provider = if let Some(provider) = &self.injected_provider {
            provider.clone()
        } else {
            let cfg = self.config.get().await;
            let provider_name = self
                .provider_override
                .clone()
                .unwrap_or_else(|| cfg.llm.provider.clone());
            let model_name = self
                .model_override
                .clone()
                .unwrap_or_else(|| cfg.llm.model.clone());
            self.router.route_required(&provider_name, &model_name).await?
        };

        let turns_limit = if max_turns == 0 { self.max_turns } else { max_turns };

        let config = AgentConfig {
            max_turns: turns_limit,
            token_limit: 128_000,
            model: provider.model_name().to_string(),
        };

        let mut agent = AgentLoop::new(
            Box::new(SharedProvider::new(provider)),
            build_tool_registry(self.platform.clone()),
            ContextManager::new(config.token_limit),
            config,
        );

        if let Some(tx) = event_tx {
            let mut stream = agent.run_streaming(goal).await;
            let mut final_session: Option<Session> = None;

            loop {
                tokio::select! {
                    _ = cancel.cancelled() => {
                        return Err(AvaError::TimeoutError("Operation cancelled".to_string()));
                    }
                    maybe_event = stream.next() => {
                        let Some(event) = maybe_event else { break; };
                        let _ = tx.send(event.clone());
                        match event {
                            AgentEvent::Complete(session) => {
                                final_session = Some(session);
                                break;
                            }
                            AgentEvent::Error(error) => {
                                return Err(AvaError::ToolError(error));
                            }
                            AgentEvent::Progress(_) | AgentEvent::Token(_) | AgentEvent::ToolCall(_) | AgentEvent::ToolResult(_) => {}
                        }
                    }
                }
            }

            let session = final_session
                .ok_or_else(|| AvaError::ToolError("agent stream ended without completion".to_string()))?;

            return Ok(AgentRunResult {
                success: true,
                turns: session.messages.len(),
                session,
            });
        }

        let session = tokio::select! {
            value = agent.run(goal) => value,
            _ = cancel.cancelled() => Err(AvaError::TimeoutError("Operation cancelled".to_string())),
        }?;

        Ok(AgentRunResult {
            success: true,
            turns: session.messages.len(),
            session,
        })
    }
}

fn build_tool_registry(platform: Arc<StandardPlatform>) -> ToolRegistry {
    let mut registry = ToolRegistry::new();
    register_core_tools(&mut registry, platform);
    registry
}

#[cfg(test)]
const _: () = {
    fn assert_send<T: Send>() {}
    fn check() {
        assert_send::<AgentStack>();
    }
};

struct SharedProvider {
    inner: Arc<dyn LLMProvider>,
}

impl SharedProvider {
    fn new(inner: Arc<dyn LLMProvider>) -> Self {
        Self { inner }
    }
}

#[async_trait]
impl LLMProvider for SharedProvider {
    async fn generate(&self, messages: &[Message]) -> Result<String> {
        self.inner.generate(messages).await
    }

    async fn generate_stream(
        &self,
        messages: &[Message],
    ) -> Result<Pin<Box<dyn Stream<Item = String> + Send>>> {
        self.inner.generate_stream(messages).await
    }

    fn estimate_tokens(&self, input: &str) -> usize {
        self.inner.estimate_tokens(input)
    }

    fn estimate_cost(&self, input_tokens: usize, output_tokens: usize) -> f64 {
        self.inner.estimate_cost(input_tokens, output_tokens)
    }

    fn model_name(&self) -> &str {
        self.inner.model_name()
    }
}
