use super::*;
use crate::state::agent::SubAgentInfo;

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

impl App {
    /// Move the currently running agent to the background.
    /// The agent continues running; its events are routed to a BackgroundTask.
    pub(crate) fn background_current_agent(&mut self, _app_tx: mpsc::UnboundedSender<AppEvent>) {
        let Some(run_id) = self.foreground_run_id else {
            return;
        };

        let goal = self
            .state
            .messages
            .messages
            .iter()
            .rev()
            .find(|m| matches!(m.kind, MessageKind::User))
            .map(|m| m.content.clone())
            .unwrap_or_else(|| "background task".to_string());

        let task_id = {
            let mut bg = self
                .state
                .background
                .lock()
                .unwrap_or_else(|e| e.into_inner());
            let id = bg.add_task(goal.clone());
            bg.add_tokens(
                id,
                self.state.agent.tokens_used.cumulative_input,
                self.state.agent.tokens_used.cumulative_output,
                self.state.agent.cost,
            );
            for msg in &self.state.messages.messages {
                bg.append_message(id, msg.clone());
            }
            id
        };

        self.background_run_routes.insert(run_id, task_id);
        self.foreground_run_id = None;

        self.state.agent.detach_run();
        self.is_streaming.store(false, Ordering::Relaxed);
        self.state.messages.push(UiMessage::new(
            MessageKind::System,
            "Agent moved to background",
        ));
        self.set_status(
            format!("Task #{task_id} moved to background"),
            StatusLevel::Info,
        );
    }

    pub(crate) fn launch_background_agent(
        &mut self,
        goal: String,
        app_tx: mpsc::UnboundedSender<AppEvent>,
        isolated_branch: bool,
    ) {
        let shared_stack = if isolated_branch {
            None
        } else {
            match self.state.agent.stack() {
                Ok(s) => Some(Arc::clone(s)),
                Err(msg) => {
                    self.set_status(
                        format!("Cannot launch background agent: {msg}"),
                        StatusLevel::Error,
                    );
                    return;
                }
            }
        };

        let bg_state = Arc::clone(&self.state.background);
        let task_id = {
            let mut bg = bg_state.lock().unwrap_or_else(|e| e.into_inner());
            bg.add_task(goal.clone())
        };

        let isolation = if isolated_branch {
            let prep = if tokio::runtime::Handle::try_current().is_ok() {
                tokio::task::block_in_place(|| Self::prepare_background_worktree_sync(task_id))
            } else {
                Self::prepare_background_worktree_sync(task_id)
            };

            match prep {
                Ok(info) => {
                    let mut bg = bg_state.lock().unwrap_or_else(|e| e.into_inner());
                    bg.set_isolation(
                        task_id,
                        info.worktree_path.to_string_lossy().to_string(),
                        info.branch_name.clone(),
                    );
                    Some(info)
                }
                Err(err) => {
                    if let Ok(mut bg) = self.state.background.lock() {
                        bg.fail_task(task_id, err.clone());
                    }
                    self.set_status(
                        format!("Background task #{task_id} setup failed: {err}"),
                        StatusLevel::Error,
                    );
                    return;
                }
            }
        } else {
            None
        };

        let max_turns = self.state.agent.max_turns;
        let max_budget_usd = self.state.agent.max_budget_usd;
        let provider_opt = if self.state.agent.provider_name == "default" {
            None
        } else {
            Some(self.state.agent.provider_name.clone())
        };
        let model_opt = if self.state.agent.model_name == "default" {
            None
        } else {
            Some(self.state.agent.model_name.clone())
        };
        let data_dir = self.data_dir.clone();
        let run_id = self.allocate_run_id();
        self.background_run_routes.insert(run_id, task_id);
        let app_tx_clone = app_tx;

        // Create cancel token before spawning so it can be stored for external cancellation
        let cancel = tokio_util::sync::CancellationToken::new();
        {
            let mut bg = bg_state.lock().unwrap_or_else(|e| e.into_inner());
            bg.set_cancel_token(task_id, cancel.clone());
        }

        tokio::spawn(async move {
            let stack = if let Some(isolation) = isolation {
                let config = ava_agent::stack::AgentStackConfig::for_background_isolation(
                    data_dir,
                    provider_opt,
                    model_opt,
                    max_turns,
                    max_budget_usd,
                    isolation.worktree_path,
                );
                match ava_agent::stack::AgentStack::new(config).await {
                    Ok((stack, _, _, _)) => Arc::new(stack),
                    Err(err) => {
                        let _ = app_tx_clone.send(AppEvent::AgentRunDone {
                            run_id,
                            result: Err(format!("failed to init isolated background stack: {err}")),
                        });
                        return;
                    }
                }
            } else {
                let Some(stack) = shared_stack else {
                    let _ = app_tx_clone.send(AppEvent::AgentRunDone {
                        run_id,
                        result: Err(
                            "no shared stack available for non-isolated background run".to_string()
                        ),
                    });
                    return;
                };
                stack
            };

            let (agent_event_tx, mut agent_event_rx) = mpsc::unbounded_channel();

            let relay_tx = app_tx_clone.clone();
            let collector_handle = tokio::spawn(async move {
                while let Some(event) = agent_event_rx.recv().await {
                    let _ = relay_tx.send(AppEvent::AgentRunEvent { run_id, event });
                }
            });

            let result = stack
                .run(
                    &goal,
                    max_turns,
                    Some(agent_event_tx),
                    cancel,
                    Vec::new(),
                    None,
                    Vec::new(),
                    None,
                    Some(run_id.to_string()),
                )
                .await;

            let _ = collector_handle.await;

            let _ = app_tx_clone.send(AppEvent::AgentRunDone {
                run_id,
                result: result.map_err(|err| err.to_string()),
            });
        });

        self.set_status(
            if isolated_branch {
                format!("Task #{task_id} launched in background (isolated branch)")
            } else {
                format!("Task #{task_id} launched in background")
            },
            StatusLevel::Info,
        );
    }

