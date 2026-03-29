//! Agent execution: the `run()` method and sub-agent spawning.

use std::collections::HashMap;
use std::sync::atomic::{AtomicUsize, Ordering};
use std::sync::Arc;
use std::time::Duration;

use async_trait::async_trait;
use ava_acp::{
    attach_delegation_record, transport_for_builtin_agent, AgentQuery, ExternalRunDescriptor,
    ExternalSessionMapper, PermissionMode,
};
use ava_config::AgentsConfig;
use ava_context::ContextManager;
use ava_llm::provider::{LLMProvider, SharedProvider};
use ava_llm::{ModelRouter, RouteDecision, RouteSource};
use ava_platform::Platform;
use ava_session::SessionManager;
use ava_tools::core::register_core_tools;
use ava_tools::core::task::{TaskResult, TaskSpawner};
use ava_tools::core::{
    file_backup::new_backup_session, register_custom_tools_with_plugins, register_plan_tool,
    register_question_tool, register_task_tool, register_todo_tools,
};
use ava_tools::mcp_bridge::MCPBridgeTool;
use ava_tools::permission_middleware::{convert_tool_source, SharedToolSources};
use ava_tools::registry::{ToolRegistry, ToolSource};
use ava_types::{
    AvaError, DelegationRecord, ExternalSessionLink, Result, Role, Session, ThinkingLevel,
};
use futures::StreamExt;
use tokio::sync::mpsc;
use tokio::time::timeout;
use tokio_util::sync::CancellationToken;
use tracing::{info, instrument, warn};

use super::stack_tools::{build_tool_registry_with_plugins, LlmSummarizer};
use super::{AgentRunResult, AgentStack};
use crate::agent_loop::{AgentConfig, AgentEvent, AgentLoop, LLM_STREAM_TIMEOUT_SECS};
use crate::budget::BudgetTelemetry;
use crate::routing::{analyze_task, analyze_task_full, EXPLICIT_DELEGATION_REASON};

const ADAPTIVE_DELEGATION_LOOKBACK: usize = 8;

impl AgentStack {
    async fn adapt_delegation_policy(
        &self,
        goal: &str,
        policy: crate::routing::SubagentDelegationPolicy,
    ) -> crate::routing::SubagentDelegationPolicy {
        let Some(parent_session_id) = self.parent_session_id.read().await.clone() else {
            return policy;
        };

        let Ok(parent_uuid) = uuid::Uuid::parse_str(&parent_session_id) else {
            return policy;
        };

        let Ok(recent) = self
            .session_manager
            .recent_delegation_records(parent_uuid, ADAPTIVE_DELEGATION_LOOKBACK)
        else {
            return policy;
        };

        adapt_delegation_policy_with_feedback(goal, policy, &recent)
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
        crate::memory_enrichment::enrich_goal_with_memories(&self.memory, goal).await
    }

