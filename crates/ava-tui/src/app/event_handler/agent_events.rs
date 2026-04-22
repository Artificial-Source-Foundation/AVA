use super::*;
use crate::state::agent::SubAgentInfo;
use crate::state::rewind::{snapshot_file, ChangeType, FileChange};
use ava_types::{MessageTier, QueuedMessage};
use tracing::{debug, info};

fn ensure_active_child_assistant_message(messages: &mut Vec<UiMessage>) -> &mut UiMessage {
    let needs_new_message = !matches!(
        messages.last(),
        Some(UiMessage {
            kind: MessageKind::Assistant,
            ..
        })
    );
    if needs_new_message {
        let mut msg = UiMessage::new(MessageKind::Assistant, String::new());
        msg.is_streaming = true;
        messages.push(msg);
    }
    messages
        .last_mut()
        .expect("assistant message should exist after ensuring child assistant")
}

fn clear_child_placeholder(messages: &mut Vec<UiMessage>) {
    if messages.len() == 2
        && matches!(messages[0].kind, MessageKind::System)
        && matches!(messages[1].kind, MessageKind::Thinking)
        && messages[0].content.starts_with("Delegated task: ")
    {
        messages.clear();
    }
}

fn apply_live_subagent_event(
    messages: &mut Vec<UiMessage>,
    event: ava_agent::agent_loop::SubAgentLiveEvent,
) -> Option<String> {
    use ava_agent::agent_loop::SubAgentLiveEvent;

    match event {
        SubAgentLiveEvent::Started { .. } => None,
        SubAgentLiveEvent::Thinking(content) => {
            clear_child_placeholder(messages);
            if let Some(last) = messages.last_mut() {
                if matches!(last.kind, MessageKind::Thinking) {
                    last.content.push_str(&content);
                    last.is_streaming = true;
                    return None;
                }
                last.is_streaming = false;
            }
            let mut msg = UiMessage::new(MessageKind::Thinking, content);
            msg.is_streaming = true;
            messages.push(msg);
            None
        }
        SubAgentLiveEvent::Token(chunk) => {
            clear_child_placeholder(messages);
            if let Some(last) = messages.last_mut() {
                if matches!(last.kind, MessageKind::Thinking) {
                    last.is_streaming = false;
                }
            }
            let assistant = ensure_active_child_assistant_message(messages);
            assistant.content.push_str(&chunk);
            assistant.is_streaming = true;
            Some(assistant.content.clone())
        }
        SubAgentLiveEvent::ToolCall(call) => {
            clear_child_placeholder(messages);
            if let Some(last) = messages.last_mut() {
                last.is_streaming = false;
            }
            let mut msg = UiMessage::new(
                MessageKind::ToolCall,
                format!("{} {}", call.name, call.arguments),
            );
            msg.tool_name = Some(call.name);
            messages.push(msg);
            None
        }
        SubAgentLiveEvent::ToolResult(result) => {
            clear_child_placeholder(messages);
            if let Some(last) = messages.last_mut() {
                last.is_streaming = false;
            }
            messages.push(UiMessage::new(MessageKind::ToolResult, result.content));
            None
        }
        SubAgentLiveEvent::Checkpoint(_) => None,
        SubAgentLiveEvent::Error(error) => {
            clear_child_placeholder(messages);
            if let Some(last) = messages.last_mut() {
                last.is_streaming = false;
            }
            messages.push(UiMessage::new(MessageKind::Error, error));
            None
        }
    }
}

fn queue_background_completion_follow_up(
    tx: &mpsc::UnboundedSender<QueuedMessage>,
    description: &str,
    summary: &str,
) {
    let text = format!(
        "Background agent completed. Task: {description}\n\nSummary:\n{summary}\n\nUse this result if it is relevant and continue."
    );
    let _ = tx.send(QueuedMessage {
        text,
        tier: MessageTier::FollowUp,
    });
}