    fn prepare_background_worktree_sync(
        task_id: usize,
    ) -> std::result::Result<BackgroundIsolation, String> {
        let repo_root = Self::run_git_output_sync(&["rev-parse", "--show-toplevel"])
            .map_err(|e| format!("not a git repository: {e}"))?;
        let repo_root = PathBuf::from(repo_root.trim());

        let stamp = SystemTime::now()
            .duration_since(UNIX_EPOCH)
            .map(|d| d.as_secs())
            .unwrap_or(0);
        let branch_name = format!("ava-bg-task-{task_id}-{stamp}");
        let worktree_root = repo_root.join(".ava").join("worktrees");
        std::fs::create_dir_all(&worktree_root)
            .map_err(|e| format!("failed to create worktree dir: {e}"))?;
        let worktree_path = worktree_root.join(&branch_name);

        let output = Command::new("git")
            .args([
                "worktree",
                "add",
                "-b",
                &branch_name,
                &worktree_path.to_string_lossy(),
                "HEAD",
            ])
            .output()
            .map_err(|e| format!("failed to run git worktree add: {e}"))?;

        if !output.status.success() {
            let stderr = String::from_utf8_lossy(&output.stderr).trim().to_string();
            if stderr.is_empty() {
                return Err("git worktree add failed".to_string());
            }
            return Err(format!("git worktree add failed: {stderr}"));
        }

        Ok(BackgroundIsolation {
            worktree_path,
            branch_name,
        })
    }

    fn run_git_output_sync(args: &[&str]) -> std::result::Result<String, String> {
        let output = Command::new("git")
            .args(args)
            .output()
            .map_err(|e| e.to_string())?;
        if !output.status.success() {
            return Err(String::from_utf8_lossy(&output.stderr).trim().to_string());
        }
        Ok(String::from_utf8_lossy(&output.stdout).to_string())
    }