    fn learn_project_patterns_from_goal(&self, goal: &str) {
        crate::memory_enrichment::learn_project_patterns_from_goal(&self.memory, goal);
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

    #[allow(clippy::too_many_arguments)]
    #[instrument(
        skip(self, event_tx, cancel, history, message_queue, images, session_id),
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
        session_id: Option<uuid::Uuid>,
    ) -> Result<AgentRunResult> {
        let raw_goal = goal.to_string();

        // Ensure MCP is initialized before building the per-run tool registry.
        // This awaits completion so MCP tools are available when we read self.mcp below.
        // Best-effort skip: configs created mid-run will be picked up on the next run.
        if self.mcp_global_config.exists() || self.mcp_project_config.exists() {
            self.ensure_mcp_initialized().await;
        }

        // Fire SessionStart plugin hook
        {
            let mut pm = self.plugin_manager.lock().await;
            if pm.has_hook_subscribers(ava_plugin::HookEvent::SessionStart) {
                pm.trigger_hook(
                    ava_plugin::HookEvent::SessionStart,
                    serde_json::json!({ "goal": raw_goal }),
                )
                .await;
            }
        }

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
        let task_analysis = analyze_task_full(goal, &images, thinking, plan_mode);
        let tool_visibility_profile = task_analysis.tool_visibility;
        let delegation_policy = self
            .adapt_delegation_policy(goal, task_analysis.delegation.clone())
            .await;
        let startup_instruction_profile = if !self.include_project_instructions {
            crate::instructions::StartupInstructionProfile::None
        } else {
            match tool_visibility_profile {
                crate::routing::ToolVisibilityProfile::AnswerOnly => {
                    crate::instructions::StartupInstructionProfile::None
                }
                crate::routing::ToolVisibilityProfile::ReadOnly => {
                    crate::instructions::StartupInstructionProfile::AgentsOnly
                }
                crate::routing::ToolVisibilityProfile::Full => {
                    crate::instructions::StartupInstructionProfile::Full
                }
            }
        };
        let mode_suffix = self.mode_prompt_suffix.read().await.clone();
        let project_root = self.permission_context.read().await.workspace_root.clone();
        // Consume any pending plan context (one-shot: only applies to this run).
        let plan_context = self.plan_context.write().await.take();

        // Build system prompt suffix: mode instructions + project instructions.
        // Uses spawn_blocking internally to avoid blocking the async executor
        // on synchronous file I/O while reading instruction files.
        let prompt_suffix_start = std::time::Instant::now();
        let prompt_suffix_fut = crate::instruction_resolver::build_system_prompt_suffix_async(
            mode_suffix,
            provider.model_name().to_string(),
            project_root.clone(),
            cfg.instructions.clone(),
            startup_instruction_profile,
        );

        let startup_context_fut = async {
            // Surface any panic from the background indexing task before we read
            // the index. If the task panicked the index will be None (empty), and
            // `check_index_status` will have already logged an actionable error.
            let index_status_fut = async {
                let start = std::time::Instant::now();
                self.check_index_status().await;
                start.elapsed()
            };
            let enrich_goal_fut = async {
                let start = std::time::Instant::now();
                let enriched = self.enrich_goal_with_memories(goal).await;
                (start.elapsed(), enriched)
            };
            tokio::join!(index_status_fut, enrich_goal_fut)
        };

        let (mut system_prompt_suffix, (index_status_elapsed, (memory_elapsed, enriched_goal))) =
            tokio::join!(prompt_suffix_fut, startup_context_fut);
        tracing::info!(
            elapsed_ms = prompt_suffix_start.elapsed().as_millis() as u64,
            suffix_chars = system_prompt_suffix.as_ref().map(|s| s.len()).unwrap_or(0),
            suffix_tokens = system_prompt_suffix
                .as_ref()
                .map(|s| ava_context::count_tokens_default(s))
                .unwrap_or(0),
            include_project_instructions = self.include_project_instructions,
            "system prompt suffix resolved"
        );
        tracing::info!(
            index_status_ms = index_status_elapsed.as_millis() as u64,
            memory_enrichment_ms = memory_elapsed.as_millis() as u64,
            goal_chars = goal.len(),
            "startup context preparation complete"
        );

        // Append approved plan context so the agent knows which steps to follow.
        if let Some(plan) = plan_context {
            let plan_section = format!(
                "\n\n## Approved Plan\n\
                 Follow this plan step by step. Mark each step as complete when done.\n\n{plan}"
            );
            match system_prompt_suffix.as_mut() {
                Some(suffix) => suffix.push_str(&plan_section),
                None => system_prompt_suffix = Some(plan_section),
            }
        }

        if delegation_policy.enable_task_tool {
            let delegation_section = format!(
                "\n\n## Hidden Delegation\n\
                 - Keep small, single-file work in the main thread.\n\
                 - You may use at most {} sub-agent(s) for this task.\n\
                 - Prefer `scout` or `explore` for read-only reconnaissance, `plan` for design-only breakdowns, `review` for a final pass, and `worker` or `task` for isolated implementation.\n\
                 - Delegate only when the returned result is easier to summarize than the full work.\n\
                 - Current guidance: {}.\n",
                delegation_policy.max_subagents, delegation_policy.reason
            );
            match system_prompt_suffix.as_mut() {
                Some(suffix) => suffix.push_str(&delegation_section),
                None => system_prompt_suffix = Some(delegation_section),
            }
        }

        // Look up the model's actual context window from the compiled-in registry.
        // This drives context compaction thresholds and ensures large-context models
        // (e.g. GPT-5.4 with 1M tokens) don't trigger premature compaction.
        // Falls back to 128K for unknown models.
        let model_context_window = {
            let model_id = provider.model_name();
            let reg = ava_config::model_catalog::registry::registry();
            reg.find(model_id)
                .map(|m| m.limits.context_window)
                .unwrap_or(128_000)
        };

        let relevance_scores = {
            let guard = self.codebase_index.read().await;
            guard.as_ref().map(|idx| {
                // Prefer symbol-level PageRank (aggregated to file level) over file-level
                if let Some(sg) = &idx.symbol_graph {
                    if !idx.symbol_pagerank.is_empty() {
                        return sg.aggregate_file_scores(&idx.symbol_pagerank);
                    }
                }
                idx.pagerank.clone()
            })
        };
        let compaction_provider =
            if let Some((provider_name, model_name)) = self.current_compaction_model().await {
                self.router
                    .route_required(&provider_name, &model_name)
                    .await?
            } else {
                provider.clone()
            };
        let summarizer: Arc<dyn ava_context::Summarizer> =
            Arc::new(LlmSummarizer(compaction_provider));
        let compaction_pct = *self.compaction_threshold_pct.read().await as f32 / 100.0;
        let condenser_config = ava_context::CondenserConfig {
            max_tokens: model_context_window,
            target_tokens: model_context_window * 3 / 4,
            compaction_threshold_pct: compaction_pct,
            preserve_recent_messages: 4,
            preserve_recent_turns: 2,
            ..Default::default()
        };
        let condenser = ava_context::create_hybrid_condenser_with_relevance(
            condenser_config.clone(),
            Some(summarizer),
            relevance_scores,
        );
        let context = ContextManager::new_with_condenser(condenser_config, condenser);

        let tool_registry_start = std::time::Instant::now();
        let (registry, run_tool_sources, run_backup_session): (
            ToolRegistry,
            SharedToolSources,
            ava_tools::core::file_backup::FileBackupSession,
        ) = if tool_visibility_profile == crate::routing::ToolVisibilityProfile::AnswerOnly {
            (
                ToolRegistry::new(),
                Arc::new(std::sync::RwLock::new(HashMap::new())),
                new_backup_session(),
            )
        } else {
            let (mut registry, run_tool_sources, run_backup_session) =
                build_tool_registry_with_plugins(
                    self.platform.clone(),
                    Arc::clone(&self.permission_inspector),
                    Arc::clone(&self.permission_context),
                    self.approval_bridge.clone(),
                    Some(Arc::clone(&self.plugin_manager)),
                );
            register_todo_tools(&mut registry, self.todo_state.clone());
            register_question_tool(&mut registry, self.question_bridge.clone());
            register_plan_tool(
                &mut registry,
                self.plan_bridge.clone(),
                self.plan_state.clone(),
            );
            register_custom_tools_with_plugins(
                &mut registry,
                &self.custom_tool_dirs,
                Some(Arc::clone(&self.plugin_manager)),
            );

            if delegation_policy.enable_task_tool {
                let spawner: Arc<dyn TaskSpawner> = Arc::new(AgentTaskSpawner {
                    provider: provider.clone(),
                    platform: self.platform.clone(),
                    model_name: provider.model_name().to_string(),
                    max_turns: turns_limit,
                    agents_config: self.agents_config.clone(),
                    router: self.router.clone(),
                    event_tx: event_tx.clone(),
                    session_manager: Some(self.session_manager.clone()),
                    parent_session_id: {
                        let guard = self.parent_session_id.read().await;
                        guard.clone()
                    },
                    depth: 0,
                    max_spawns: delegation_policy.max_subagents,
                    spawn_count: Arc::new(AtomicUsize::new(0)),
                });
                register_task_tool(&mut registry, spawner);
            }

            {
                let mcp_guard = self.mcp.read().await;
                if let Some(ref runtime) = *mcp_guard {
                    for (server_name, tool_def) in &runtime.tools_with_source {
                        let source = ToolSource::MCP {
                            server: server_name.clone(),
                        };
                        registry.register_with_source(
                            MCPBridgeTool::new(
                                tool_def.clone(),
                                runtime.caller.clone(),
                                server_name,
                            ),
                            source,
                        );
                    }
                }
            }

            (registry, run_tool_sources, run_backup_session)
        };
        tracing::info!(
            elapsed_ms = tool_registry_start.elapsed().as_millis() as u64,
            tool_count = registry.list_tools().len(),
            tool_profile = ?tool_visibility_profile,
            "run-scoped tool registry prepared"
        );

        let auto_compact = *self.auto_compact.read().await;
        let config = AgentConfig {
            max_turns: turns_limit,
            max_budget_usd: self.max_budget_usd,
            token_limit: model_context_window,
            provider: resolved_provider_name.clone(),
            model: provider.model_name().to_string(),
            max_cost_usd: 10.0,
            loop_detection: true,
            custom_system_prompt: None,
            thinking_level: thinking,
            thinking_budget_tokens,
            system_prompt_suffix,
            project_root: Some(project_root.clone()),
            enable_dynamic_rules: true,
            extended_tools: false,
            plan_mode,
            post_edit_validation: None,
            auto_compact,
            stream_timeout_secs: LLM_STREAM_TIMEOUT_SECS,
            prompt_caching: true,
        };

        // Populate tool sources for the permission middleware (run-scoped registry).
        {
            let mut sources = run_tool_sources.write().unwrap_or_else(|e| e.into_inner());
            for (def, src) in registry.list_tools_with_source() {
                sources.insert(def.name, convert_tool_source(&src));
            }
        }

        let mut agent = AgentLoop::new(
            Box::new(SharedProvider::new(provider)),
            registry,
            context,
            config,
        )
        .with_tool_visibility_profile(tool_visibility_profile)
        .with_history(history)
        .with_plugin_manager(Arc::clone(&self.plugin_manager));

        // Use the caller-provided session ID so frontend and backend share the same ID
        if let Some(sid) = session_id {
            agent = agent.with_session_id(sid);
            // Populate file backup sessions so write/edit tools save pre-mutation
            // snapshots to ~/.ava/file-history/{session_id}/
            let sid_str = sid.to_string();
            *run_backup_session.write().await = Some(sid_str.clone());
            *self.file_backup_session.write().await = Some(sid_str);
        }

        // Attach JSONL session logger if enabled in config
        if cfg.features.session_logging {
            let session_id = uuid::Uuid::new_v4().to_string();
            if let Some(logger) = crate::session_logger::SessionLogger::new(&session_id) {
                agent = agent.with_session_logger(logger);
            }
        }

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
            self.learn_project_patterns_from_goal(&raw_goal);

            // Fire SessionEnd plugin hook
            {
                let mut pm = self.plugin_manager.lock().await;
                if pm.has_hook_subscribers(ava_plugin::HookEvent::SessionEnd) {
                    pm.trigger_hook(
                        ava_plugin::HookEvent::SessionEnd,
                        serde_json::json!({ "turns": session.messages.len() }),
                    )
                    .await;
                }
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
        self.learn_project_patterns_from_goal(&raw_goal);

        // Fire SessionEnd plugin hook
        {
            let mut pm = self.plugin_manager.lock().await;
            if pm.has_hook_subscribers(ava_plugin::HookEvent::SessionEnd) {
                pm.trigger_hook(
                    ava_plugin::HookEvent::SessionEnd,
                    serde_json::json!({ "turns": session.messages.len() }),
                )
                .await;
            }
        }

        Ok(AgentRunResult {
            success: true,
            turns: session.messages.len(),
            session,
        })
    }

    /// Gracefully shut down all running plugins.
    pub async fn shutdown_plugins(&self) {
        let mut pm = self.plugin_manager.lock().await;
        pm.shutdown_all().await;
    }
}

/// Maximum nesting depth for sub-agent spawning. Prevents unbounded recursion
/// even if future refactors accidentally expose the task tool to sub-agents.
const MAX_AGENT_DEPTH: u32 = 3;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
enum SubAgentRuntimeProfile {
    Full,
    ReadOnly,
}

struct AgentTaskSpawner {
    provider: Arc<dyn LLMProvider>,
    platform: Arc<dyn Platform>,
    model_name: String,
    max_turns: usize,
    agents_config: AgentsConfig,
    /// Router for resolving per-agent model overrides from agents.toml.
    router: Arc<ModelRouter>,
    /// Optional event sender to emit `SubAgentComplete` events for TUI consumption.
    event_tx: Option<mpsc::UnboundedSender<AgentEvent>>,
    /// Session manager for persisting sub-agent sessions.
    session_manager: Option<Arc<SessionManager>>,
    /// Parent session ID for linking sub-agent sessions.
    parent_session_id: Option<String>,
    /// Current nesting depth (0 = top-level agent).
    depth: u32,
    /// Maximum number of sub-agents this parent run may spawn.
    max_spawns: usize,
    /// Total number of sub-agents already spawned in this parent run.
    spawn_count: Arc<AtomicUsize>,
}

#[async_trait]
impl TaskSpawner for AgentTaskSpawner {
    async fn spawn(&self, prompt: &str) -> Result<TaskResult> {
        self.spawn_named("task", prompt).await
    }

