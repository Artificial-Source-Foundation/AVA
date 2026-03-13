use std::path::PathBuf;
use std::sync::Arc;

use async_trait::async_trait;
use ava_codebase::indexer::index_project;
use ava_codebase::CodebaseIndex;
use ava_config::{AgentsConfig, ConfigManager};
use ava_context::{
    create_hybrid_condenser_with_relevance, CondenserConfig, ContextManager, Summarizer,
};
use ava_llm::provider::{LLMProvider, SharedProvider};
use ava_llm::{ModelRouter, RouteDecision, RouteSource};
use ava_mcp::config::load_merged_mcp_config;
use ava_mcp::manager::ExtensionManager;
use ava_memory::MemorySystem;
use ava_platform::{Platform, StandardPlatform};
use ava_session::SessionManager;
use ava_tools::core::question::QuestionBridge;
use ava_tools::core::task::{TaskResult, TaskSpawner};
use ava_tools::core::{
    register_core_tools, register_custom_tools, register_default_tools, register_extended_tools,
    register_question_tool, register_task_tool, register_todo_tools,
};
use ava_tools::mcp_bridge::{MCPBridgeTool, MCPToolCaller};
use ava_tools::registry::{ToolRegistry, ToolSource};
use ava_types::{
    AvaError, QueuedMessage, Result, Role, Session, ThinkingLevel, TodoState, ToolResult,
};
use futures::StreamExt;
use serde_json::Value;
use tokio::sync::{mpsc, RwLock};
use tokio_util::sync::CancellationToken;

use tracing::{info, instrument, warn};

use crate::agent_loop::{AgentConfig, AgentEvent, AgentLoop};
use crate::routing::analyze_task;

struct LlmSummarizer(Arc<dyn LLMProvider>);

#[async_trait]
impl Summarizer for LlmSummarizer {
    async fn summarize(&self, text: &str) -> std::result::Result<String, String> {
        use ava_types::{Message, Role};
        let messages = vec![Message::new(Role::User, text.to_string())];
        self.0.generate(&messages).await.map_err(|e| e.to_string())
    }
}

struct ExtensionManagerCaller {
    manager: ExtensionManager,
}

#[async_trait]
impl MCPToolCaller for ExtensionManagerCaller {
    async fn call_tool(&self, name: &str, arguments: Value) -> Result<ToolResult> {
        self.manager.call_tool(name, arguments).await
    }
}

struct MCPRuntime {
    caller: Arc<dyn MCPToolCaller>,
    server_count: usize,
    tool_count: usize,
    tools_with_source: Vec<(String, ava_types::Tool)>,
}

#[derive(Debug, Clone)]
pub struct MCPServerInfo {
    pub name: String,
    pub tool_count: usize,
}

pub struct AgentStack {
    pub router: ModelRouter,
    pub tools: Arc<RwLock<ToolRegistry>>,
    pub session_manager: Arc<SessionManager>,
    pub memory: Arc<MemorySystem>,
    pub config: ConfigManager,
    pub platform: Arc<dyn Platform>,
    pub codebase_index: Arc<RwLock<Option<Arc<CodebaseIndex>>>>,
    provider_override: RwLock<Option<String>>,
    model_override: RwLock<Option<String>>,
    routing_locked: RwLock<bool>,
    max_turns: usize,
    max_budget_usd: f64,
    #[allow(dead_code)] // Field retained for future use; accessor removed as dead code
    yolo: bool,
    injected_provider: Option<Arc<dyn LLMProvider>>,
    mcp: Arc<RwLock<Option<MCPRuntime>>>,
    custom_tool_dirs: Vec<PathBuf>,
    mcp_global_config: PathBuf,
    mcp_project_config: PathBuf,
    pub thinking_level: RwLock<ThinkingLevel>,
    pub mode_prompt_suffix: RwLock<Option<String>>,
    /// When true, agent is in Plan mode — write/edit restricted to .ava/plans/*.md.
    pub plan_mode: RwLock<bool>,
    pub todo_state: TodoState,
    /// Bridge for the question tool to communicate with the TUI.
    question_bridge: QuestionBridge,
    /// Sub-agent configuration loaded from agents.toml files.
    agents_config: AgentsConfig,
    /// Parent session ID for linking sub-agent sessions back to their parent.
    /// Set by the TUI before calling `run()` so spawned sub-agents record lineage.
    pub parent_session_id: RwLock<Option<String>>,
}

