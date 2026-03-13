use super::*;
use crate::state::agent::SubAgentInfo;
use crate::state::rewind::{snapshot_file, ChangeType, FileChange};
use ava_types::{MessageTier, QueuedMessage};
use tracing::{debug, info};

impl App {
    /// Fire hooks asynchronously. Results are sent back via AppEvent::HookResult.
    /// If no hooks are registered for the event, this is a no-op.
    pub(crate) fn fire_hooks_async(
        &self,
        event: HookEvent,
        context: HookContext,
        app_tx: mpsc::UnboundedSender<AppEvent>,
    ) {
        // Skip if no hooks are registered (avoids spawning for the common case)
        if self.state.hooks.is_empty() {
            return;
        }
        let registry = self.state.hooks.clone();
        // Guard against missing Tokio runtime (e.g., in sync tests)
        if tokio::runtime::Handle::try_current().is_err() {
            return;
        }
        tokio::spawn(async move {
            let (_, executions) = HookRunner::run_hooks(&registry, event.clone(), context).await;
            for exec in &executions {
                let _ = app_tx.send(AppEvent::HookResult {
                    event: exec.event.clone(),
                    result: exec.result.clone(),
                    description: exec.description.clone(),
                });
            }
        });
    }

    /// Build a HookContext with common session/model fields populated.
    pub(crate) fn build_hook_context(&self, event: &HookEvent) -> HookContext {
        let mut ctx = HookContext::for_event(event);
        ctx.model = Some(self.state.agent.model_name.clone());
        ctx.session_id = self
            .state
            .session
            .current_session
            .as_ref()
            .map(|s| s.id.to_string());
        ctx.tokens_used =
            Some(self.state.agent.tokens_used.input + self.state.agent.tokens_used.output);
        ctx.cost_usd = Some(self.state.agent.cost);
        ctx
    }

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
                self.state.agent.activity = AgentActivity::Thinking;
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
                self.state.agent.tokens_used.input += input_tokens;
                self.state.agent.tokens_used.output += output_tokens;
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
                // Mark last assistant/thinking message as done streaming
                if let Some(last) = self.state.messages.messages.last_mut() {
                    if matches!(last.kind, MessageKind::Assistant | MessageKind::Thinking) {
                        last.is_streaming = false;
                        if matches!(last.kind, MessageKind::Assistant) && last.model_name.is_none()
                        {
                            last.model_name = Some(self.state.agent.model_name.clone());
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
                    });

                    // Create a SubAgent message instead of a regular ToolCall
                    let mut msg = UiMessage::new(MessageKind::SubAgent, String::new());
                    msg.is_streaming = true;
                    msg.sub_agent = Some(crate::state::messages::SubAgentData {
                        description,
                        tool_count: 0,
                        duration: None,
                        is_running: true,
                        call_id: call.id.clone(),
                        session_id: None,
                        session_messages: Vec::new(),
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

                // Force flush any remaining buffered tokens
                self.force_flush_token_buffer();
                // Mark last assistant message as done streaming and attach model info
                if let Some(last) = self.state.messages.messages.last_mut() {
                    last.is_streaming = false;
                    if matches!(last.kind, MessageKind::Assistant) && last.model_name.is_none() {
                        last.model_name = Some(self.state.agent.model_name.clone());
                    }
                }
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

                // Accumulate sub-agent token usage into the parent's totals
                self.state.agent.tokens_used.input += input_tokens;
                self.state.agent.tokens_used.output += output_tokens;
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
                    .find(|s| s.description == description)
                {
                    sa.session_id = Some(session_id.clone());
                    sa.session_messages = ui_messages.clone();
                }

                // Update the SubAgentData in the matching UI message
                if let Some(msg) = self.state.messages.messages.iter_mut().rev().find(|m| {
                    matches!(m.kind, MessageKind::SubAgent)
                        && m.sub_agent.as_ref().is_some_and(|d| d.call_id == call_id)
                }) {
                    if let Some(data) = msg.sub_agent.as_mut() {
                        data.session_id = Some(session_id);
                        data.session_messages = ui_messages;
                    }
                }
            }
            ava_agent::AgentEvent::ToolStats(_) => {}
            ava_agent::AgentEvent::Error(err) => {
                info!(error = %err, "TUI received AgentEvent::Error");
                // Force flush any remaining buffered tokens
                self.force_flush_token_buffer();
                // Mark last assistant message as done streaming and attach model info
                if let Some(last) = self.state.messages.messages.last_mut() {
                    last.is_streaming = false;
                    if matches!(last.kind, MessageKind::Assistant) && last.model_name.is_none() {
                        last.model_name = Some(self.state.agent.model_name.clone());
                    }
                }
                self.state.agent.activity = AgentActivity::Idle;
                self.state
                    .messages
                    .push(UiMessage::new(MessageKind::Error, err));
            }
        }
    }