    async fn spawn_named(&self, agent_type: &str, prompt: &str) -> Result<TaskResult> {
        if self.depth >= MAX_AGENT_DEPTH {
            return Err(AvaError::ToolError(format!(
                "Maximum sub-agent depth reached ({MAX_AGENT_DEPTH}). Cannot spawn deeper."
            )));
        }

        let resolved = self.agents_config.get_agent(agent_type);

        if self.max_spawns == 0 {
            return Err(AvaError::ToolError(
                "Sub-agent delegation is disabled for this task. Keep the work in the main thread."
                    .to_string(),
            ));
        }

        if !resolved.enabled {
            return Err(AvaError::ToolError(format!(
                "Sub-agent '{agent_type}' is disabled in agents.toml"
            )));
        }

        if self
            .spawn_count
            .fetch_update(Ordering::SeqCst, Ordering::SeqCst, |current| {
                (current < self.max_spawns).then_some(current + 1)
            })
            .is_err()
        {
            return Err(AvaError::ToolError(format!(
                "Sub-agent budget exhausted for this task (max {}). Summarize what you have and continue in the main thread.",
                self.max_spawns
            )));
        }

        if let Some(ref external_provider) = resolved.provider {
            return self
                .spawn_external_named(agent_type, prompt, external_provider, &resolved)
                .await;
        }

        // Resolve per-agent model override. If the agent has a model configured
        // in agents.toml (e.g. `model = "openrouter/google/gemini-flash-1.5"`),
        // create a provider for that model. Otherwise, use the parent's provider.
        let (provider, effective_model) = if let Some(ref model_spec) = resolved.model {
            match self.resolve_agent_provider(model_spec).await {
                Ok((p, m)) => {
                    info!(
                        agent_type,
                        model = %m,
                        "sub-agent using per-agent model override"
                    );
                    (p, m)
                }
                Err(e) => {
                    warn!(
                        agent_type,
                        model_spec,
                        error = %e,
                        "failed to resolve per-agent model override, falling back to parent model"
                    );
                    (self.provider.clone(), self.model_name.clone())
                }
            }
        } else {
            (self.provider.clone(), self.model_name.clone())
        };

        info!(
            prompt_len = prompt.len(),
            agent_type,
            model = %effective_model,
            "spawning sub-agent for task tool"
        );
        let runtime_profile = subagent_runtime_profile(agent_type);
        let mut registry = ToolRegistry::new();
        register_core_tools(&mut registry, self.platform.clone());
        apply_subagent_runtime_profile(&mut registry, runtime_profile);
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
        let mut system_prompt = resolved
            .prompt
            .unwrap_or_else(|| build_sub_agent_system_prompt(agent_type));
        system_prompt.push_str(subagent_runtime_guidance(runtime_profile));

        let sub_agent_context_window = {
            let reg = ava_config::model_catalog::registry::registry();
            reg.find(&effective_model)
                .map(|m| m.limits.context_window)
                .unwrap_or(128_000)
        };

        let config = AgentConfig {
            max_turns: sub_max_turns,
            max_budget_usd: 0.0, // sub-agents don't get CLI budget
            token_limit: sub_agent_context_window,
            provider: String::new(), // sub-agents inherit parent's detection behavior
            model: effective_model.clone(),
            max_cost_usd: 5.0,
            loop_detection: true,
            custom_system_prompt: Some(system_prompt),
            thinking_level: ThinkingLevel::Off,
            thinking_budget_tokens: None,
            system_prompt_suffix: crate::instruction_resolver::build_sub_agent_instructions(
                &effective_model,
                &std::env::current_dir().unwrap_or_default(),
            ),
            project_root: Some(std::env::current_dir().unwrap_or_default()),
            enable_dynamic_rules: true,
            extended_tools: true, // sub-agents get full tool access
            plan_mode: false,
            auto_compact: true,
            post_edit_validation: None,
            stream_timeout_secs: LLM_STREAM_TIMEOUT_SECS,
            prompt_caching: true,
        };
        let mut agent = AgentLoop::new(
            Box::new(SharedProvider::new(provider)),
            registry,
            context,
            config,
        )
        .with_tool_visibility_profile(subagent_tool_visibility_profile(runtime_profile));
        let started_at = std::time::Instant::now();
        let mut session = agent.run(prompt).await?;

        // Set parent_id metadata so the session can be linked to the parent.
        session.metadata["is_sub_agent"] = serde_json::Value::Bool(true);
        session.metadata["agent_type"] = serde_json::Value::String(agent_type.to_string());
        if let Some(ref parent_id) = self.parent_session_id {
            session.metadata["parent_id"] = serde_json::Value::String(parent_id.clone());
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
        let description = format!("[{agent_type}] {prompt}");

        // Extract accumulated token usage from the sub-agent's session and compute cost.
        let sub_input_tokens = session.token_usage.input_tokens;
        let sub_output_tokens = session.token_usage.output_tokens;
        let (in_rate, out_rate) =
            ava_llm::providers::common::model_pricing_usd_per_million(&effective_model);
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
            agent_type,
            model = %effective_model,
            sub_input_tokens,
            sub_output_tokens,
            sub_cost_usd,
            "sub-agent task completed"
        );

        let delegation_record = DelegationRecord {
            agent_type: Some(agent_type.to_string()),
            provider: None,
            parent_session_id: self.parent_session_id.clone(),
            child_session_id: Some(session.id.to_string()),
            external_session_id: None,
            policy_reason: Some("native hidden subagent".to_string()),
            policy_version: Some("v1".to_string()),
            latency_ms: Some(started_at.elapsed().as_millis() as u64),
            resumed: false,
            input_tokens: Some(sub_input_tokens),
            output_tokens: Some(sub_output_tokens),
            cost_usd: Some(sub_cost_usd),
            outcome: Some("success".to_string()),
        };
        attach_delegation_record(&mut session, &delegation_record);

        // Persist the sub-agent session if a session manager is available.
        if let Some(ref sm) = self.session_manager {
            if let Err(e) = sm.save(&session) {
                warn!(error = %e, "Failed to persist sub-agent session");
            }
        }

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
                agent_type: Some(agent_type.to_string()),
                provider: None,
                resumed: false,
            });
        }

        Ok(TaskResult {
            text,
            session_id,
            messages,
        })
    }
}