pub struct AgentStackConfig {
    pub data_dir: PathBuf,
    pub provider: Option<String>,
    pub model: Option<String>,
    pub max_turns: usize,
    pub max_budget_usd: f64,
    pub yolo: bool,
    pub injected_provider: Option<Arc<dyn LLMProvider>>,
    /// Override the working directory for the agent. When set, the agent uses
    /// this path instead of `std::env::current_dir()` for project-root detection,
    /// MCP config lookup, custom tool discovery, and codebase indexing. Useful for
    /// benchmarks and sandboxed runs that should not touch the real project.
    pub working_dir: Option<PathBuf>,
}

#[derive(Debug)]
pub struct AgentRunResult {
    pub success: bool,
    pub turns: usize,
    pub session: Session,
}

#[derive(Debug, Default)]
struct BudgetTelemetry {
    input_tokens: usize,
    output_tokens: usize,
    total_cost_usd: f64,
    max_budget_usd: f64,
    last_alert_threshold_percent: Option<u8>,
    emitted_thresholds: Vec<u8>,
    skipped_follow_up_messages: usize,
    skipped_post_complete_groups: usize,
    skipped_post_complete_messages: usize,
}

impl BudgetTelemetry {
    const ALERT_THRESHOLDS: [u8; 3] = [50, 75, 90];

    fn new(max_budget_usd: f64) -> Self {
        Self {
            max_budget_usd,
            ..Self::default()
        }
    }

    fn observe(&mut self, event: &AgentEvent) -> Vec<AgentEvent> {
        match event {
            AgentEvent::TokenUsage {
                input_tokens,
                output_tokens,
                cost_usd,
            } => {
                self.input_tokens += input_tokens;
                self.output_tokens += output_tokens;
                self.total_cost_usd += cost_usd;
            }
            AgentEvent::SubAgentComplete {
                input_tokens,
                output_tokens,
                cost_usd,
                ..
            } => {
                self.input_tokens += input_tokens;
                self.output_tokens += output_tokens;
                self.total_cost_usd += cost_usd;
            }
            _ => return Vec::new(),
        }

        if self.max_budget_usd <= 0.0 {
            return Vec::new();
        }

        let percent_used = self.total_cost_usd / self.max_budget_usd * 100.0;
        let mut warnings = Vec::new();
        for threshold in Self::ALERT_THRESHOLDS {
            if percent_used >= f64::from(threshold) && !self.emitted_thresholds.contains(&threshold)
            {
                self.emitted_thresholds.push(threshold);
                self.last_alert_threshold_percent = Some(threshold);
                warnings.push(AgentEvent::BudgetWarning {
                    threshold_percent: threshold,
                    current_cost_usd: self.total_cost_usd,
                    max_budget_usd: self.max_budget_usd,
                });
            }
        }
        warnings
    }

    fn attach_to_session(&self, session: &mut Session) {
        session.metadata["costSummary"] = serde_json::json!({
            "totalUsd": self.total_cost_usd,
            "budgetUsd": (self.max_budget_usd > 0.0).then_some(self.max_budget_usd),
            "inputTokens": self.input_tokens,
            "outputTokens": self.output_tokens,
            "lastAlertThresholdPercent": self.last_alert_threshold_percent,
            "skippedQueuedFollowUps": self.skipped_follow_up_messages,
            "skippedQueuedPostCompleteGroups": self.skipped_post_complete_groups,
            "skippedQueuedPostCompleteMessages": self.skipped_post_complete_messages,
        });
    }

    fn remaining_budget_usd(&self) -> Option<f64> {
        if self.max_budget_usd <= 0.0 {
            None
        } else {
            Some((self.max_budget_usd - self.total_cost_usd).max(0.0))
        }
    }

    fn budget_exhausted(&self) -> bool {
        self.remaining_budget_usd()
            .is_some_and(|remaining| remaining <= 0.0)
    }

    fn budget_status_label(&self) -> String {
        if self.max_budget_usd > 0.0 {
            format!("${:.2}/${:.2}", self.total_cost_usd, self.max_budget_usd)
        } else {
            format!("${:.2}", self.total_cost_usd)
        }
    }

    fn record_skipped_follow_up_messages(&mut self, count: usize) {
        self.skipped_follow_up_messages += count;
    }

    fn record_skipped_post_complete_group(&mut self, message_count: usize) {
        self.skipped_post_complete_groups += 1;
        self.skipped_post_complete_messages += message_count;
    }
}

impl Default for AgentStackConfig {
    fn default() -> Self {
        Self {
            data_dir: dirs::home_dir().unwrap_or_default().join(".ava"),
            provider: None,
            model: None,
            max_turns: 0,
            max_budget_usd: 0.0,
            yolo: false,
            injected_provider: None,
            working_dir: None,
        }
    }
}