    pub(crate) fn enter_background_task_view(&mut self, task_id: usize) -> bool {
        let bg = self
            .state
            .background
            .lock()
            .unwrap_or_else(|e| e.into_inner());
        if let Some(task) = bg.tasks.iter().find(|t| t.id == task_id) {
            let goal = task.goal_display(50);
            drop(bg);
            self.state.view_mode = ViewMode::BackgroundTask { task_id, goal };
            self.state.messages.reset_scroll();
            true
        } else {
            false
        }
    }

    fn cleanup_background_worktree_sync(
        branch_name: &str,
        worktree_path: &str,
    ) -> std::result::Result<(), String> {
        let mut errors = Vec::new();

        let remove_output = Command::new("git")
            .args(["worktree", "remove", "--force", worktree_path])
            .output();
        match remove_output {
            Ok(output) if output.status.success() => {}
            Ok(output) => {
                errors.push(format!(
                    "worktree remove failed: {}",
                    String::from_utf8_lossy(&output.stderr).trim()
                ));
            }
            Err(err) => {
                errors.push(format!("worktree remove failed: {err}"));
            }
        }

        let _ = Command::new("git").args(["worktree", "prune"]).output();

        let delete_branch_output = Command::new("git")
            .args(["branch", "-D", branch_name])
            .output();
        match delete_branch_output {
            Ok(output) if output.status.success() => {}
            Ok(output) => {
                errors.push(format!(
                    "branch delete failed: {}",
                    String::from_utf8_lossy(&output.stderr).trim()
                ));
            }
            Err(err) => {
                errors.push(format!("branch delete failed: {err}"));
            }
        }

        if errors.is_empty() {
            Ok(())
        } else {
            Err(errors.join("; "))
        }
    }

    pub(super) fn spawn_background_worktree_cleanup(
        task_id: usize,
        branch_name: String,
        worktree_path: String,
        app_tx: mpsc::UnboundedSender<AppEvent>,
    ) {
        tokio::spawn(async move {
            let join = tokio::task::spawn_blocking(move || {
                Self::cleanup_background_worktree_sync(&branch_name, &worktree_path)
            })
            .await;

            let result = match join {
                Ok(result) => result,
                Err(err) => Err(format!("cleanup task failed: {err}")),
            };

            let _ = app_tx.send(AppEvent::BackgroundCleanupResult { task_id, result });
        });
    }

