use std::path::PathBuf;
use std::sync::Arc;

use async_trait::async_trait;
use ava_codebase::CodebaseIndex;
use ava_codebase::indexer::index_project;
use ava_config::ConfigManager;
use ava_context::{
    create_hybrid_condenser_with_relevance, CondenserConfig, ContextManager, Summarizer,
};
use ava_llm::provider::{LLMProvider, SharedProvider};
use ava_llm::ModelRouter;
use ava_mcp::config::load_merged_mcp_config;
use ava_mcp::manager::ExtensionManager;
use ava_memory::MemorySystem;
use ava_platform::StandardPlatform;
use ava_session::SessionManager;
use ava_tools::core::{
    register_codebase_tools, register_core_tools, register_custom_tools, register_memory_tools,
    register_session_tools,
};
use ava_tools::mcp_bridge::{MCPBridgeTool, MCPToolCaller};
use ava_tools::registry::{ToolRegistry, ToolSource};
use ava_types::{AvaError, Result, Session, ThinkingLevel, ToolResult};
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

/// Tracks MCP runtime state — servers, tools, and the caller bridge.
struct MCPRuntime {
    caller: Arc<dyn MCPToolCaller>,
    server_count: usize,
    tool_count: usize,
    tools_with_source: Vec<(String, ava_types::Tool)>,
}

/// Server info for display in TUI.
#[derive(Debug, Clone)]
pub struct MCPServerInfo {
    pub name: String,
    pub tool_count: usize,
}

/// Unified entrypoint composing LLM routing, tool registry, session/memory persistence,
/// MCP integration, and codebase indexing into a single `run()` call.
pub struct AgentStack {
    pub router: ModelRouter,
    pub tools: Arc<RwLock<ToolRegistry>>,
    pub session_manager: Arc<SessionManager>,
    pub memory: Arc<MemorySystem>,
    pub config: ConfigManager,
    pub platform: Arc<StandardPlatform>,
    pub codebase_index: Arc<RwLock<Option<Arc<CodebaseIndex>>>>,
    provider_override: RwLock<Option<String>>,
    model_override: RwLock<Option<String>>,
    max_turns: usize,
    yolo: bool,
    injected_provider: Option<Arc<dyn LLMProvider>>,
    mcp: Arc<RwLock<Option<MCPRuntime>>>,
    custom_tool_dirs: Vec<PathBuf>,
    mcp_global_config: PathBuf,
    mcp_project_config: PathBuf,
    /// Current thinking level for extended reasoning (persisted per session).
    pub thinking_level: RwLock<ThinkingLevel>,
}

/// Configuration for constructing an [`AgentStack`] — data directory, provider/model overrides, and flags.
pub struct AgentStackConfig {
    pub data_dir: PathBuf,
    pub provider: Option<String>,
    pub model: Option<String>,
    pub max_turns: usize,
    pub yolo: bool,
    pub injected_provider: Option<Arc<dyn LLMProvider>>,
}