impl AgentStack {
    /// Create a new `AgentStack`.
    ///
    /// Returns the stack and a receiver for question requests. The caller
    /// (typically the TUI) should poll this receiver and present questions to the
    /// user, sending answers back via the embedded oneshot channel.
    pub async fn new(
        config: AgentStackConfig,
    ) -> Result<(
        Self,
        mpsc::UnboundedReceiver<ava_tools::core::question::QuestionRequest>,
    )> {
        tokio::fs::create_dir_all(&config.data_dir)
            .await
            .map_err(|e| AvaError::IoError(e.to_string()))?;

        let db_path = config.data_dir.join("data.db");
        let config_path = config.data_dir.join("config.yaml");
        let credentials_path = config.data_dir.join("credentials.json");

        let platform: Arc<dyn Platform> = Arc::new(StandardPlatform);

        let config_mgr = ConfigManager::load_from_paths(config_path, credentials_path).await?;
        let credentials = config_mgr.credentials().await;
        let router = ModelRouter::new(credentials);

        let session_manager = Arc::new(SessionManager::new(&db_path)?);
        let memory = Arc::new(
            MemorySystem::new(&db_path).map_err(|e| AvaError::DatabaseError(e.to_string()))?,
        );

        let effective_cwd = config
            .working_dir
            .clone()
            .unwrap_or_else(|| std::env::current_dir().unwrap_or_default());

        let codebase_index: Arc<RwLock<Option<Arc<CodebaseIndex>>>> = Arc::new(RwLock::new(None));
        let index_clone = codebase_index.clone();
        let project_root = effective_cwd.clone();
        tokio::spawn(async move {
            match index_project(&project_root).await {
                Ok(idx) => {
                    *index_clone.write().await = Some(Arc::new(idx));
                    info!("Codebase indexing complete");
                }
                Err(e) => warn!("Codebase indexing failed: {e}"),
            }
        });

        let cfg = config_mgr.get().await;
        let routing_locked = config
            .provider
            .as_deref()
            .is_some_and(|provider| provider != cfg.llm.provider)
            || config
                .model
                .as_deref()
                .is_some_and(|model| model != cfg.llm.model);
        let provider_name = config.provider.as_deref().unwrap_or(&cfg.llm.provider);
        if let Some(base_url) = ava_llm::providers::base_url_for_provider(provider_name) {
            match router.pool().get_client(base_url).await {
                Ok(_) => info!(base_url, "Pre-warmed connection pool"),
                Err(e) => warn!(%e, base_url, "Failed to pre-warm connection pool"),
            }
        }

        let mcp_global_config = config.data_dir.join("mcp.json");
        let mcp_project_config = effective_cwd.join(".ava").join("mcp.json");
        let custom_tool_dirs = vec![
            config.data_dir.join("tools"),
            effective_cwd.join(".ava").join("tools"),
        ];

        let agents_config = AgentsConfig::load(
            &config.data_dir.join("agents.toml"),
            &effective_cwd.join(".ava").join("agents.toml"),
        );

        let todo_state = TodoState::new();
        let (question_bridge, question_rx) = QuestionBridge::new();

        let mut registry = build_tool_registry(platform.clone());
        register_todo_tools(&mut registry, todo_state.clone());
        register_question_tool(&mut registry, question_bridge.clone());
        register_custom_tools(&mut registry, &custom_tool_dirs);

        let mcp_runtime = init_mcp(&mcp_global_config, &mcp_project_config, &mut registry).await;
        let tools = Arc::new(RwLock::new(registry));

        Ok((
            Self {
                router,
                tools,
                session_manager,
                memory,
                config: config_mgr,
                platform,
                codebase_index,
                provider_override: RwLock::new(config.provider),
                model_override: RwLock::new(config.model),
                routing_locked: RwLock::new(routing_locked),
                max_turns: config.max_turns,
                max_budget_usd: config.max_budget_usd,
                yolo: config.yolo,
                injected_provider: config.injected_provider,
                mcp: Arc::new(RwLock::new(mcp_runtime)),
                custom_tool_dirs,
                mcp_global_config,
                mcp_project_config,
                thinking_level: RwLock::new(ThinkingLevel::Off),
                mode_prompt_suffix: RwLock::new(None),
                plan_mode: RwLock::new(false),
                todo_state,
                question_bridge,
                agents_config,
                parent_session_id: RwLock::new(None),
            },
            question_rx,
        ))
    }

    pub async fn mcp_server_count(&self) -> usize {
        self.mcp.read().await.as_ref().map_or(0, |r| r.server_count)
    }

    pub async fn mcp_tool_count(&self) -> usize {
        self.mcp.read().await.as_ref().map_or(0, |r| r.tool_count)
    }

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