    pub(crate) fn submit_goal(
        &mut self,
        goal: String,
        app_tx: mpsc::UnboundedSender<AppEvent>,
        _agent_tx: mpsc::UnboundedSender<ava_agent::AgentEvent>,
    ) {
        // Handle shell commands (! prefix)
        if let Some(cmd) = goal.strip_prefix('!') {
            let cmd = cmd.trim();
            if cmd.is_empty() {
                return;
            }
            self.state
                .messages
                .push(UiMessage::new(MessageKind::User, goal.clone()));

            let cmd_owned = cmd.to_string();
            let app_tx_clone = app_tx;
            tokio::spawn(async move {
                let output = tokio::process::Command::new("sh")
                    .arg("-c")
                    .arg(&cmd_owned)
                    .output()
                    .await;
                match output {
                    Ok(out) => {
                        let stdout = String::from_utf8_lossy(&out.stdout);
                        let stderr = String::from_utf8_lossy(&out.stderr);
                        let content = if stderr.is_empty() {
                            stdout.to_string()
                        } else if stdout.is_empty() {
                            stderr.to_string()
                        } else {
                            format!("{stdout}\n{stderr}")
                        };
                        let kind = if out.status.success() {
                            MessageKind::System
                        } else {
                            MessageKind::Error
                        };
                        let _ = app_tx_clone.send(AppEvent::ShellResult(kind, content));
                    }
                    Err(e) => {
                        let _ = app_tx_clone
                            .send(AppEvent::ShellResult(MessageKind::Error, e.to_string()));
                    }
                }
            });
            return;
        }

        // Handle slash commands
        if let Some((kind, msg)) = self.handle_slash_command(&goal, Some(app_tx.clone())) {
            self.state
                .messages
                .push(UiMessage::new(MessageKind::User, goal));
            self.state.messages.push(UiMessage::new(kind, msg));
            return;
        }

        // Check if /bg set a pending background goal
        if let Some(bg_goal) = self.pending_bg_goal.take() {
            self.state
                .messages
                .push(UiMessage::new(MessageKind::User, goal));
            self.launch_background_agent(bg_goal.goal, app_tx, bg_goal.isolated_branch);
            return;
        }

        // Handle custom slash commands — resolve prompt and redirect as agent goal
        if goal.starts_with('/') {
            if let Some(result) = self.try_resolve_custom_command(&goal) {
                match result {
                    Ok(resolved_prompt) => {
                        // Show the original command as the user message
                        self.state
                            .messages
                            .push(UiMessage::new(MessageKind::User, goal));
                        // Submit the resolved prompt as the actual goal
                        // (fall through to agent submission below with the resolved prompt)
                        return self.submit_goal(resolved_prompt, app_tx, _agent_tx);
                    }
                    Err(err) => {
                        self.state
                            .messages
                            .push(UiMessage::new(MessageKind::User, goal));
                        self.state
                            .messages
                            .push(UiMessage::new(MessageKind::Error, err));
                        return;
                    }
                }
            }
            // If it starts with / but wasn't handled by handle_slash_command or custom
            // commands, it falls through to the agent (this handles unknown slash commands
            // that returned None from handle_slash_command for modal actions).
        }

        // Resolve @-mentions: parse mentions from goal text and resolve attachments.
        // Also consume any attachments added via the autocomplete picker.
        let (mut mention_attachments, cleaned_goal) = ava_types::parse_mentions(&goal);
        // Merge picker attachments (those added via Tab/Enter in the mention picker)
        let picker_attachments = std::mem::take(&mut self.state.input.attachments);
        mention_attachments.extend(picker_attachments);

        // Build the final goal with context blocks prepended
        let goal = if mention_attachments.is_empty() {
            goal
        } else {
            let context_block = resolve_attachments(&mention_attachments);
            let user_text = if cleaned_goal.is_empty() {
                // All text was @mentions — use a default prompt
                "Please review the attached context.".to_string()
            } else {
                cleaned_goal
            };
            if context_block.is_empty() {
                user_text
            } else {
                format!("{context_block}\n\n{user_text}")
            }
        };

        // Fire UserPromptSubmit hooks
        {
            let mut ctx = self.build_hook_context(&HookEvent::UserPromptSubmit);
            ctx.prompt = Some(goal.clone());
            self.fire_hooks_async(HookEvent::UserPromptSubmit, ctx, app_tx.clone());
        }

        // Build conversation history from previous UI messages for LLM context.
        // Only include User and Assistant messages (tool calls/results/system are internal).
        let history: Vec<ava_types::Message> = self
            .state
            .messages
            .messages
            .iter()
            .filter_map(|ui_msg| {
                let role = match ui_msg.kind {
                    MessageKind::User => ava_types::Role::User,
                    MessageKind::Assistant => ava_types::Role::Assistant,
                    _ => return None,
                };
                Some(ava_types::Message::new(role, ui_msg.content.clone()))
            })
            .collect();

        // Create a rewind checkpoint at the current message position
        let msg_index = self.state.messages.messages.len();
        self.state.rewind.create_checkpoint(msg_index, &goal);

        self.state
            .messages
            .push(UiMessage::new(MessageKind::User, goal.clone()));
        self.is_streaming.store(true, Ordering::Relaxed);
        self.state.agent.activity = AgentActivity::Thinking;
        let parent_session_id = self
            .state
            .session
            .current_session
            .as_ref()
            .map(|s| s.id.to_string());
        let run_id = self.allocate_run_id();
        self.foreground_run_id = Some(run_id);
        self.state.agent.start(
            run_id,
            goal,
            self.state.agent.max_turns,
            app_tx,
            history,
            parent_session_id,
            std::mem::take(&mut self.pending_images),
        );
    }

