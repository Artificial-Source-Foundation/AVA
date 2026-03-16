use super::*;

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
            let mut bg = self.state.background.lock().unwrap();
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
            let mut bg = bg_state.lock().unwrap();
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
                    let mut bg = bg_state.lock().unwrap();
                    bg.set_isolation(
                        task_id,
                        info.worktree_path.to_string_lossy().to_string(),
                        info.branch_name.clone(),
                    );
                    Some(info)
                }
                Err(err) => {
                    self.state
                        .background
                        .lock()
                        .unwrap()
                        .fail_task(task_id, err.clone());
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
            let mut bg = bg_state.lock().unwrap();
            bg.set_cancel_token(task_id, cancel.clone());
        }

        tokio::spawn(async move {
            let stack = if let Some(isolation) = isolation {
                let config = ava_agent::stack::AgentStackConfig {
                    data_dir,
                    provider: provider_opt,
                    model: model_opt,
                    max_turns,
                    max_budget_usd,
                    yolo: false,
                    injected_provider: None,
                    working_dir: Some(isolation.worktree_path),
                };
                match ava_agent::stack::AgentStack::new(config).await {
                    Ok((stack, _, _)) => Arc::new(stack),
                    Err(err) => {
                        let _ = app_tx_clone.send(AppEvent::AgentRunDone {
                            run_id,
                            result: Err(format!("failed to init isolated background stack: {err}")),
                        });
                        return;
                    }
                }
            } else {
                shared_stack.expect("shared stack exists for non-isolated background runs")
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
        let bg = self.state.background.lock().unwrap();
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
        let mut bg = self.state.background.lock().unwrap();
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
                bg.append_message(
                    task_id,
                    UiMessage::new(
                        MessageKind::ToolCall,
                        format!("{} {}", call.name, call.arguments),
                    ),
                );
            }
            ava_agent::AgentEvent::ToolResult(result) => {
                bg.append_message(
                    task_id,
                    UiMessage::new(MessageKind::ToolResult, result.content),
                );
            }
            ava_agent::AgentEvent::Error(err) => {
                bg.append_message(task_id, UiMessage::new(MessageKind::Error, err));
            }
            _ => {}
        }
    }
}