    pub async fn reload_mcp(&self) -> Result<(usize, usize)> {
        let mut registry = self.tools.write().await;
        registry.remove_by_source(|src| matches!(src, ToolSource::MCP { .. }));
        let runtime = init_mcp(
            &self.mcp_global_config,
            &self.mcp_project_config,
            &mut registry,
        )
        .await;
        let counts = runtime
            .as_ref()
            .map_or((0, 0), |r| (r.server_count, r.tool_count));
        *self.mcp.write().await = runtime;
        Ok(counts)
    }

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

    pub async fn reload_tools(&self) -> Result<usize> {
        let mut registry = build_tool_registry(self.platform.clone());
        register_todo_tools(&mut registry, self.todo_state.clone());
        register_question_tool(&mut registry, self.question_bridge.clone());
        register_custom_tools(&mut registry, &self.custom_tool_dirs);
        let runtime = init_mcp(
            &self.mcp_global_config,
            &self.mcp_project_config,
            &mut registry,
        )
        .await;
        let count = registry.tool_count();
        *self.tools.write().await = registry;
        *self.mcp.write().await = runtime;
        Ok(count)
    }

    #[instrument(skip(self))]
    pub async fn switch_model(&self, provider: &str, model: &str) -> Result<()> {
        self.router.route_required(provider, model).await?;
        *self.provider_override.write().await = Some(provider.to_string());
        *self.model_override.write().await = Some(model.to_string());
        *self.routing_locked.write().await = true;
        let project_root = std::env::current_dir().unwrap_or_default();
        let mut state = ava_config::ProjectState::load(&project_root);
        state.last_provider = Some(provider.to_string());
        state.last_model = Some(model.to_string());
        let _ = state.save(&project_root);
        Ok(())
    }

    pub async fn set_mode_prompt_suffix(&self, suffix: Option<String>) {
        *self.mode_prompt_suffix.write().await = suffix;
    }

    pub async fn set_plan_mode(&self, enabled: bool) {
        *self.plan_mode.write().await = enabled;
    }

    pub async fn set_thinking_level(&self, level: ThinkingLevel) {
        *self.thinking_level.write().await = level;
    }