    /// Send a mid-stream message to the running agent via the message queue.
    pub(crate) fn send_queued_message(&mut self, text: String, tier: MessageTier) {
        let label = match &tier {
            MessageTier::Steering => "steering",
            MessageTier::FollowUp => "follow-up",
            MessageTier::PostComplete { group } => {
                // Can't return a &str from format!, so we handle display below
                let _ = group;
                "post-complete"
            }
        };

        if let Some(ref tx) = self.state.agent.message_tx {
            let msg = QueuedMessage {
                text: text.clone(),
                tier: tier.clone(),
            };
            if tx.send(msg).is_ok() {
                // Add to UI queue display
                self.state
                    .input
                    .queue_display
                    .push(text.clone(), tier.clone());
                // Show user message in chat with a tier badge
                let badge = match &tier {
                    MessageTier::Steering => "[S]".to_string(),
                    MessageTier::FollowUp => "[F]".to_string(),
                    MessageTier::PostComplete { group } => format!("[G{group}]"),
                };
                self.state
                    .messages
                    .push(UiMessage::new(MessageKind::User, format!("{badge} {text}")));
                self.set_status(format!("Queued {label} message"), StatusLevel::Info);
            } else {
                self.set_status(
                    format!("Failed to queue {label} message — agent may have finished"),
                    StatusLevel::Error,
                );
            }
        } else {
            self.set_status("No running agent to send messages to", StatusLevel::Warn);
        }
    }

    pub(crate) fn toggle_voice(&mut self, app_tx: mpsc::UnboundedSender<AppEvent>) {
        match self.state.voice.phase {
            VoicePhase::Idle => {
                #[cfg(feature = "voice")]
                {
                    self.start_recording(app_tx);
                }
                #[cfg(not(feature = "voice"))]
                {
                    let _ = app_tx;
                    self.set_status(
                        "Voice requires --features voice. Rebuild with: cargo build --features voice",
                        StatusLevel::Error,
                    );
                }
            }
            VoicePhase::Recording => {
                self.stop_and_transcribe(app_tx);
            }
            VoicePhase::Transcribing => {
                // Ignore toggle while transcribing
            }
        }
    }

    #[cfg(feature = "voice")]
    fn start_recording(&mut self, app_tx: mpsc::UnboundedSender<AppEvent>) {
        match crate::audio::AudioRecorder::start(
            app_tx.clone(),
            self.voice_config.silence_threshold,
            self.voice_config.silence_duration_secs,
            self.voice_config.max_duration_secs,
        ) {
            Ok(recorder) => {
                self.audio_recorder = Some(recorder);
                self.state.voice.phase = VoicePhase::Recording;
                self.state.voice.recording_start = Some(std::time::Instant::now());
                self.state.voice.error = None;

                // Max duration timeout
                let max_secs = self.voice_config.max_duration_secs;
                let tx = app_tx;
                tokio::spawn(async move {
                    tokio::time::sleep(std::time::Duration::from_secs(max_secs as u64)).await;
                    let _ = tx.send(AppEvent::VoiceSilenceDetected);
                });
            }
            Err(err) => {
                let _ = app_tx.send(AppEvent::VoiceError(err));
            }
        }
    }