    pub(super) fn handle_background_agent_event(
        &mut self,
        task_id: usize,
        event: ava_agent::AgentEvent,
    ) {
        let mut bg = self
            .state
            .background
            .lock()
            .unwrap_or_else(|e| e.into_inner());
        match event {
            ava_agent::AgentEvent::Token(chunk) => {
                if let Some(task) = bg.tasks.iter_mut().find(|t| t.id == task_id) {
                    if let Some(last) = task.messages.last_mut() {
                        if matches!(last.kind, MessageKind::Assistant) {
                            last.content.push_str(&chunk);
                            return;
                        }
                    }
                    task.messages
                        .push(UiMessage::new(MessageKind::Assistant, chunk));
                }
            }
            ava_agent::AgentEvent::Thinking(content) => {
                bg.append_message(task_id, UiMessage::new(MessageKind::Thinking, content));
            }
            ava_agent::AgentEvent::TokenUsage {
                input_tokens,
                output_tokens,
                cost_usd,
            } => {
                bg.add_tokens(task_id, input_tokens, output_tokens, cost_usd);
            }
            ava_agent::AgentEvent::ToolCall(call) => {
                if matches!(call.name.as_str(), "subagent" | "task") {
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
                        .unwrap_or(false);
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
                        session_messages: Vec::new(),
                        provider: None,
                        resumed: false,
                        cost_usd: None,
                        input_tokens: None,
                        output_tokens: None,
                    });
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
                        session_messages: Vec::new(),
                        provider: None,
                        resumed: false,
                        cost_usd: None,
                        input_tokens: None,
                        output_tokens: None,
                    });
                    bg.append_message(task_id, msg);
                } else {
                    bg.append_message(
                        task_id,
                        UiMessage::new(
                            MessageKind::ToolCall,
                            format!("{} {}", call.name, call.arguments),
                        ),
                    );
                }
            }
            ava_agent::AgentEvent::ToolResult(result) => {
                if let Some(task) = bg.tasks.iter_mut().find(|t| t.id == task_id) {
                    if let Some(msg) = task.messages.iter_mut().rev().find(|m| {
                        matches!(m.kind, MessageKind::SubAgent)
                            && m.sub_agent
                                .as_ref()
                                .is_some_and(|d| d.is_running && d.call_id == result.call_id)
                    }) {
                        msg.content = result.content;
                        let keep_running = msg
                            .sub_agent
                            .as_ref()
                            .is_some_and(|data| data.background && !result.is_error);
                        msg.is_streaming = keep_running;
                        if let Some(data) = msg.sub_agent.as_mut() {
                            data.is_running = keep_running;
                            data.failed = result.is_error && !keep_running;
                            data.current_tool = None;
                        }
                    } else {
                        task.messages
                            .push(UiMessage::new(MessageKind::ToolResult, result.content));
                    }
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
                if let Some(task) = bg.tasks.iter_mut().find(|t| t.id == task_id) {
                    let ui_messages = crate::app::session_messages_to_ui_messages(&messages);

                    if let Some(subagent) =
                        self.state.agent.sub_agents.iter_mut().rev().find(|sa| {
                            (!call_id.is_empty() && sa.call_id == call_id)
                                || normalize_subagent_description(&sa.description)
                                    == normalize_subagent_description(&description)
                        })
                    {
                        subagent.is_running = false;
                        subagent.elapsed = Some(subagent.started_at.elapsed());
                        subagent.current_tool = None;
                        subagent.agent_type = agent_type.clone();
                        subagent.session_id = Some(session_id.clone());
                        subagent.session_messages = ui_messages.clone();
                        subagent.provider = provider.clone();
                        subagent.resumed = resumed;
                        subagent.cost_usd = Some(cost_usd);
                        subagent.input_tokens = Some(input_tokens);
                        subagent.output_tokens = Some(output_tokens);
                    }

                    if let Some(msg) = task.messages.iter_mut().rev().find(|m| {
                        matches!(m.kind, MessageKind::SubAgent)
                            && m.sub_agent.as_ref().is_some_and(|d| {
                                (!call_id.is_empty() && d.call_id == call_id)
                                    || normalize_subagent_description(&d.description)
                                        == normalize_subagent_description(&description)
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
                            data.session_messages = ui_messages;
                            data.current_tool = None;
                            data.provider = provider;
                            data.resumed = resumed;
                            data.cost_usd = Some(cost_usd);
                            data.input_tokens = Some(input_tokens);
                            data.output_tokens = Some(output_tokens);
                        }
                    }
                }
            }
            ava_agent::AgentEvent::PlanStepComplete { step_id } => {
                bg.append_message(
                    task_id,
                    UiMessage::new(
                        MessageKind::System,
                        format!("Plan step completed: {step_id}"),
                    ),
                );
            }
            ava_agent::AgentEvent::StreamingEditProgress {
                tool_name,
                file_path,
                bytes_received,
                ..
            } => {
                let summary = if let Some(path) = file_path {
                    format!("{tool_name} {path}... ({bytes_received} bytes)")
                } else {
                    format!("{tool_name}... ({bytes_received} bytes)")
                };
                bg.append_message(task_id, UiMessage::new(MessageKind::System, summary));
            }
            ava_agent::AgentEvent::Complete(_) => {
                bg.append_message(task_id, UiMessage::new(MessageKind::System, "Run complete"));
            }
            ava_agent::AgentEvent::Error(err) => {
                bg.append_message(task_id, UiMessage::new(MessageKind::Error, err));
            }
            _ => {}
        }
    }
}