    pub async fn cycle_thinking(&self) -> &'static str {
        let mut guard = self.thinking_level.write().await;
        *guard = guard.cycle();
        guard.label()
    }

    pub async fn get_thinking_level(&self) -> ThinkingLevel {
        *self.thinking_level.read().await
    }

    /// Create a message queue for mid-stream messaging.
    /// Returns the queue (to pass into `run()`) and the sender (for the TUI to send messages).
    pub fn create_message_queue(
        &self,
    ) -> (
        crate::message_queue::MessageQueue,
        mpsc::UnboundedSender<QueuedMessage>,
    ) {
        crate::message_queue::MessageQueue::new()
    }

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

    pub async fn resolve_model_route(
        &self,
        goal: &str,
        images: &[ava_types::ImageContent],
    ) -> Result<RouteDecision> {
        let cfg = self.config.get().await;
        let (provider, model) = self.current_model().await;
        let thinking = *self.thinking_level.read().await;
        let plan_mode = *self.plan_mode.read().await;
        let intent = analyze_task(goal, images, thinking, plan_mode);
        let intent_reasons = intent.reasons.clone();

        if *self.routing_locked.read().await {
            let mut reasons = intent.reasons;
            reasons
                .push("provider/model override is locked; skipping automatic routing".to_string());
            return Ok(RouteDecision::fixed(
                provider,
                model,
                intent.profile,
                RouteSource::ManualOverride,
                reasons,
            ));
        }

        let mut decision = self
            .router
            .decide_route(
                &provider,
                &model,
                &cfg.llm.routing,
                intent.profile,
                intent.requirements,
            )
            .await;
        if !intent_reasons.is_empty() {
            let mut reasons = intent_reasons;
            reasons.extend(decision.reasons);
            decision.reasons = reasons;
        }
        Ok(decision)
    }

    fn attach_route_metadata(session: &mut Session, decision: &RouteDecision) {
        session.metadata["routing"] = serde_json::json!({
            "provider": decision.provider,
            "model": decision.model,
            "displayModel": decision.display_model,
            "profile": match decision.profile {
                ava_config::RoutingProfile::Cheap => "cheap",
                ava_config::RoutingProfile::Capable => "capable",
            },
            "source": decision.source.as_str(),
            "reasons": decision.reasons,
            "costPerMillion": {
                "input": decision.cost_input_per_million,
                "output": decision.cost_output_per_million,
            }
        });
    }

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
        let memory_block = entries.join("\n");
        let memory_block = if memory_block.len() > 2000 {
            format!("{}...", &memory_block[..2000])
        } else {
            memory_block
        };
        format!("{goal}\n\nRelevant memories:\n{memory_block}")
    }

    #[allow(clippy::too_many_arguments)]
    #[instrument(
        skip(self, event_tx, cancel, history, message_queue, images),
        fields(max_turns)
    )]
    pub async fn run(
        &self,
        goal: &str,
        max_turns: usize,
        event_tx: Option<mpsc::UnboundedSender<AgentEvent>>,
        cancel: CancellationToken,
        history: Vec<ava_types::Message>,
        message_queue: Option<crate::message_queue::MessageQueue>,
        images: Vec<ava_types::ImageContent>,
    ) -> Result<AgentRunResult> {
        let cfg = self.config.get().await;
        let mut resolved_provider_name = self.current_model().await.0;
        let route_decision = if self.injected_provider.is_none() {
            Some(self.resolve_model_route(goal, &images).await?)
        } else {
            None
        };
        let mut applied_route_decision = route_decision.clone();
        if let (Some(tx), Some(decision)) = (&event_tx, &route_decision) {
            let _ = tx.send(AgentEvent::Progress(format!(
                "{}: {}",
                decision.summary(),
                decision.reasons.join("; ")
            )));
        }
        let provider = if let Some(provider) = &self.injected_provider {
            provider.clone()
        } else {
            let decision = route_decision.as_ref().expect("route decision exists");
            resolved_provider_name = decision.provider.clone();
            match self
                .router
                .route_required(&decision.provider, &decision.model)
                .await
            {
                Ok(p) => p,
                Err(e) => {
                    if let Some(fb) = &cfg.fallback {
                        warn!(
                            primary = %decision.provider,
                            fallback = %fb.provider,
                            "Primary provider unavailable, using fallback"
                        );
                        if let Some(ref tx) = event_tx {
                            let _ = tx.send(AgentEvent::Progress(format!(
                                "Primary provider unavailable, using fallback: {}/{}",
                                fb.provider, fb.model
                            )));
                        }
                        resolved_provider_name = fb.provider.clone();
                        applied_route_decision = Some(RouteDecision::fixed(
                            fb.provider.clone(),
                            fb.model.clone(),
                            decision.profile,
                            RouteSource::Fallback,
                            vec![format!(
                                "routed model was unavailable; fell back to {}/{}",
                                fb.provider, fb.model
                            )],
                        ));
                        self.router.route_required(&fb.provider, &fb.model).await?
                    } else {
                        return Err(e);
                    }
                }
            }
        };

        // 0 = unlimited everywhere; explicit non-zero arg overrides stored config
        let turns_limit = if max_turns > 0 {
            max_turns
        } else {
            self.max_turns // may also be 0 (unlimited)
        };
        let thinking = *self.thinking_level.read().await;
        let thinking_budget_tokens = cfg
            .llm
            .thinking_budgets
            .resolve(&resolved_provider_name, provider.model_name());
        let plan_mode = *self.plan_mode.read().await;
        let mode_suffix = self.mode_prompt_suffix.read().await.clone();

        // Build system prompt suffix: mode instructions + project instructions
        let project_instructions =
            crate::instructions::load_project_instructions_with_config(&cfg.instructions);
        if let Some(ref pi) = project_instructions {
            info!(
                bytes = pi.len(),
                "Loaded project instructions into system prompt"
            );
        }
        let system_prompt_suffix = match (mode_suffix, project_instructions) {
            (Some(mode), Some(proj)) => Some(format!("{mode}\n\n{proj}")),
            (Some(mode), None) => Some(mode),
            (None, Some(proj)) => Some(proj),
            (None, None) => None,
        };

        let config = AgentConfig {
            max_turns: turns_limit,
            max_budget_usd: self.max_budget_usd,
            token_limit: 128_000,
            model: provider.model_name().to_string(),
            max_cost_usd: 10.0,
            loop_detection: true,
            custom_system_prompt: None,
            thinking_level: thinking,
            thinking_budget_tokens,
            system_prompt_suffix,
            extended_tools: false,
            plan_mode,
            post_edit_validation: None,
        };

        let enriched_goal = self.enrich_goal_with_memories(goal).await;
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

        let mut registry = build_tool_registry(self.platform.clone());
        register_todo_tools(&mut registry, self.todo_state.clone());
        register_question_tool(&mut registry, self.question_bridge.clone());
        register_custom_tools(&mut registry, &self.custom_tool_dirs);

        let spawner: Arc<dyn TaskSpawner> = Arc::new(AgentTaskSpawner {
            provider: provider.clone(),
            platform: self.platform.clone(),
            model_name: provider.model_name().to_string(),
            max_turns: turns_limit,
            agents_config: self.agents_config.clone(),
            event_tx: event_tx.clone(),
            session_manager: Some(self.session_manager.clone()),
            parent_session_id: {
                let guard = self.parent_session_id.read().await;
                guard.clone()
            },
        });
        register_task_tool(&mut registry, spawner);

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

        // Attach images if provided (multimodal input)
        if !images.is_empty() {
            agent = agent.with_images(images);
        }

        // Attach message queue if provided (enables steering/follow-up/post-complete)
        let mut queue = message_queue;
        if let Some(q) = queue.take() {
            agent = agent.with_message_queue(q);
        }

        let goal = &enriched_goal;

        if let Some(tx) = event_tx {
            // --- Helper: run agent streaming and collect until Complete ---
            async fn run_streaming_until_complete(
                agent: &mut AgentLoop,
                goal_str: &str,
                tx: &mpsc::UnboundedSender<AgentEvent>,
                cancel: &CancellationToken,
                telemetry: &mut BudgetTelemetry,
            ) -> Result<Session> {
                agent.config.max_budget_usd = telemetry.remaining_budget_usd().unwrap_or(0.0);
                let mut stream = agent.run_streaming(goal_str).await;
                let mut final_session: Option<Session> = None;
                loop {
                    tokio::select! {
                        _ = cancel.cancelled() => {
                            return Err(AvaError::Cancelled);
                        }
                        maybe_event = stream.next() => {
                            let Some(event) = maybe_event else { break; };
                            let warnings = telemetry.observe(&event);
                            match event {
                                AgentEvent::Complete(mut session) => {
                                    telemetry.attach_to_session(&mut session);
                                    let complete = AgentEvent::Complete(session.clone());
                                    let _ = tx.send(complete);
                                    final_session = Some(session);
                                    break;
                                }
                                AgentEvent::Error(error) => {
                                    let _ = tx.send(AgentEvent::Error(error.clone()));
                                    return Err(AvaError::AgentStopped { reason: error });
                                }
                                other => {
                                    let _ = tx.send(other);
                                }
                            }
                            for warning in warnings {
                                let _ = tx.send(warning);
                            }
                        }
                    }
                }
                final_session.ok_or_else(|| {
                    AvaError::ToolError(
                        "Agent stream ended unexpectedly without a completion event. \
                         This may indicate the model returned no actionable response"
                            .to_string(),
                    )
                })
            }

            let mut telemetry = BudgetTelemetry::new(self.max_budget_usd);

            // --- Primary run ---
            let mut session =
                run_streaming_until_complete(&mut agent, goal, &tx, &cancel, &mut telemetry)
                    .await?;

            // --- Follow-up loop (Tier 2): check for queued follow-up messages ---
            // Discard any leftover steering messages — they are only meaningful while
            // tools are executing and should not leak into the follow-up phase.
            if let Some(ref mut queue) = agent.message_queue {
                queue.poll();
                queue.clear_steering();
            }
            loop {
                let follow_ups = {
                    match agent.message_queue {
                        Some(ref mut queue) => {
                            queue.poll();
                            if queue.has_follow_up() {
                                queue.drain_follow_up()
                            } else {
                                break;
                            }
                        }
                        None => break,
                    }
                };
                let follow_up_count = follow_ups.len();
                for (index, text) in follow_ups.into_iter().enumerate() {
                    if cancel.is_cancelled() {
                        return Err(AvaError::Cancelled);
                    }
                    if telemetry.budget_exhausted() {
                        let remaining = follow_up_count.saturating_sub(index);
                        telemetry.record_skipped_follow_up_messages(remaining);
                        let _ = tx.send(AgentEvent::Progress(
                            format!(
                                "budget exhausted at {} — skipping {remaining} queued follow-up message(s)",
                                telemetry.budget_status_label()
                            ),
                        ));
                        break;
                    }
                    let prefixed = format!("[User follow-up] {text}");
                    let _ = tx.send(AgentEvent::Progress(format!("follow-up: {text}")));

                    // run_streaming will inject this as the goal message, so we only
                    // need to send the prefixed version as the goal — no manual injection.
                    session = run_streaming_until_complete(
                        &mut agent,
                        &prefixed,
                        &tx,
                        &cancel,
                        &mut telemetry,
                    )
                    .await?;
                }
            }

            // --- Post-complete pipeline loop (Tier 3): run grouped stages ---
            loop {
                let group_data = {
                    match agent.message_queue {
                        Some(ref mut queue) => {
                            queue.poll();
                            match queue.next_post_complete_group() {
                                Some(pair) => pair,
                                None => break,
                            }
                        }
                        None => break,
                    }
                };
                let (group_id, messages) = group_data;

                if cancel.is_cancelled() {
                    if let Some(ref mut queue) = agent.message_queue {
                        queue.finish_post_complete_group();
                    }
                    return Err(AvaError::Cancelled);
                }
                if telemetry.budget_exhausted() {
                    telemetry.record_skipped_post_complete_group(messages.len());
                    let _ = tx.send(AgentEvent::Progress(format!(
                        "budget exhausted at {} — skipping post-complete group {group_id} ({} message(s))",
                        telemetry.budget_status_label(),
                        messages.len()
                    )));
                    if let Some(ref mut queue) = agent.message_queue {
                        queue.finish_post_complete_group();
                        queue.advance_post_group();
                    }
                    continue;
                }

                // Combine all messages from this group into a single goal
                let combined = messages.join("\n\n");
                let prefixed = format!("[User post-complete (group {group_id})] {combined}");
                let msg_count = messages.len();
                let _ = tx.send(AgentEvent::Progress(format!(
                    "post-complete group {group_id}: {msg_count} message(s)"
                )));

                // run_streaming will inject this as the goal message — no manual injection.
                let group_result = run_streaming_until_complete(
                    &mut agent,
                    &prefixed,
                    &tx,
                    &cancel,
                    &mut telemetry,
                )
                .await;

                if let Some(ref mut queue) = agent.message_queue {
                    queue.finish_post_complete_group();
                    queue.advance_post_group();
                }

                match group_result {
                    Ok(s) => session = s,
                    Err(e) => {
                        info!(group_id, error = %e, "post-complete group failed");
                        // Continue to next group on failure
                    }
                }
            }

            telemetry.attach_to_session(&mut session);
            if let Some(decision) = applied_route_decision.as_ref() {
                Self::attach_route_metadata(&mut session, decision);
            }
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

        let mut session = session;
        if let Some(decision) = applied_route_decision.as_ref() {
            Self::attach_route_metadata(&mut session, decision);
        }

        Ok(AgentRunResult {
            success: true,
            turns: session.messages.len(),
            session,
        })
    }
}

