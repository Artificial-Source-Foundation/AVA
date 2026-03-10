use super::*;
use tracing::{debug, info};

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
            ava_agent::AgentEvent::TokenUsage { input_tokens, output_tokens, cost_usd } => {
                self.state.agent.tokens_used.input += input_tokens;
                self.state.agent.tokens_used.output += output_tokens;
                self.state.agent.cost += cost_usd;
            }
            ava_agent::AgentEvent::ToolCall(call) => {
                debug!(tool = %call.name, "TUI received ToolCall");
                // Force flush any buffered tokens before tool call
                self.force_flush_token_buffer();
                // Mark last assistant/thinking message as done streaming
                if let Some(last) = self.state.messages.messages.last_mut() {
                    if matches!(last.kind, MessageKind::Assistant | MessageKind::Thinking) {
                        last.is_streaming = false;
                        if matches!(last.kind, MessageKind::Assistant) && last.model_name.is_none() {
                            last.model_name = Some(self.state.agent.model_name.clone());
                        }
                    }
                }
                self.state.agent.activity = AgentActivity::ExecutingTool(call.name.clone());
                self.state.agent.tool_start = Some(std::time::Instant::now());

                // Check if we need approval
                if !self.state.permission.permission_level.is_auto_approve()
                    && !self.state.permission.session_approved.contains(&call.name)
                {
                    let (tx, _rx) = tokio::sync::oneshot::channel();
                    let request = crate::state::permission::ApprovalRequest {
                        call: call.clone(),
                        approve_tx: tx,
                        inspection: None,
                    };
                    self.state.permission.enqueue(request);
                    self.state.active_modal = Some(ModalType::ToolApproval);
                }
                self.state.messages.push(UiMessage::new(
                    MessageKind::ToolCall,
                    format!("{} {}", call.name, call.arguments),
                ));
            }
            ava_agent::AgentEvent::ToolResult(result) => {
                self.state.agent.activity = AgentActivity::Thinking;
                self.state.agent.tool_start = None;
                self.state
                    .messages
                    .push(UiMessage::new(MessageKind::ToolResult, result.content));
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

                // Continuous voice: restart recording after agent completes
                if self.state.voice.continuous && self.state.voice.phase == VoicePhase::Idle {
                    self.toggle_voice(app_tx.clone());
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
        agent_tx: mpsc::UnboundedSender<ava_agent::AgentEvent>,
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
                        let _ = app_tx_clone.send(AppEvent::ShellResult(
                            MessageKind::Error,
                            e.to_string(),
                        ));
                    }
                }
            });
            return;
        }

        // Handle slash commands
        if let Some((kind, msg)) = self.handle_slash_command(&goal) {
            self.state
                .messages
                .push(UiMessage::new(MessageKind::User, goal));
            self.state.messages.push(UiMessage::new(kind, msg));
            return;
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

        self.state
            .messages
            .push(UiMessage::new(MessageKind::User, goal.clone()));
        self.is_streaming.store(true, Ordering::Relaxed);
        self.state.agent.activity = AgentActivity::Thinking;
        self.state
            .agent
            .start(goal, self.state.agent.max_turns, app_tx, agent_tx, history);
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
            let wav = self
                .audio_recorder
                .as_mut()
                .and_then(|r| r.stop().ok());

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
                                    match transcriber.transcribe(wav_data, config.language.as_deref()).await {
                                        Ok(text) => { let _ = tx.send(AppEvent::VoiceReady(text)); }
                                        Err(e) => { let _ = tx.send(AppEvent::VoiceError(e.to_string())); }
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
                                    match transcriber.transcribe(wav_data, config.language.as_deref()).await {
                                        Ok(text) => { let _ = tx.send(AppEvent::VoiceReady(text)); }
                                        Err(e) => { let _ = tx.send(AppEvent::VoiceError(e.to_string())); }
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
