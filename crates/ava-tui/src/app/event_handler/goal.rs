use super::*;
use ava_agent::control_plane::sessions::resolve_session_precedence;
use uuid::Uuid;

impl App {
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

        let history = foreground_history_for_run(
            self.state.session.current_session.as_ref(),
            &self.state.messages.messages,
        );

        // Create a rewind checkpoint at the current message position
        let msg_index = self.state.messages.messages.len();
        self.state.rewind.create_checkpoint(
            msg_index,
            &goal,
            self.state.session.current_session.as_ref(),
        );

        // BUG-41: Track where this turn's messages start so that
        // mark_interrupted_messages only affects the current turn.
        self.state.turn_start_index = self.state.messages.messages.len();

        self.state
            .messages
            .push(UiMessage::new(MessageKind::User, goal.clone()));
        self.cancel_foreground_interactive_requests(app_tx.clone(), "Superseded by a new TUI run");
        self.is_streaming.store(true, Ordering::Relaxed);
        self.state.agent.activity = AgentActivity::Thinking;
        self.state.agent.loop_started_at = Some(std::time::Instant::now());
        let session_id = resolve_session_precedence(
            None,
            self.state
                .session
                .current_session
                .as_ref()
                .map(|session| session.id),
            Uuid::new_v4,
        )
        .session_id;
        let run_id = self.allocate_run_id();
        self.foreground_run_id = Some(run_id);
        self.state.agent.start(
            run_id,
            goal,
            self.state.agent.max_turns,
            app_tx,
            history,
            Some(session_id),
            std::mem::take(&mut self.pending_images),
        );
        self.state.pending_image_count = 0;
    }
}

fn foreground_history_for_run(
    current_session: Option<&ava_types::Session>,
    messages: &[UiMessage],
) -> Vec<ava_types::Message> {
    if let Some(session) = current_session {
        return session.messages.clone();
    }

    messages
        .iter()
        .filter(|ui_msg| !ui_msg.transient)
        .filter_map(|ui_msg| {
            let role = match ui_msg.kind {
                MessageKind::User => ava_types::Role::User,
                MessageKind::Assistant => ava_types::Role::Assistant,
                _ => return None,
            };
            Some(ava_types::Message::new(role, ui_msg.content.clone()))
        })
        .collect()
}

/// Resolve context attachments into a text block to prepend to the goal.
pub(super) fn resolve_attachments(attachments: &[ava_types::ContextAttachment]) -> String {
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
                // Validate the resolved path is within the workspace to prevent
                // reading arbitrary files outside the project via @file mentions.
                match std::fs::canonicalize(&full_path) {
                    Ok(canonical) => {
                        if !canonical.starts_with(&cwd) {
                            tracing::warn!("Attachment path outside workspace: {}", path.display());
                            blocks.push(format!(
                                "<context source=\"{}\" error=\"path is outside the workspace boundary\" />",
                                path.display()
                            ));
                            continue;
                        }
                    }
                    Err(_) => {
                        // If we can't canonicalize (e.g. file doesn't exist), fall through
                        // to the read_to_string which will produce a proper error.
                    }
                }
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
                // Validate the resolved path is within the workspace to prevent
                // listing arbitrary directories outside the project via @folder mentions.
                match std::fs::canonicalize(&full_path) {
                    Ok(canonical) => {
                        if !canonical.starts_with(&cwd) {
                            tracing::warn!(
                                "Folder attachment outside workspace: {}",
                                path.display()
                            );
                            blocks.push(format!(
                                "<context source=\"{}/\" error=\"path is outside the workspace boundary\" />",
                                path.display()
                            ));
                            continue;
                        }
                    }
                    Err(_) => {
                        // If we can't canonicalize (e.g. folder doesn't exist), fall through
                        // to the read_dir which will produce a proper error.
                    }
                }
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

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn foreground_history_prefers_canonical_session_messages() {
        let mut session = ava_types::Session::new();
        let mut user = ava_types::Message::new(ava_types::Role::User, "persisted");
        user.images = vec![ava_types::ImageContent::new(
            "img",
            ava_types::ImageMediaType::Png,
        )];
        session.add_message(user.clone());

        let history = foreground_history_for_run(
            Some(&session),
            &[UiMessage::new(MessageKind::User, "ui-only")],
        );

        assert_eq!(history, vec![user]);
    }

    #[test]
    fn foreground_history_falls_back_to_user_and_assistant_ui_messages() {
        let history = foreground_history_for_run(
            None,
            &[
                UiMessage::new(MessageKind::System, "ignore"),
                UiMessage::new(MessageKind::User, "goal"),
                UiMessage::new(MessageKind::Assistant, "done"),
            ],
        );

        assert_eq!(history.len(), 2);
        assert_eq!(history[0].role, ava_types::Role::User);
        assert_eq!(history[0].content, "goal");
        assert_eq!(history[1].role, ava_types::Role::Assistant);
        assert_eq!(history[1].content, "done");
    }
}