struct AgentTaskSpawner {
    provider: Arc<dyn LLMProvider>,
    platform: Arc<dyn Platform>,
    model_name: String,
    max_turns: usize,
    agents_config: AgentsConfig,
    /// Optional event sender to emit `SubAgentComplete` events for TUI consumption.
    event_tx: Option<mpsc::UnboundedSender<AgentEvent>>,
    /// Session manager for persisting sub-agent sessions.
    session_manager: Option<Arc<SessionManager>>,
    /// Parent session ID for linking sub-agent sessions.
    parent_session_id: Option<String>,
}

#[async_trait]
impl TaskSpawner for AgentTaskSpawner {
    async fn spawn(&self, prompt: &str) -> Result<TaskResult> {
        let resolved = self.agents_config.get_agent("task");

        if !resolved.enabled {
            return Err(AvaError::ToolError(
                "Sub-agent 'task' is disabled in agents.toml".to_string(),
            ));
        }

        info!(
            prompt_len = prompt.len(),
            "spawning sub-agent for task tool"
        );
        let mut registry = ToolRegistry::new();
        register_core_tools(&mut registry, self.platform.clone());
        registry.unregister("todo_write");
        registry.unregister("todo_read");
        let context = ContextManager::new(128_000);

        // Use configured max_turns if set. If parent is unlimited (0), keep sub-agent bounded
        // by its own configured/default cap. Otherwise, cap at parent's max_turns.
        let sub_max_turns = if self.max_turns == 0 {
            resolved.max_turns.unwrap_or(10)
        } else {
            resolved.max_turns.unwrap_or(10).min(self.max_turns)
        };

        // Use configured prompt if set, otherwise fall back to default.
        let system_prompt = resolved
            .prompt
            .unwrap_or_else(build_sub_agent_system_prompt);

        // TODO: support model override from resolved.model — requires routing
        // through ModelRouter which the spawner doesn't currently have access to.

        let config = AgentConfig {
            max_turns: sub_max_turns,
            max_budget_usd: 0.0, // sub-agents don't get CLI budget
            token_limit: 128_000,
            model: self.model_name.clone(),
            max_cost_usd: 5.0,
            loop_detection: true,
            custom_system_prompt: Some(system_prompt),
            thinking_level: ThinkingLevel::Off,
            thinking_budget_tokens: None,
            system_prompt_suffix: crate::instructions::load_project_instructions(),
            extended_tools: true, // sub-agents get full tool access
            plan_mode: false,
            post_edit_validation: None,
        };
        let mut agent = AgentLoop::new(
            Box::new(SharedProvider::new(self.provider.clone())),
            registry,
            context,
            config,
        );
        let mut session = agent.run(prompt).await?;

        // Set parent_id metadata so the session can be linked to the parent.
        session.metadata["is_sub_agent"] = serde_json::Value::Bool(true);
        if let Some(ref parent_id) = self.parent_session_id {
            session.metadata["parent_id"] = serde_json::Value::String(parent_id.clone());
        }

        // Persist the sub-agent session if a session manager is available.
        if let Some(ref sm) = self.session_manager {
            if let Err(e) = sm.save(&session) {
                warn!(error = %e, "Failed to persist sub-agent session");
            }
        }

        let text = session
            .messages
            .iter()
            .rev()
            .find(|m| m.role == Role::Assistant && !m.content.trim().is_empty())
            .map(|m| m.content.clone())
            .unwrap_or_else(|| "Sub-agent completed but produced no output.".to_string());

        let session_id = session.id.to_string();
        let messages = session.messages.clone();
        let description = prompt.to_string();

        // Extract accumulated token usage from the sub-agent's session and compute cost.
        let sub_input_tokens = session.token_usage.input_tokens;
        let sub_output_tokens = session.token_usage.output_tokens;
        let (in_rate, out_rate) =
            ava_llm::providers::common::model_pricing_usd_per_million(&self.model_name);
        let sub_cost_usd = ava_llm::providers::common::estimate_cost_usd(
            sub_input_tokens,
            sub_output_tokens,
            in_rate,
            out_rate,
        );

        info!(
            result_len = text.len(),
            session_id = %session_id,
            message_count = messages.len(),
            sub_input_tokens,
            sub_output_tokens,
            sub_cost_usd,
            "sub-agent task completed"
        );

        // Emit SubAgentComplete event so the TUI can store the conversation.
        // The call_id is not available here (it's set by the tool registry),
        // so we use an empty string — the TUI matches by description instead.
        if let Some(ref tx) = self.event_tx {
            let _ = tx.send(AgentEvent::SubAgentComplete {
                call_id: String::new(),
                session_id: session_id.clone(),
                messages: messages.clone(),
                description: description.clone(),
                input_tokens: sub_input_tokens,
                output_tokens: sub_output_tokens,
                cost_usd: sub_cost_usd,
            });
        }

        Ok(TaskResult {
            text,
            session_id,
            messages,
        })
    }
}

