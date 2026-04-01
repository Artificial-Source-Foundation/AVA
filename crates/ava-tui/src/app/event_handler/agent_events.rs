use super::*;
use crate::state::agent::SubAgentInfo;
use crate::state::rewind::{snapshot_file, ChangeType, FileChange};
use tracing::{debug, info};

fn normalize_subagent_description(value: &str) -> &str {
    let trimmed = value.trim();
    if let Some(rest) = trimmed
        .strip_prefix('[')
        .and_then(|rest| rest.split_once(']').map(|(_, tail)| tail.trim()))
    {
        if !rest.is_empty() {
            return rest;
        }
    }
    trimmed
}

fn subagent_descriptions_match(a: &str, b: &str) -> bool {
    normalize_subagent_description(a) == normalize_subagent_description(b)
}

impl App {
    pub(crate) fn handle_agent_event(
        &mut self,
        agent_event: ava_agent::AgentEvent,
        app_tx: mpsc::UnboundedSender<AppEvent>,
        _agent_tx: mpsc::UnboundedSender<ava_agent::AgentEvent>,
    ) {
        match agent_event {
            ava_agent::AgentEvent::Thinking(content) => {
                // Accumulate thinking tokens into a single message
                if let Some(last) = self.state.messages.messages.last_mut() {
                    if matches!(last.kind, MessageKind::Thinking) {
                        last.content.push_str(&content);
                        last.is_streaming = true;
                    } else {
                        let mut msg = UiMessage::new(MessageKind::Thinking, content);
                        msg.is_streaming = true;
                        self.state.messages.push(msg);
                    }
                } else {
                    let mut msg = UiMessage::new(MessageKind::Thinking, content);
                    msg.is_streaming = true;
                    self.state.messages.push(msg);
                }
            }
            ava_agent::AgentEvent::Token(chunk) => {
                self.state.agent.activity = AgentActivity::Streaming;
                // Mark any preceding thinking message as done streaming
                if let Some(last) = self.state.messages.messages.last_mut() {
                    if matches!(last.kind, MessageKind::Thinking) {
                        last.is_streaming = false;
                    }
                }
                self.token_buffer.push(&chunk);
                // Ensure the streaming indicator is set on the current message
                if let Some(last) = self.state.messages.messages.last_mut() {
                    if matches!(last.kind, MessageKind::Assistant) {
                        last.is_streaming = true;
                    }
                }
            }
            ava_agent::AgentEvent::TokenUsage {
                input_tokens,
                output_tokens,
                cost_usd,
            } => {
                // Replace (not accumulate) for context display — each turn's
                // input_tokens already includes the full conversation history,
                // so the latest value represents current context window usage.
                self.state.agent.tokens_used.input = input_tokens;
                self.state.agent.tokens_used.output = output_tokens;
                // Accumulate for session-level reporting
                self.state.agent.tokens_used.cumulative_input += input_tokens;
                self.state.agent.tokens_used.cumulative_output += output_tokens;
                self.state.agent.cost += cost_usd;
            }
            ava_agent::AgentEvent::BudgetWarning {
                threshold_percent,
                current_cost_usd,
                max_budget_usd,
            } => {
                self.set_status(
                    format!(
                        "Budget warning: {}% used (${:.2} / ${:.2})",
                        threshold_percent, current_cost_usd, max_budget_usd
                    ),
                    StatusLevel::Warn,
                );
            }
            ava_agent::AgentEvent::ToolCall(call) => {
                debug!(tool = %call.name, "TUI received ToolCall");

                // Fire PreToolUse hooks asynchronously
                {
                    let mut ctx = self.build_hook_context(&HookEvent::PreToolUse);
                    ctx.tool_name = Some(call.name.clone());
                    ctx.tool_input = Some(call.arguments.clone());
                    // Extract file_path from tool arguments for path_pattern matching
                    ctx.file_path = call
                        .arguments
                        .get("file_path")
                        .or_else(|| call.arguments.get("path"))
                        .and_then(|v| v.as_str())
                        .map(String::from);
                    self.fire_hooks_async(HookEvent::PreToolUse, ctx, app_tx.clone());
                }

                // Force flush any buffered tokens before tool call
                self.force_flush_token_buffer();
                // Mark last assistant/thinking message as done streaming.
                // Clear response_time on intermediate assistant messages — only the
                // LAST assistant message will get the total loop time on Complete.
                if let Some(last) = self.state.messages.messages.last_mut() {
                    if matches!(last.kind, MessageKind::Assistant | MessageKind::Thinking) {
                        last.is_streaming = false;
                        if matches!(last.kind, MessageKind::Assistant) {
                            if last.model_name.is_none() {
                                last.model_name = Some(self.state.agent.model_name.clone());
                            }
                            // This is an intermediate message (more tool calls coming),
                            // so suppress its footer by clearing timing data.
                            last.response_time = None;
                            last.started_at = None;
                        }
                    }
                }
                self.state.agent.activity = AgentActivity::ExecutingTool(call.name.clone());
                self.state.agent.tool_start = Some(std::time::Instant::now());

                // Track sub-agent spawns from the task tool
                if call.name == "task" {
                    let description = call
                        .arguments
                        .get("prompt")
                        .and_then(|p| p.as_str())
                        .map(String::from)
                        .unwrap_or_else(|| "sub-agent task".to_string());

                    // Fire SubagentStart hook
                    {
                        let mut ctx = self.build_hook_context(&HookEvent::SubagentStart);
                        ctx.subagent_description = Some(description.clone());
                        self.fire_hooks_async(HookEvent::SubagentStart, ctx, app_tx.clone());
                    }
                    self.state.agent.sub_agents.push(SubAgentInfo {
                        description: description.clone(),
                        is_running: true,
                        tool_count: 0,
                        current_tool: None,
                        started_at: std::time::Instant::now(),
                        elapsed: None,
                        session_id: None,
                        session_messages: Vec::new(),
                        provider: None,
                        resumed: false,
                        cost_usd: None,
                        input_tokens: None,
                        output_tokens: None,
                    });

                    // Create a SubAgent message instead of a regular ToolCall
                    let mut msg = UiMessage::new(MessageKind::SubAgent, String::new());
                    msg.is_streaming = true;
                    msg.sub_agent = Some(crate::state::messages::SubAgentData {
                        description,
                        tool_count: 0,
                        duration: None,
                        is_running: true,
                        failed: false,
                        call_id: call.id.clone(),
                        session_id: None,
                        session_messages: Vec::new(),
                        provider: None,
                        resumed: false,
                        cost_usd: None,
                        input_tokens: None,
                        output_tokens: None,
                    });
                    self.state.messages.push(msg);
                } else {
                    // Snapshot files before write/edit/apply_patch for rewind
                    if matches!(
                        call.name.as_str(),
                        "write" | "edit" | "apply_patch" | "multiedit"
                    ) {
                        let file_path = call
                            .arguments
                            .get("file_path")
                            .or_else(|| call.arguments.get("path"))
                            .and_then(|v| v.as_str())
                            .map(String::from);
                        if let Some(path) = file_path {
                            let original = snapshot_file(&path);
                            let change_type = if original.is_some() {
                                ChangeType::Modified
                            } else {
                                ChangeType::Created
                            };
                            self.state.rewind.record_file_change(FileChange {
                                path,
                                original_content: original,
                                change_type,
                            });
                        }
                    }

                    self.state.messages.push(UiMessage::new(
                        MessageKind::ToolCall,
                        format!("{} {}", call.name, call.arguments),
                    ));
                }
            }
            ava_agent::AgentEvent::ToolResult(result) => {
                // Fire PostToolUse or PostToolUseFailure hooks
                {
                    let hook_event = if result.is_error {
                        HookEvent::PostToolUseFailure
                    } else {
                        HookEvent::PostToolUse
                    };
                    let mut ctx = self.build_hook_context(&hook_event);
                    // Extract tool name from the last ExecutingTool activity
                    if let AgentActivity::ExecutingTool(ref name) = self.state.agent.activity {
                        ctx.tool_name = Some(name.clone());
                    }
                    ctx.tool_output = Some(result.content.clone());
                    self.fire_hooks_async(hook_event, ctx, app_tx.clone());
                }

                self.state.agent.activity = AgentActivity::Thinking;
                self.state.agent.tool_start = None;

                // Check if this result belongs to a running sub-agent (task tool)
                let is_sub_agent_result = self.state.messages.messages.iter().any(|m| {
                    matches!(m.kind, MessageKind::SubAgent)
                        && m.sub_agent
                            .as_ref()
                            .is_some_and(|d| d.is_running && d.call_id == result.call_id)
                });

                if is_sub_agent_result {
                    // Mark the sub-agent state as completed
                    let sa_tool_count = if let Some(sa) = self
                        .state
                        .agent
                        .sub_agents
                        .iter_mut()
                        .rev()
                        .find(|s| s.is_running)
                    {
                        sa.is_running = false;
                        sa.elapsed = Some(sa.started_at.elapsed());
                        sa.current_tool = None;
                        (sa.tool_count, sa.elapsed)
                    } else {
                        (0, None)
                    };

                    // Update the SubAgent UI message with result content and stats
                    let sa_failed = result.is_error;
                    if let Some(msg) = self.state.messages.messages.iter_mut().rev().find(|m| {
                        matches!(m.kind, MessageKind::SubAgent)
                            && m.sub_agent
                                .as_ref()
                                .is_some_and(|d| d.call_id == result.call_id)
                    }) {
                        msg.content = result.content;
                        msg.is_streaming = false;
                        if let Some(data) = msg.sub_agent.as_mut() {
                            data.is_running = false;
                            data.failed = sa_failed;
                            data.tool_count = sa_tool_count.0;
                            data.duration = sa_tool_count.1;
                        }
                    }
                } else {
                    // Mark most recent running sub-agent as completed (non-task tool results)
                    if let Some(sa) = self
                        .state
                        .agent
                        .sub_agents
                        .iter_mut()
                        .rev()
                        .find(|s| s.is_running)
                    {
                        sa.is_running = false;
                        sa.elapsed = Some(sa.started_at.elapsed());
                        sa.current_tool = None;
                    }

                    self.state
                        .messages
                        .push(UiMessage::new(MessageKind::ToolResult, result.content));
                }
            }
            ava_agent::AgentEvent::Progress(progress) => {
                // Parse turn info from progress messages (internal state only, not displayed)
                if let Some(turn_str) = progress.strip_prefix("turn ") {
                    if let Ok(turn) = turn_str.trim().parse::<usize>() {
                        self.state.agent.current_turn = turn;
                    }
                }
            }
            ava_agent::AgentEvent::Complete(_) => {
                info!("TUI received AgentEvent::Complete — marking agent idle");

                // Fire Stop hooks
                {
                    let ctx = self.build_hook_context(&HookEvent::Stop);
                    self.fire_hooks_async(HookEvent::Stop, ctx, app_tx.clone());
                }

                self.finalize_assistant_stream();
                self.is_streaming.store(false, Ordering::Relaxed);
                self.state.agent.is_running = false;
                self.state.agent.activity = AgentActivity::Idle;
                // Clear the queue display (all messages have been delivered)
                self.state.input.queue_display.clear();

                // Continuous voice: restart recording after agent completes
                if self.state.voice.continuous && self.state.voice.phase == VoicePhase::Idle {
                    self.toggle_voice(app_tx.clone());
                }
            }
            ava_agent::AgentEvent::SubAgentComplete {
                call_id,
                session_id,
                messages,
                description,
                input_tokens,
                output_tokens,
                cost_usd,
                provider,
                resumed,
                ..
            } => {
                debug!(
                    session_id = %session_id,
                    message_count = messages.len(),
                    input_tokens,
                    output_tokens,
                    cost_usd,
                    "TUI received SubAgentComplete"
                );

                // Fire SubagentStop hook
                {
                    let mut ctx = self.build_hook_context(&HookEvent::SubagentStop);
                    ctx.subagent_description = Some(description.clone());
                    self.fire_hooks_async(HookEvent::SubagentStop, ctx, app_tx.clone());
                }

                // Accumulate sub-agent token usage into cumulative totals only —
                // don't overwrite the parent's latest-turn context display values.
                self.state.agent.tokens_used.cumulative_input += input_tokens;
                self.state.agent.tokens_used.cumulative_output += output_tokens;
                self.state.agent.cost += cost_usd;

                // Convert agent messages to UI messages for storage
                let ui_messages: Vec<UiMessage> = messages
                    .iter()
                    .filter_map(|m| {
                        let kind = match m.role {
                            ava_types::Role::User => MessageKind::User,
                            ava_types::Role::Assistant => MessageKind::Assistant,
                            ava_types::Role::Tool => MessageKind::ToolResult,
                            ava_types::Role::System => return None,
                        };
                        Some(UiMessage::new(kind, m.content.clone()))
                    })
                    .collect();

                // Update the SubAgentInfo in agent state
                if let Some(sa) = self
                    .state
                    .agent
                    .sub_agents
                    .iter_mut()
                    .rev()
                    .find(|s| subagent_descriptions_match(&s.description, &description))
                {
                    sa.session_id = Some(session_id.clone());
                    sa.session_messages = ui_messages.clone();
                    sa.provider = provider.clone();
                    sa.resumed = resumed;
                    sa.cost_usd = Some(cost_usd);
                    sa.input_tokens = Some(input_tokens);
                    sa.output_tokens = Some(output_tokens);
                }

                // Update the SubAgentData in the matching UI message
                if let Some(msg) = self.state.messages.messages.iter_mut().rev().find(|m| {
                    matches!(m.kind, MessageKind::SubAgent)
                        && m.sub_agent.as_ref().is_some_and(|d| {
                            (!call_id.is_empty() && d.call_id == call_id)
                                || subagent_descriptions_match(&d.description, &description)
                        })
                }) {
                    if let Some(data) = msg.sub_agent.as_mut() {
                        data.session_id = Some(session_id);
                        data.session_messages = ui_messages;
                        data.provider = provider;
                        data.resumed = resumed;
                        data.cost_usd = Some(cost_usd);
                        data.input_tokens = Some(input_tokens);
                        data.output_tokens = Some(output_tokens);
                    }
                }
            }
            ava_agent::AgentEvent::ToolStats(_) => {}
            ava_agent::AgentEvent::DiffPreview {
                file,
                diff_text,
                additions,
                deletions,
            } => {
                debug!(
                    file = %file.display(),
                    additions,
                    deletions,
                    "TUI received DiffPreview"
                );
                // Store the diff info for potential UI display (e.g., file change panel)
                let summary = format!(
                    "{}  +{} -{}\n{}",
                    file.display(),
                    additions,
                    deletions,
                    diff_text
                );
                self.state
                    .messages
                    .push(UiMessage::new(MessageKind::ToolResult, summary));
            }
            ava_agent::AgentEvent::MCPToolsChanged {
                server_name,
                tool_count,
            } => {
                let msg =
                    format!("MCP server '{server_name}' tool list refreshed ({tool_count} tools)");
                info!("{}", msg);
                self.state
                    .messages
                    .push(UiMessage::new(MessageKind::ToolResult, msg));
            }
            ava_agent::AgentEvent::Checkpoint(session) => {
                // Incremental save so progress survives unexpected exits.
                // Uses add_messages (INSERT OR REPLACE) instead of full save
                // (DELETE-all + INSERT-all) to avoid data loss on crash.
                self.state.session.checkpoint_session(&session);
            }
            ava_agent::AgentEvent::ContextCompacted { summary, .. } => {
                self.set_status("Context compacted".to_string(), StatusLevel::Info);
                self.state
                    .messages
                    .push(UiMessage::new(MessageKind::System, summary));
            }
            ava_agent::AgentEvent::SnapshotTaken {
                commit_hash,
                message,
            } => {
                debug!(hash = %commit_hash, msg = %message, "snapshot recorded for rewind");
                self.state.rewind.record_snapshot_hash(commit_hash);
                // Lazily create the snapshot manager handle for TUI-side restore.
                // The agent loop has its own handle; here we create a matching one
                // pointing at the same project so /rewind can use it.
                if self.state.snapshot_manager.is_none() {
                    let project_root = std::env::current_dir().unwrap_or_default();
                    if let Ok(mgr) =
                        ava_tools::core::file_snapshot::SnapshotManager::new_initialized(
                            &project_root,
                        )
                    {
                        let shared = ava_tools::core::file_snapshot::new_shared_snapshot_manager();
                        // Use try_write to avoid blocking — the lock is uncontested here.
                        if let Ok(mut guard) = shared.try_write() {
                            *guard = Some(mgr);
                        }
                        self.state.snapshot_manager = Some(shared);
                    }
                }
            }
            ava_agent::AgentEvent::PlanStepComplete { step_id } => {
                debug!(step_id = %step_id, "Plan step completed");
                // Progress tracking is handled by the web frontend via
                // WebSocket events. The TUI logs the event for diagnostics.
            }
            ava_agent::AgentEvent::StreamingEditProgress {
                tool_name,
                file_path,
                bytes_received,
                ..
            } => {
                if let Some(path) = file_path {
                    self.set_status(
                        format!("{} {}... ({} bytes)", tool_name, path, bytes_received),
                        StatusLevel::Info,
                    );
                } else {
                    self.set_status(format!("{}...", tool_name), StatusLevel::Info);
                }
            }
            ava_agent::AgentEvent::RetryHeartbeat { attempt, wait_secs } => {
                self.set_status(
                    format!("retrying (attempt {attempt}, waiting {wait_secs}s)..."),
                    StatusLevel::Warn,
                );
            }
            ava_agent::AgentEvent::FallbackModelSwitch {
                primary_model,
                fallback_model,
            } => {
                self.set_status(
                    format!("switched to fallback model: {fallback_model} (primary {primary_model} overloaded)"),
                    StatusLevel::Warn,
                );
            }
            ava_agent::AgentEvent::Error(err) => {
                info!(error = %err, "TUI received AgentEvent::Error");
                self.finalize_assistant_stream();
                self.state.agent.activity = AgentActivity::Idle;
                self.state
                    .messages
                    .push(UiMessage::new(MessageKind::Error, err));
            }
            ava_agent::AgentEvent::StreamSilenceWarning { elapsed_secs } => {
                self.set_status(
                    format!("Stream silent for {elapsed_secs}s — waiting for provider..."),
                    StatusLevel::Warn,
                );
            }
        }
    }
}