impl AgentTaskSpawner {
    fn lookup_external_resume_session(
        &self,
        agent_type: &str,
        external_provider: &str,
        cwd: &str,
    ) -> Result<(Option<String>, bool)> {
        let Some(parent_session_id) = self.parent_session_id.as_deref() else {
            return Ok((None, false));
        };
        let Some(session_manager) = self.session_manager.as_ref() else {
            return Ok((None, false));
        };
        let parent_uuid = uuid::Uuid::parse_str(parent_session_id)
            .map_err(|error| AvaError::ValidationError(error.to_string()))?;
        let recent = session_manager.find_recent_child_by_external_link(
            parent_uuid,
            agent_type,
            external_provider,
            cwd,
        )?;
        let session_id = recent.and_then(|session| {
            session
                .metadata
                .get("externalLink")
                .and_then(|value| serde_json::from_value::<ExternalSessionLink>(value.clone()).ok())
                .and_then(|link| link.external_session_id)
        });
        let resume_attempted = session_id.is_some();
        Ok((session_id, resume_attempted))
    }

    async fn spawn_external_named(
        &self,
        agent_type: &str,
        prompt: &str,
        external_provider: &str,
        resolved: &ava_config::ResolvedAgent,
    ) -> Result<TaskResult> {
        let runtime_profile = subagent_runtime_profile(agent_type);
        let cwd = std::env::current_dir().unwrap_or_default();
        let sub_max_turns = if self.max_turns == 0 {
            resolved.max_turns.unwrap_or(10)
        } else {
            resolved.max_turns.unwrap_or(10).min(self.max_turns)
        };
        let mut system_prompt = resolved
            .prompt
            .clone()
            .unwrap_or_else(|| build_sub_agent_system_prompt(agent_type));
        system_prompt.push_str(subagent_runtime_guidance(runtime_profile));

        let cwd_str = cwd.to_string_lossy().to_string();
        let (resume_session_id, resume_attempted) = self
            .lookup_external_resume_session(agent_type, external_provider, &cwd_str)
            .unwrap_or((None, false));

        let transport = transport_for_builtin_agent(external_provider)?;
        let mut stream = transport
            .query(AgentQuery {
                prompt: prompt.to_string(),
                system_prompt: Some(system_prompt),
                working_directory: Some(cwd_str.clone()),
                max_turns: Some(sub_max_turns),
                permission_mode: Some(external_permission_mode(runtime_profile)),
                allowed_tools: resolved
                    .allowed_tools
                    .clone()
                    .or_else(|| default_external_allowed_tools(runtime_profile)),
                disallowed_tools: None,
                session_id: resume_session_id.clone(),
                resume: resume_attempted,
                model: resolved.model.clone(),
                max_budget_usd: resolved.max_budget_usd,
            })
            .await?;

        let started_at = std::time::Instant::now();
        let mut mapper = ExternalSessionMapper::new(ExternalRunDescriptor {
            provider: Some(external_provider.to_string()),
            agent_name: Some(external_provider.to_string()),
            model: resolved.model.clone(),
            cwd: Some(cwd_str.clone()),
            resume_attempted,
        });
        let mut session = Session::new();
        session.metadata["is_sub_agent"] = serde_json::Value::Bool(true);
        session.metadata["agent_type"] = serde_json::Value::String(agent_type.to_string());
        session.metadata["external_provider"] =
            serde_json::Value::String(external_provider.to_string());
        if let Some(ref parent_id) = self.parent_session_id {
            session.metadata["parent_id"] = serde_json::Value::String(parent_id.clone());
        }

        while let Some(msg) = timeout(Duration::from_secs(LLM_STREAM_TIMEOUT_SECS), stream.next())
            .await
            .map_err(|_| {
                AvaError::ToolError(format!(
                    "External sub-agent '{external_provider}' timed out waiting for output"
                ))
            })?
        {
            mapper.apply(msg)?;
        }

        let mut mapped_session = mapper.into_session();
        session.messages.append(&mut mapped_session.messages);
        session.token_usage = mapped_session.token_usage;
        if let Some(object) = mapped_session.metadata.as_object() {
            for (key, value) in object {
                session.metadata[key] = value.clone();
            }
        }

        let external_link = session
            .metadata
            .get("externalLink")
            .and_then(|value| serde_json::from_value::<ExternalSessionLink>(value.clone()).ok())
            .unwrap_or_default();
        let delegation_record = DelegationRecord {
            agent_type: Some(agent_type.to_string()),
            provider: Some(external_provider.to_string()),
            parent_session_id: self.parent_session_id.clone(),
            child_session_id: Some(session.id.to_string()),
            external_session_id: external_link.external_session_id.clone(),
            policy_reason: Some("configured external ACP subagent".to_string()),
            policy_version: Some("v1".to_string()),
            latency_ms: Some(started_at.elapsed().as_millis() as u64),
            resumed: external_link.resumed,
            input_tokens: Some(session.token_usage.input_tokens),
            output_tokens: Some(session.token_usage.output_tokens),
            cost_usd: session
                .metadata
                .get("externalCostUsd")
                .and_then(|value| value.as_f64()),
            outcome: Some("success".to_string()),
        };
        attach_delegation_record(&mut session, &delegation_record);

        if let Some(ref sm) = self.session_manager {
            if let Err(e) = sm.save(&session) {
                warn!(error = %e, "Failed to persist external sub-agent session");
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
        let description = format!("[{agent_type}] {prompt}");

        if let Some(ref tx) = self.event_tx {
            let _ = tx.send(AgentEvent::SubAgentComplete {
                call_id: String::new(),
                session_id: session_id.clone(),
                messages: messages.clone(),
                description,
                input_tokens: session.token_usage.input_tokens,
                output_tokens: session.token_usage.output_tokens,
                cost_usd: session
                    .metadata
                    .get("externalCostUsd")
                    .and_then(|value| value.as_f64())
                    .unwrap_or(0.0),
                agent_type: Some(agent_type.to_string()),
                provider: Some(external_provider.to_string()),
                resumed: external_link.resumed,
            });
        }

        Ok(TaskResult {
            text,
            session_id,
            messages,
        })
    }

    /// Resolve a model spec from agents.toml into a provider and model name.
    ///
    /// The model spec can be in the form `provider/model` (e.g.
    /// `openrouter/google/gemini-flash-1.5`) or just `model` (uses the parent's
    /// provider). For `openrouter` style specs where the model itself contains
    /// slashes, the first segment is the provider and the rest is the model.
    async fn resolve_agent_provider(
        &self,
        model_spec: &str,
    ) -> Result<(Arc<dyn LLMProvider>, String)> {
        let (provider_name, model_name) = parse_model_spec(model_spec);
        let provider = self
            .router
            .route_required(&provider_name, &model_name)
            .await?;
        Ok((provider, model_name))
    }
}

fn external_permission_mode(profile: SubAgentRuntimeProfile) -> PermissionMode {
    match profile {
        SubAgentRuntimeProfile::Full => PermissionMode::AcceptEdits,
        SubAgentRuntimeProfile::ReadOnly => PermissionMode::Plan,
    }
}

fn default_external_allowed_tools(profile: SubAgentRuntimeProfile) -> Option<Vec<String>> {
    match profile {
        SubAgentRuntimeProfile::Full => None,
        SubAgentRuntimeProfile::ReadOnly => Some(vec![
            "Read".to_string(),
            "Glob".to_string(),
            "Grep".to_string(),
        ]),
    }
}

fn adapt_delegation_policy_with_feedback(
    goal: &str,
    policy: crate::routing::SubagentDelegationPolicy,
    recent: &[DelegationRecord],
) -> crate::routing::SubagentDelegationPolicy {
    if recent.len() < 3 || policy.reason == EXPLICIT_DELEGATION_REASON {
        return policy;
    }

    let score = recent_delegation_feedback_score(recent);
    let is_broad_goal = {
        let lower = goal.to_lowercase();
        [
            "multiple files",
            "multi-file",
            "across files",
            "investigate",
            "review",
            "audit",
        ]
        .iter()
        .any(|needle| lower.contains(needle))
    };

    if score < 0.45 && policy.enable_task_tool {
        let next_max = policy.max_subagents.saturating_sub(1);
        return crate::routing::SubagentDelegationPolicy {
            enable_task_tool: next_max > 0,
            max_subagents: next_max,
            reason: format!(
                "{} (adaptive fallback: recent delegation quality {:.2} was weak)",
                policy.reason, score
            ),
        };
    }

    if score > 0.85 && policy.enable_task_tool && is_broad_goal {
        let next_max = (policy.max_subagents + 1).min(3);
        return crate::routing::SubagentDelegationPolicy {
            enable_task_tool: true,
            max_subagents: next_max,
            reason: format!(
                "{} (adaptive boost: recent delegation quality {:.2} was strong)",
                policy.reason, score
            ),
        };
    }

    policy
}

fn recent_delegation_feedback_score(recent: &[DelegationRecord]) -> f64 {
    let total = recent
        .iter()
        .map(|record| {
            let outcome = match record.outcome.as_deref() {
                Some("success") => 1.0,
                Some("error") => 0.2,
                Some("timeout") => 0.1,
                _ => 0.5,
            };
            let resumed_bonus = if record.resumed { 0.1 } else { 0.0 };
            let cost_penalty = record.cost_usd.unwrap_or(0.0).min(1.0) * 0.1;
            (outcome + resumed_bonus - cost_penalty).clamp(0.0, 1.0)
        })
        .sum::<f64>();
    total / recent.len() as f64
}

/// Parse a model spec string into (provider, model).
///
/// Supports formats:
/// - `provider/model` -> ("provider", "model")
/// - `provider/org/model` -> ("provider", "org/model") (for OpenRouter-style specs)
/// - `model` (no slash) -> uses model catalog to infer provider, or defaults to "openrouter"
pub fn parse_model_spec(spec: &str) -> (String, String) {
    if let Some(idx) = spec.find('/') {
        let provider = &spec[..idx];
        let model = &spec[idx + 1..];
        // Verify the first segment looks like a known provider name
        if ava_llm::providers::is_known_provider(provider) || provider.starts_with("cli:") {
            return (provider.to_string(), model.to_string());
        }
    }
    // No slash or first segment is not a known provider — try the model catalog
    if let Some(entry) = ava_config::model_catalog::registry::registry().find(spec) {
        return (entry.provider.clone(), entry.id.clone());
    }
    // Last resort: treat the whole string as an OpenRouter model path
    ("openrouter".to_string(), spec.to_string())
}

fn subagent_runtime_profile(agent_type: &str) -> SubAgentRuntimeProfile {
    match agent_type {
        "plan" | "explore" | "scout" | "review" => SubAgentRuntimeProfile::ReadOnly,
        _ => SubAgentRuntimeProfile::Full,
    }
}

#[cfg(test)]
#[allow(clippy::items_after_test_module)]
mod tests {
    use super::*;

    #[test]
    fn adaptive_feedback_reduces_subagents_after_weak_history() {
        let policy = crate::routing::SubagentDelegationPolicy {
            enable_task_tool: true,
            max_subagents: 2,
            reason: "task looks broad enough to justify one scout or reviewer".to_string(),
        };
        let recent = vec![
            DelegationRecord {
                outcome: Some("error".into()),
                ..Default::default()
            },
            DelegationRecord {
                outcome: Some("timeout".into()),
                ..Default::default()
            },
            DelegationRecord {
                outcome: Some("error".into()),
                ..Default::default()
            },
        ];

        let adjusted = adapt_delegation_policy_with_feedback(
            "investigate this across multiple files",
            policy,
            &recent,
        );

        assert!(adjusted.max_subagents < 2);
        assert!(adjusted.reason.contains("adaptive fallback"));
    }

    #[test]
    fn adaptive_feedback_boosts_subagents_after_strong_history() {
        let policy = crate::routing::SubagentDelegationPolicy {
            enable_task_tool: true,
            max_subagents: 1,
            reason: "task looks broad enough to justify one scout or reviewer".to_string(),
        };
        let recent = vec![
            DelegationRecord {
                outcome: Some("success".into()),
                resumed: true,
                ..Default::default()
            },
            DelegationRecord {
                outcome: Some("success".into()),
                ..Default::default()
            },
            DelegationRecord {
                outcome: Some("success".into()),
                ..Default::default()
            },
        ];

        let adjusted = adapt_delegation_policy_with_feedback(
            "review architecture across multiple files",
            policy,
            &recent,
        );

        assert!(adjusted.max_subagents > 1);
        assert!(adjusted.reason.contains("adaptive boost"));
    }
}

fn apply_subagent_runtime_profile(registry: &mut ToolRegistry, profile: SubAgentRuntimeProfile) {
    if profile == SubAgentRuntimeProfile::ReadOnly {
        for tool in ["write", "edit", "bash", "web_fetch", "web_search"] {
            registry.unregister(tool);
        }
    }
}

fn subagent_tool_visibility_profile(
    profile: SubAgentRuntimeProfile,
) -> crate::routing::ToolVisibilityProfile {
    match profile {
        SubAgentRuntimeProfile::Full => crate::routing::ToolVisibilityProfile::Full,
        SubAgentRuntimeProfile::ReadOnly => crate::routing::ToolVisibilityProfile::ReadOnly,
    }
}

fn subagent_runtime_guidance(profile: SubAgentRuntimeProfile) -> &'static str {
    match profile {
        SubAgentRuntimeProfile::Full => {
            "\n\n## Runtime limits\n- Stay focused on the delegated task. Keep changes narrow and summarize the result clearly.\n"
        }
        SubAgentRuntimeProfile::ReadOnly => {
            "\n\n## Runtime limits\n- You are running in read-only specialist mode. Do not edit files, run shell commands, or browse the web. Investigate with read, glob, grep, and git_read, then report back clearly.\n"
        }
    }
}

fn build_sub_agent_system_prompt(agent_type: &str) -> String {
    format!(
        "You are the `{agent_type}` sub-agent of AVA, an AI coding assistant. You have been given a specific task \
         to complete autonomously. Work through it step by step using the available tools.\n\n\
         ## Rules\n\
         - Read files before modifying them.\n\
         - Prefer focused, local changes over broad rewrites.\n\
         - Be thorough but efficient -- you have a limited number of turns.\n\
         - When your task is complete, provide a clear summary of what you did as your final response.\n\
         - Do NOT call attempt_completion -- simply respond with your final answer when done.\n"
    )
}
