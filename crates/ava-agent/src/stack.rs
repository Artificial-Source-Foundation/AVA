use std::path::PathBuf;
use std::sync::Arc;

use async_trait::async_trait;
use ava_config::ConfigManager;
use ava_context::{create_hybrid_condenser, CondenserConfig, ContextManager, Summarizer};
use ava_llm::provider::{LLMProvider, SharedProvider};
use ava_llm::ModelRouter;
use ava_mcp::config::load_mcp_config;
use ava_mcp::manager::ExtensionManager;
use ava_memory::MemorySystem;
use ava_platform::StandardPlatform;
use ava_session::SessionManager;
use ava_tools::core::register_core_tools;
use ava_tools::mcp_bridge::{MCPBridgeTool, MCPToolCaller};
use ava_tools::registry::ToolRegistry;
use ava_types::{AvaError, Result, Session, ToolResult};
use futures::StreamExt;
use serde_json::Value;
use tokio::sync::{mpsc, RwLock};
use tokio_util::sync::CancellationToken;

use tracing::{info, instrument, warn};

use crate::agent_loop::{AgentConfig, AgentEvent, AgentLoop};

/// Wraps an `LLMProvider` into the `Summarizer` trait for context compaction.
struct LlmSummarizer(Arc<dyn LLMProvider>);

#[async_trait]
impl Summarizer for LlmSummarizer {
    async fn summarize(&self, text: &str) -> std::result::Result<String, String> {
        use ava_types::{Message, Role};
        let messages = vec![Message::new(Role::User, text.to_string())];
        self.0
            .generate(&messages)
            .await
            .map_err(|e| e.to_string())
    }
}

/// Adapter implementing `MCPToolCaller` by delegating to `ExtensionManager`.
struct ExtensionManagerCaller {
    manager: ExtensionManager,
}

#[async_trait]
impl MCPToolCaller for ExtensionManagerCaller {
    async fn call_tool(&self, name: &str, arguments: Value) -> Result<ToolResult> {
        self.manager.call_tool(name, arguments).await
    }
}

pub struct AgentStack {
    pub router: ModelRouter,
    pub tools: Arc<ToolRegistry>,
    pub session_manager: SessionManager,
    pub memory: MemorySystem,
    pub config: ConfigManager,
    pub platform: Arc<StandardPlatform>,
    provider_override: RwLock<Option<String>>,
    model_override: RwLock<Option<String>>,
    max_turns: usize,
    yolo: bool,
    injected_provider: Option<Arc<dyn LLMProvider>>,
    /// MCP extension manager (if any MCP servers are configured).
    _mcp_caller: Option<Arc<dyn MCPToolCaller>>,
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
        let mut registry = build_tool_registry(platform.clone());

        // Load MCP servers (optional — no crash if mcp.json doesn't exist)
        let mcp_config_path = config.data_dir.join("mcp.json");
        let mcp_caller = match load_mcp_config(&mcp_config_path).await {
            Ok(configs) if !configs.is_empty() => {
                let mut manager = ExtensionManager::new();
                manager.initialize(configs).await?;

                let mcp_tools = manager.list_tools();
                let caller: Arc<dyn MCPToolCaller> =
                    Arc::new(ExtensionManagerCaller { manager });

                for tool_def in mcp_tools {
                    info!(tool = %tool_def.name, "Registering MCP tool");
                    registry.register(MCPBridgeTool::new(tool_def, caller.clone()));
                }

                Some(caller)
            }
            Ok(_) => None,
            Err(e) => {
                warn!(error = %e, "Failed to load MCP config, continuing without MCP tools");
                None
            }
        };

        let tools = Arc::new(registry);

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
            provider_override: RwLock::new(config.provider),
            model_override: RwLock::new(config.model),
            max_turns: config.max_turns,
            yolo: config.yolo,
            injected_provider: config.injected_provider,
            _mcp_caller: mcp_caller,
        })
    }

    /// Whether this stack runs in yolo mode (skip tool permission checks).
    pub fn yolo(&self) -> bool {
        self.yolo
    }

    /// Switch the provider and model for subsequent runs.
    /// Validates that the provider/model combination can be routed.
    #[instrument(skip(self))]
    pub async fn switch_model(&self, provider: &str, model: &str) -> Result<()> {
        // Validate by attempting to route — this ensures credentials exist
        self.router.route_required(provider, model).await?;

        *self.provider_override.write().await = Some(provider.to_string());
        *self.model_override.write().await = Some(model.to_string());
        Ok(())
    }

    /// Get the current provider and model names.
    pub async fn current_model(&self) -> (String, String) {
        let cfg = self.config.get().await;
        let provider = self
            .provider_override
            .read()
            .await
            .clone()
            .unwrap_or_else(|| cfg.llm.provider.clone());
        let model = self
            .model_override
            .read()
            .await
            .clone()
            .unwrap_or_else(|| cfg.llm.model.clone());
        (provider, model)
    }

    #[instrument(skip(self, event_tx, cancel), fields(max_turns))]
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
            let (provider_name, model_name) = self.current_model().await;
            self.router.route_required(&provider_name, &model_name).await?
        };

        let turns_limit = if max_turns == 0 { self.max_turns } else { max_turns };

        let config = AgentConfig {
            max_turns: turns_limit,
            token_limit: 128_000,
            model: provider.model_name().to_string(),
            max_cost_usd: 10.0,
            loop_detection: true,
        };

        let summarizer: Arc<dyn Summarizer> = Arc::new(LlmSummarizer(provider.clone()));
        let condenser_config = CondenserConfig {
            max_tokens: config.token_limit,
            target_tokens: config.token_limit * 3 / 4,
            ..Default::default()
        };
        let condenser = create_hybrid_condenser(condenser_config.clone(), Some(summarizer));
        let context = ContextManager::new_with_condenser(condenser_config, condenser);

        let mut agent = AgentLoop::new(
            Box::new(SharedProvider::new(provider)),
            build_tool_registry(self.platform.clone()),
            context,
            config,
        );

        if let Some(tx) = event_tx {
            let mut stream = agent.run_streaming(goal).await;
            let mut final_session: Option<Session> = None;

            loop {
                tokio::select! {
                    _ = cancel.cancelled() => {
                        return Err(AvaError::Cancelled);
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
                                return Err(AvaError::AgentStopped { reason: error });
                            }
                            AgentEvent::Progress(_) | AgentEvent::Token(_) | AgentEvent::ToolCall(_) | AgentEvent::ToolResult(_) | AgentEvent::ToolStats(_) => {}
                        }
                    }
                }
            }

            let session = final_session
                .ok_or_else(|| AvaError::ToolError(
                "Agent stream ended unexpectedly without a completion event. \
                 This may indicate the model returned no actionable response".to_string()
            ))?;

            return Ok(AgentRunResult {
                success: true,
                turns: session.messages.len(),
                session,
            });
        }

        let session = tokio::select! {
            value = agent.run(goal) => value,
            _ = cancel.cancelled() => Err(AvaError::Cancelled),
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