impl App {
    pub(crate) fn handle_agent_event(
        &mut self,
        agent_event: ava_agent::AgentEvent,
        app_tx: mpsc::UnboundedSender<AppEvent>,
        agent_tx: mpsc::UnboundedSender<ava_agent::AgentEvent>,
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

                // Track sub-agent spawns from the subagent tool.
                // Keep `task` as a compatibility alias for older sessions/tests.
                if matches!(call.name.as_str(), "subagent" | "task" | "background_agent") {
                    let agent_type = call
                        .arguments
                        .get("agent")
                        .and_then(|value| value.as_str())
                        .map(String::from);
                    let description = call
                        .arguments
                        .get("prompt")
                        .and_then(|p| p.as_str())
                        .map(String::from)
                        .unwrap_or_else(|| "sub-agent task".to_string());
                    let background = call
                        .arguments
                        .get("background")
                        .and_then(|value| value.as_bool())
                        .unwrap_or_else(|| call.name == "background_agent");
                    let initial_session_messages =
                        initial_subagent_session_messages(&description, background);

                    // Fire SubagentStart hook
                    {
                        let mut ctx = self.build_hook_context(&HookEvent::SubagentStart);
                        ctx.subagent_description = Some(description.clone());
                        self.fire_hooks_async(HookEvent::SubagentStart, ctx, app_tx.clone());
                    }
                    self.state.agent.sub_agents.push(SubAgentInfo {
                        call_id: call.id.clone(),
                        agent_type: agent_type.clone(),
                        description: description.clone(),
                        background,
                        is_running: true,
                        tool_count: 0,
                        current_tool: None,
                        started_at: std::time::Instant::now(),
                        elapsed: None,
                        session_id: None,
                        session_messages: initial_session_messages.clone(),
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
                        agent_type,
                        description,
                        background,
                        tool_count: 0,
                        current_tool: None,
                        duration: None,
                        is_running: true,
                        failed: false,
                        call_id: call.id.clone(),
                        session_id: None,
                        session_messages: initial_session_messages,
                        provider: None,
                        resumed: false,
                        cost_usd: None,
                        input_tokens: None,
                        output_tokens: None,
                    });
                    self.state.messages.push(msg);
                } else {
                    if let Some(sa) = self
                        .state
                        .agent
                        .sub_agents
                        .iter_mut()
                        .rev()
                        .find(|s| s.is_running && !s.background)
                    {
                        sa.tool_count += 1;
                        sa.current_tool = Some(call.name.clone());
                    }
                    if let Some(msg) = self.state.messages.messages.iter_mut().rev().find(|m| {
                        matches!(m.kind, MessageKind::SubAgent)
                            && m.sub_agent.as_ref().is_some_and(|d| d.is_running)
                    }) {
                        if let Some(data) = msg.sub_agent.as_mut() {
                            data.tool_count += 1;
                            data.current_tool = Some(call.name.clone());
                        }
                    }

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

                // Check if this result belongs to a running sub-agent tool call.
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
                        .find(|s| s.call_id == result.call_id)
                    {
                        let completed = !sa.background;
                        if completed {
                            sa.is_running = false;
                            sa.elapsed = Some(sa.started_at.elapsed());
                        }
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
                        let keep_running = msg
                            .sub_agent
                            .as_ref()
                            .is_some_and(|data| data.background && !result.is_error);
                        msg.is_streaming = keep_running;
                        if let Some(data) = msg.sub_agent.as_mut() {
                            data.is_running = keep_running;
                            data.failed = sa_failed && !keep_running;
                            data.tool_count = sa_tool_count.0;
                            data.current_tool = None;
                            data.duration = sa_tool_count.1;
                        }
                    }
                } else {
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
                agent_type,
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

                if let Ok(session_uuid) = uuid::Uuid::parse_str(&session_id) {
                    let mut session = ava_types::Session::new().with_id(session_uuid);
                    session.messages = messages.clone();
                    session.metadata["is_sub_agent"] = serde_json::Value::Bool(true);
                    session.metadata["agent_type"] = serde_json::Value::String(
                        agent_type.clone().unwrap_or_else(|| "subagent".to_string()),
                    );
                    self.state.session.cache_session(&session);
                }

                // Convert agent messages to UI messages for storage
                let ui_messages = crate::app::session_messages_to_subagent_ui_messages(&messages);
                let exact_ui_messages =
                    crate::app::session_messages_to_exact_child_ui_messages(&messages);

                // Update the SubAgentInfo in agent state
                if let Some(sa) = self.state.agent.sub_agents.iter_mut().rev().find(|s| {
                    subagent_matches_completion(&s.call_id, &s.description, &call_id, &description)
                }) {
                    let was_background = sa.background;
                    let original_description = sa.description.clone();
                    sa.is_running = false;
                    sa.elapsed = Some(sa.started_at.elapsed());
                    sa.current_tool = None;
                    sa.agent_type = agent_type.clone();
                    sa.session_id = Some(session_id.clone());
                    sa.session_messages = exact_ui_messages.clone();
                    sa.provider = provider.clone();
                    sa.resumed = resumed;
                    sa.cost_usd = Some(cost_usd);
                    sa.input_tokens = Some(input_tokens);
                    sa.output_tokens = Some(output_tokens);

                    if was_background {
                        if let Some(summary) = ui_messages
                            .iter()
                            .rev()
                            .find(|message| matches!(message.kind, MessageKind::Assistant))
                            .map(|message| message.content.clone())
                        {
                            self.state.messages.push(UiMessage::new(
                                MessageKind::System,
                                format!("Background agent finished: {original_description}"),
                            ));
                            if let Some(ref tx) = self.state.agent.message_tx {
                                queue_background_completion_follow_up(
                                    tx,
                                    &original_description,
                                    &summary,
                                );
                            } else if !self.state.agent.is_running {
                                let restart_prompt = format!(
                                    "Background agent completed. Task: {original_description}\n\nSummary:\n{summary}\n\nContinue from this new information."
                                );
                                self.submit_goal(restart_prompt, app_tx.clone(), agent_tx.clone());
                            }
                        }
                    }
                }

                // Update the SubAgentData in the matching UI message
                if let Some(msg) = self.state.messages.messages.iter_mut().rev().find(|m| {
                    matches!(m.kind, MessageKind::SubAgent)
                        && m.sub_agent.as_ref().is_some_and(|d| {
                            subagent_matches_completion(
                                &d.call_id,
                                &d.description,
                                &call_id,
                                &description,
                            )
                        })
                }) {
                    let final_summary = ui_messages
                        .iter()
                        .rev()
                        .find(|message| matches!(message.kind, MessageKind::Assistant))
                        .map(|message| message.content.clone());
                    if let Some(summary) = final_summary {
                        msg.content = summary;
                    }
                    msg.is_streaming = false;
                    if let Some(data) = msg.sub_agent.as_mut() {
                        data.is_running = false;
                        data.agent_type = agent_type;
                        data.session_id = Some(session_id);
                        data.session_messages = exact_ui_messages;
                        data.current_tool = None;
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
            ava_agent::AgentEvent::SubAgentUpdate {
                call_id,
                description,
                event,
            } => {
                let mut latest_summary = None;
                let checkpoint_messages = match &event {
                    ava_agent::agent_loop::SubAgentLiveEvent::Checkpoint(session) => {
                        self.state.session.cache_session(session);
                        Some(crate::app::session_messages_to_exact_child_ui_messages(
                            &session.messages,
                        ))
                    }
                    _ => None,
                };

                if let Some(subagent) =
                    self.state
                        .agent
                        .sub_agents
                        .iter_mut()
                        .rev()
                        .find(|subagent| {
                            subagent_matches_completion(
                                &subagent.call_id,
                                &subagent.description,
                                &call_id,
                                &description,
                            )
                        })
                {
                    if let Some(messages) = checkpoint_messages.clone() {
                        subagent.session_messages = messages;
                    }
                    latest_summary =
                        apply_live_subagent_event(&mut subagent.session_messages, event.clone());
                    match &event {
                        ava_agent::agent_loop::SubAgentLiveEvent::Started { session_id } => {
                            subagent.session_id = Some(session_id.clone());
                        }
                        ava_agent::agent_loop::SubAgentLiveEvent::ToolCall(call) => {
                            subagent.tool_count += 1;
                            subagent.current_tool = Some(call.name.clone());
                        }
                        ava_agent::agent_loop::SubAgentLiveEvent::ToolResult(_) => {
                            subagent.current_tool = None;
                        }
                        ava_agent::agent_loop::SubAgentLiveEvent::Error(_) => {
                            subagent.current_tool = None;
                            subagent.is_running = false;
                            subagent.elapsed = Some(subagent.started_at.elapsed());
                        }
                        ava_agent::agent_loop::SubAgentLiveEvent::Token(_)
                        | ava_agent::agent_loop::SubAgentLiveEvent::Thinking(_)
                        | ava_agent::agent_loop::SubAgentLiveEvent::Checkpoint(_) => {}
                    }
                }

                if let Some(msg) = self.state.messages.messages.iter_mut().rev().find(|msg| {
                    matches!(msg.kind, MessageKind::SubAgent)
                        && msg.sub_agent.as_ref().is_some_and(|data| {
                            subagent_matches_completion(
                                &data.call_id,
                                &data.description,
                                &call_id,
                                &description,
                            )
                        })
                }) {
                    if let Some(data) = msg.sub_agent.as_mut() {
                        if let Some(messages) = checkpoint_messages {
                            data.session_messages = messages;
                        }
                        let _ =
                            apply_live_subagent_event(&mut data.session_messages, event.clone());
                        match &event {
                            ava_agent::agent_loop::SubAgentLiveEvent::Started { session_id } => {
                                data.session_id = Some(session_id.clone());
                            }
                            ava_agent::agent_loop::SubAgentLiveEvent::ToolCall(call) => {
                                data.tool_count += 1;
                                data.current_tool = Some(call.name.clone());
                            }
                            ava_agent::agent_loop::SubAgentLiveEvent::ToolResult(_) => {
                                data.current_tool = None;
                            }
                            ava_agent::agent_loop::SubAgentLiveEvent::Error(error) => {
                                data.current_tool = None;
                                data.is_running = false;
                                data.failed = true;
                                msg.is_streaming = false;
                                msg.content = error.clone();
                            }
                            ava_agent::agent_loop::SubAgentLiveEvent::Token(_)
                            | ava_agent::agent_loop::SubAgentLiveEvent::Thinking(_)
                            | ava_agent::agent_loop::SubAgentLiveEvent::Checkpoint(_) => {}
                        }
                    }
                    if let Some(summary) = latest_summary {
                        msg.content = summary;
                    }
                }
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
                self.set_status(format!("Plan step completed: {step_id}"), StatusLevel::Info);
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