    pub(crate) fn stop_and_transcribe(&mut self, app_tx: mpsc::UnboundedSender<AppEvent>) {
        #[cfg(feature = "voice")]
        {
            let wav = self.audio_recorder.as_mut().and_then(|r| r.stop().ok());

            self.audio_recorder = None;

            match wav {
                Some(wav_data) => {
                    self.state.voice.phase = VoicePhase::Transcribing;
                    self.state.voice.amplitude = 0.0;

                    // Initialize transcriber lazily
                    if self.transcriber.is_none() {
                        let config = self.voice_config.clone();
                        let tx = app_tx.clone();
                        tokio::spawn(async move {
                            match crate::transcribe::create_transcriber(&config).await {
                                Ok(transcriber) => {
                                    match transcriber
                                        .transcribe(wav_data, config.language.as_deref())
                                        .await
                                    {
                                        Ok(text) => {
                                            let _ = tx.send(AppEvent::VoiceReady(text));
                                        }
                                        Err(e) => {
                                            let _ = tx.send(AppEvent::VoiceError(e.to_string()));
                                        }
                                    }
                                }
                                Err(e) => {
                                    let _ = tx.send(AppEvent::VoiceError(e.to_string()));
                                }
                            }
                        });
                    } else {
                        // This branch can't easily borrow self.transcriber for async use,
                        // so we always use the spawn pattern above
                        let config = self.voice_config.clone();
                        let tx = app_tx;
                        tokio::spawn(async move {
                            match crate::transcribe::create_transcriber(&config).await {
                                Ok(transcriber) => {
                                    match transcriber
                                        .transcribe(wav_data, config.language.as_deref())
                                        .await
                                    {
                                        Ok(text) => {
                                            let _ = tx.send(AppEvent::VoiceReady(text));
                                        }
                                        Err(e) => {
                                            let _ = tx.send(AppEvent::VoiceError(e.to_string()));
                                        }
                                    }
                                }
                                Err(e) => {
                                    let _ = tx.send(AppEvent::VoiceError(e.to_string()));
                                }
                            }
                        });
                    }
                }
                None => {
                    self.state.voice.phase = VoicePhase::Idle;
                    self.state.voice.recording_start = None;
                    let _ = app_tx.send(AppEvent::VoiceError("No audio captured".to_string()));
                }
            }
        }

        #[cfg(not(feature = "voice"))]
        {
            let _ = app_tx;
        }
    }
}

/// Resolve context attachments into a text block to prepend to the goal.
fn resolve_attachments(attachments: &[ava_types::ContextAttachment]) -> String {
    let mut blocks = Vec::new();
    let cwd = std::env::current_dir().unwrap_or_default();

    for attachment in attachments {
        match attachment {
            ava_types::ContextAttachment::File { path } => {
                let full_path = if path.is_absolute() {
                    path.clone()
                } else {
                    cwd.join(path)
                };
                match std::fs::read_to_string(&full_path) {
                    Ok(content) => {
                        // Truncate very large files
                        let (safe_slice, was_truncated) =
                            crate::text_utils::truncate_bytes_safe(&content, 50_000);
                        let truncated = if was_truncated {
                            format!(
                                "{}... [truncated, {} bytes total]",
                                safe_slice,
                                content.len()
                            )
                        } else {
                            content
                        };
                        blocks.push(format!(
                            "<context source=\"{}\">\n{}\n</context>",
                            path.display(),
                            truncated
                        ));
                    }
                    Err(e) => {
                        blocks.push(format!(
                            "<context source=\"{}\" error=\"{}\" />",
                            path.display(),
                            e
                        ));
                    }
                }
            }
            ava_types::ContextAttachment::Folder { path } => {
                let full_path = if path.is_absolute() {
                    path.clone()
                } else {
                    cwd.join(path)
                };
                match std::fs::read_dir(&full_path) {
                    Ok(entries) => {
                        let mut listing = Vec::new();
                        let mut entries: Vec<_> = entries.filter_map(|e| e.ok()).collect();
                        entries.sort_by_key(|e| e.file_name());
                        for entry in entries.iter().take(100) {
                            let name = entry.file_name();
                            let is_dir = entry.path().is_dir();
                            let suffix = if is_dir { "/" } else { "" };
                            listing.push(format!("  {}{}", name.to_string_lossy(), suffix));
                        }
                        if entries.len() > 100 {
                            listing.push(format!("  ... and {} more", entries.len() - 100));
                        }
                        blocks.push(format!(
                            "<context source=\"{}/\" type=\"directory\">\n{}\n</context>",
                            path.display(),
                            listing.join("\n")
                        ));
                    }
                    Err(e) => {
                        blocks.push(format!(
                            "<context source=\"{}/\" error=\"{}\" />",
                            path.display(),
                            e
                        ));
                    }
                }
            }
            ava_types::ContextAttachment::CodebaseQuery { query } => {
                // For codebase queries, we inject the query as a search directive.
                // The agent will use its codebase_search tool for actual searching.
                blocks.push(format!(
                    "<context type=\"codebase_search\" query=\"{query}\">\n\
                     Please search the codebase for: {query}\n\
                     </context>"
                ));
            }
        }
    }

    blocks.join("\n\n")
}