fn build_sub_agent_system_prompt() -> String {
    "You are a sub-agent of AVA, an AI coding assistant. You have been given a specific task \
     to complete autonomously. Work through the task step by step using the available tools.\n\n\
     ## Rules\n\
     - Read files before modifying them.\n\
     - Prefer editing existing files over creating new ones.\n\
     - Be thorough but efficient -- you have a limited number of turns.\n\
     - When your task is complete, provide a clear summary of what you did as your final response.\n\
     - Do NOT call attempt_completion -- simply respond with your final answer when done.\n"
        .to_string()
}

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
            let caller: Arc<dyn MCPToolCaller> = Arc::new(ExtensionManagerCaller { manager });
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
            info!(
                servers = server_count,
                tools = tool_count,
                "MCP initialized"
            );
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
    "do", "does", "did", "will", "would", "could", "should", "may", "might", "can", "shall", "to",
    "of", "in", "for", "on", "with", "at", "by", "from", "as", "into", "about", "it", "its",
    "this", "that", "and", "or", "but", "not", "no", "so", "if", "then", "than", "too", "very",
    "just", "how", "what", "which", "who", "when", "where", "why", "all", "each", "me", "my", "i",
    "you", "your", "we", "our", "he", "she", "they", "them",
];

fn extract_goal_keywords(goal: &str) -> Vec<String> {
    goal.split(|c: char| !c.is_alphanumeric() && c != '_' && c != '-')
        .map(|s| s.trim().to_lowercase())
        .filter(|s| s.len() > 2 && !STOPWORDS.contains(&s.as_str()))
        .collect()
}

fn build_tool_registry(platform: Arc<dyn Platform>) -> ToolRegistry {
    let mut registry = ToolRegistry::new();
    register_default_tools(&mut registry, platform.clone());
    register_extended_tools(&mut registry, platform);
    registry
}

const _: () = {
    fn _assert_send<T: Send>() {}
    fn _check() {
        _assert_send::<AgentStack>();
    }
};