/// Result of a completed agent run — success flag, turn count, and final session transcript.
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

        let config_mgr = ConfigManager::load_from_paths(config_path, credentials_path).await?;
        let credentials = config_mgr.credentials().await;
        let router = ModelRouter::new(credentials);

        let session_manager = Arc::new(SessionManager::new(&db_path)?);
        let memory = Arc::new(
            MemorySystem::new(&db_path)
                .map_err(|e| AvaError::DatabaseError(e.to_string()))?,
        );

        // Background codebase indexing
        let codebase_index: Arc<RwLock<Option<Arc<CodebaseIndex>>>> =
            Arc::new(RwLock::new(None));
        let index_clone = codebase_index.clone();
        let project_root = std::env::current_dir().unwrap_or_default();
        tokio::spawn(async move {
            match index_project(&project_root).await {
                Ok(idx) => {
                    *index_clone.write().await = Some(Arc::new(idx));
                    info!("Codebase indexing complete");
                }
                Err(e) => warn!("Codebase indexing failed: {e}"),
            }
        });

        // Pre-warm connection pool for the configured provider
        let cfg = config_mgr.get().await;
        let provider_name = config.provider.as_deref().unwrap_or(&cfg.llm.provider);
        if let Some(base_url) = ava_llm::providers::base_url_for_provider(provider_name) {
            match router.pool().get_client(base_url).await {
                Ok(_) => info!(base_url, "Pre-warmed connection pool"),
                Err(e) => warn!(%e, base_url, "Failed to pre-warm connection pool"),
            }
        }

        // Config paths
        let mcp_global_config = config.data_dir.join("mcp.json");
        let mcp_project_config = std::env::current_dir()
            .unwrap_or_default()
            .join(".ava")
            .join("mcp.json");
        let custom_tool_dirs = vec![
            config.data_dir.join("tools"),
            std::env::current_dir()
                .unwrap_or_default()
                .join(".ava")
                .join("tools"),
        ];

        // Build registry
        let mut registry = build_tool_registry(platform.clone());
        register_memory_tools(&mut registry, memory.clone());
        register_session_tools(&mut registry, session_manager.clone());
        register_codebase_tools(&mut registry, codebase_index.clone());
        register_custom_tools(&mut registry, &custom_tool_dirs);

        // Load MCP servers (merged global + project)
        let mcp_runtime = init_mcp(&mcp_global_config, &mcp_project_config, &mut registry).await;

        let tools = Arc::new(RwLock::new(registry));

        Ok(Self {
            router,
            tools,
            session_manager,
            memory,
            config: config_mgr,
            platform,
            codebase_index,
            provider_override: RwLock::new(config.provider),
            model_override: RwLock::new(config.model),
            max_turns: config.max_turns,
            yolo: config.yolo,
            injected_provider: config.injected_provider,
            mcp: Arc::new(RwLock::new(mcp_runtime)),
            custom_tool_dirs,
            mcp_global_config,
            mcp_project_config,
            thinking_level: RwLock::new(ThinkingLevel::Off),
        })
    }

    /// Whether this stack runs in yolo mode (skip tool permission checks).
    pub fn yolo(&self) -> bool {
        self.yolo
    }

    /// Number of connected MCP servers.
    pub async fn mcp_server_count(&self) -> usize {
        self.mcp.read().await.as_ref().map_or(0, |r| r.server_count)
    }

    /// Number of discovered MCP tools.
    pub async fn mcp_tool_count(&self) -> usize {
        self.mcp.read().await.as_ref().map_or(0, |r| r.tool_count)
    }

    /// Get info about connected MCP servers.
    pub async fn mcp_server_info(&self) -> Vec<MCPServerInfo> {
        let guard = self.mcp.read().await;
        let runtime = match guard.as_ref() {
            Some(r) => r,
            None => return Vec::new(),
        };

        let mut servers: std::collections::HashMap<String, usize> =
            std::collections::HashMap::new();
        for (server_name, _) in &runtime.tools_with_source {
            *servers.entry(server_name.clone()).or_insert(0) += 1;
        }

        servers
            .into_iter()
            .map(|(name, tool_count)| MCPServerInfo { name, tool_count })
            .collect()
    }

    /// Reload MCP servers from config files. Returns (server_count, tool_count).
    pub async fn reload_mcp(&self) -> Result<(usize, usize)> {
        let mut registry = self.tools.write().await;
        // Remove existing MCP tools
        registry.remove_by_source(|src| matches!(src, ToolSource::MCP { .. }));

        let runtime =
            init_mcp(&self.mcp_global_config, &self.mcp_project_config, &mut registry).await;
        let counts = runtime
            .as_ref()
            .map_or((0, 0), |r| (r.server_count, r.tool_count));
        *self.mcp.write().await = runtime;
        Ok(counts)
    }

    /// Reload custom tools from TOML dirs. Returns the number of tools loaded.
    pub async fn reload_custom_tools(&self) -> usize {
        let mut registry = self.tools.write().await;
        registry.remove_by_source(|src| matches!(src, ToolSource::Custom { .. }));
        register_custom_tools(&mut registry, &self.custom_tool_dirs);
        registry
            .list_tools_with_source()
            .iter()
            .filter(|(_, src)| matches!(src, ToolSource::Custom { .. }))
            .count()
    }

    /// Reload ALL tools (core + memory + session + codebase + custom + MCP).
    pub async fn reload_tools(&self) -> Result<usize> {
        let mut registry = build_tool_registry(self.platform.clone());
        register_memory_tools(&mut registry, self.memory.clone());
        register_session_tools(&mut registry, self.session_manager.clone());
        register_codebase_tools(&mut registry, self.codebase_index.clone());
        register_custom_tools(&mut registry, &self.custom_tool_dirs);

        let runtime =
            init_mcp(&self.mcp_global_config, &self.mcp_project_config, &mut registry).await;
        let count = registry.tool_count();

        *self.tools.write().await = registry;
        *self.mcp.write().await = runtime;
        Ok(count)
    }

    /// Switch the provider and model for subsequent runs.
    /// Validates that the provider/model combination can be routed.
    /// Persists the choice per-project (`.ava/state.json`).
    #[instrument(skip(self))]
    pub async fn switch_model(&self, provider: &str, model: &str) -> Result<()> {
        // Validate by attempting to route — this ensures credentials exist
        self.router.route_required(provider, model).await?;

        *self.provider_override.write().await = Some(provider.to_string());
        *self.model_override.write().await = Some(model.to_string());

        // Persist per-project
        let project_root = std::env::current_dir().unwrap_or_default();
        let mut state = ava_config::ProjectState::load(&project_root);
        state.last_provider = Some(provider.to_string());
        state.last_model = Some(model.to_string());
        let _ = state.save(&project_root);

        Ok(())
    }

    /// Set the thinking level.
    pub async fn set_thinking_level(&self, level: ThinkingLevel) {
        *self.thinking_level.write().await = level;
    }

    /// Cycle thinking: Off → Low → Medium → High → Max → Off.
    /// Returns the new level's label for status display.
    pub async fn cycle_thinking(&self) -> &'static str {
        let mut guard = self.thinking_level.write().await;
        *guard = guard.cycle();
        guard.label()
    }

    /// Get current thinking level.
    pub async fn get_thinking_level(&self) -> ThinkingLevel {
        *self.thinking_level.read().await
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

    /// Enrich goal with relevant memories for auto-context injection.
    async fn enrich_goal_with_memories(&self, goal: &str) -> String {
        let keywords = extract_goal_keywords(goal);
        if keywords.is_empty() {
            return goal.to_string();
        }

        let query = keywords.join(" ");
        let memories = match self.memory.search(&query) {
            Ok(m) => m,
            Err(_) => return goal.to_string(),
        };

        if memories.is_empty() {
            return goal.to_string();
        }

        let entries: Vec<String> = memories
            .into_iter()
            .take(5)
            .map(|m| format!("- [{}]: {}", m.key, m.value))
            .collect();

        // Cap memory context at ~500 tokens (~2000 chars)
        let memory_block = entries.join("\n");
        let memory_block = if memory_block.len() > 2000 {
            format!("{}...", &memory_block[..2000])
        } else {
            memory_block
        };

        format!("{goal}\n\nRelevant memories:\n{memory_block}")
    }

    #[instrument(skip(self, event_tx, cancel, history), fields(max_turns))]
    pub async fn run(
        &self,
        goal: &str,
        max_turns: usize,
        event_tx: Option<mpsc::UnboundedSender<AgentEvent>>,
        cancel: CancellationToken,
        history: Vec<ava_types::Message>,
    ) -> Result<AgentRunResult> {
        let provider = if let Some(provider) = &self.injected_provider {
            provider.clone()
        } else {
            let (provider_name, model_name) = self.current_model().await;
            match self.router.route_required(&provider_name, &model_name).await {
                Ok(p) => p,
                Err(e) => {
                    // Try fallback if configured
                    let cfg = self.config.get().await;
                    if let Some(fb) = &cfg.fallback {
                        warn!(
                            primary = %provider_name,
                            fallback = %fb.provider,
                            "Primary provider unavailable, using fallback"
                        );
                        if let Some(ref tx) = event_tx {
                            let _ = tx.send(AgentEvent::Progress(format!(
                                "Primary provider unavailable, using fallback: {}/{}",
                                fb.provider, fb.model
                            )));
                        }
                        self.router
                            .route_required(&fb.provider, &fb.model)
                            .await?
                    } else {
                        return Err(e);
                    }
                }
            }
        };

        let turns_limit = if max_turns == 0 { self.max_turns } else { max_turns };

        let thinking = *self.thinking_level.read().await;
        let config = AgentConfig {
            max_turns: turns_limit,
            token_limit: 128_000,
            model: provider.model_name().to_string(),
            max_cost_usd: 10.0,
            loop_detection: true,
            custom_system_prompt: None,
            thinking_level: thinking,
        };

        // Auto-context: inject relevant memories into the goal
        let enriched_goal = self.enrich_goal_with_memories(goal).await;

        // Relevance-aware condenser: extract pagerank scores if index is ready
        let relevance_scores = {
            let guard = self.codebase_index.read().await;
            guard.as_ref().map(|idx| idx.pagerank.clone())
        };

        let summarizer: Arc<dyn Summarizer> = Arc::new(LlmSummarizer(provider.clone()));
        let condenser_config = CondenserConfig {
            max_tokens: config.token_limit,
            target_tokens: config.token_limit * 3 / 4,
            ..Default::default()
        };
        let condenser = create_hybrid_condenser_with_relevance(
            condenser_config.clone(),
            Some(summarizer),
            relevance_scores,
        );
        let context = ContextManager::new_with_condenser(condenser_config, condenser);

        // Build a fresh registry WITH MCP tools (fixes the bug where run() dropped them)
        let mut registry = build_tool_registry(self.platform.clone());
        register_memory_tools(&mut registry, self.memory.clone());
        register_session_tools(&mut registry, self.session_manager.clone());
        register_codebase_tools(&mut registry, self.codebase_index.clone());
        register_custom_tools(&mut registry, &self.custom_tool_dirs);

        // Re-register MCP tools from the runtime
        {
            let mcp_guard = self.mcp.read().await;
            if let Some(ref runtime) = *mcp_guard {
                for (server_name, tool_def) in &runtime.tools_with_source {
                    let source = ToolSource::MCP {
                        server: server_name.clone(),
                    };
                    registry.register_with_source(
                        MCPBridgeTool::new(tool_def.clone(), runtime.caller.clone()),
                        source,
                    );
                }
            }
        }

        let mut agent = AgentLoop::new(
            Box::new(SharedProvider::new(provider)),
            registry,
            context,
            config,
        )
        .with_history(history);
        let goal = &enriched_goal;

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
                            AgentEvent::Thinking(_) | AgentEvent::Progress(_) | AgentEvent::Token(_) | AgentEvent::ToolCall(_) | AgentEvent::ToolResult(_) | AgentEvent::ToolStats(_) | AgentEvent::TokenUsage { .. } => {}
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

/// Initialize MCP from merged global+project configs, registering bridge tools into the registry.
async fn init_mcp(
    global_config: &std::path::Path,
    project_config: &std::path::Path,
    registry: &mut ToolRegistry,
) -> Option<MCPRuntime> {
    match load_merged_mcp_config(global_config, project_config).await {
        Ok(configs) if !configs.is_empty() => {
            let mut manager = ExtensionManager::new();
            if let Err(e) = manager.initialize(configs).await {
                warn!(error = %e, "Failed to initialize MCP servers");
                return None;
            }

            let server_count = manager.server_count();
            let mcp_tools_with_server = manager.list_tools_with_server().to_vec();
            let mcp_tools = manager.list_tools();
            let caller: Arc<dyn MCPToolCaller> =
                Arc::new(ExtensionManagerCaller { manager });

            let mut tools_with_source = Vec::new();
            for (server_name, mcp_tool) in &mcp_tools_with_server {
                if let Some(tool_def) = mcp_tools.iter().find(|t| t.name == mcp_tool.name) {
                    tools_with_source.push((server_name.clone(), tool_def.clone()));
                }
            }

            let tool_count = tools_with_source.len();

            for (server_name, tool_def) in &tools_with_source {
                info!(tool = %tool_def.name, server = %server_name, "Registering MCP tool");
                let source = ToolSource::MCP {
                    server: server_name.clone(),
                };
                registry.register_with_source(
                    MCPBridgeTool::new(tool_def.clone(), caller.clone()),
                    source,
                );
            }

            info!(servers = server_count, tools = tool_count, "MCP initialized");

            Some(MCPRuntime {
                caller,
                server_count,
                tool_count,
                tools_with_source,
            })
        }
        Ok(_) => None,
        Err(e) => {
            warn!(error = %e, "Failed to load MCP config, continuing without MCP tools");
            None
        }
    }
}

const STOPWORDS: &[&str] = &[
    "a", "an", "the", "is", "are", "was", "were", "be", "been", "being", "have", "has", "had",
    "do", "does", "did", "will", "would", "could", "should", "may", "might", "can", "shall",
    "to", "of", "in", "for", "on", "with", "at", "by", "from", "as", "into", "about", "it",
    "its", "this", "that", "and", "or", "but", "not", "no", "so", "if", "then", "than", "too",
    "very", "just", "how", "what", "which", "who", "when", "where", "why", "all", "each",
    "me", "my", "i", "you", "your", "we", "our", "he", "she", "they", "them",
];

fn extract_goal_keywords(goal: &str) -> Vec<String> {
    goal.split(|c: char| !c.is_alphanumeric() && c != '_' && c != '-')
        .map(|s| s.trim().to_lowercase())
        .filter(|s| s.len() > 2 && !STOPWORDS.contains(&s.as_str()))
        .collect()
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
