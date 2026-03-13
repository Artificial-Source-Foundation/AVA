use super::*;

impl App {
    pub(crate) fn copy_last_response_with_mode(&mut self, force_all: bool) {
        match self.state.messages.last_assistant_content() {
            Some(content) => {
                let content = content.to_owned();
                if !force_all {
                    let blocks = Self::extract_code_blocks(&content);
                    if blocks.len() > 1 {
                        self.state.copy_picker = Some(CopyPickerState {
                            blocks,
                            full_content: content,
                        });
                        self.state.active_modal = Some(ModalType::CopyPicker);
                        return;
                    }
                }
                self.copy_to_clipboard(&content, None);
            }
            None => {
                self.set_status("No assistant message to copy", StatusLevel::Warn);
            }
        }
    }

    pub(crate) fn copy_last_response(&mut self) {
        self.copy_last_response_with_mode(false);
    }

    pub(crate) fn copy_to_clipboard(&mut self, text: &str, label: Option<String>) {
        match arboard::Clipboard::new() {
            Ok(mut clipboard) => match clipboard.set_text(text) {
                Ok(_) => {
                    let status = if let Some(lbl) = label {
                        lbl
                    } else {
                        let preview_len = text.len().min(40);
                        let preview: String = text.chars().take(preview_len).collect();
                        let ellipsis = if text.len() > 40 { "..." } else { "" };
                        format!("Copied to clipboard: \"{preview}{ellipsis}\"")
                    };
                    self.set_status(status, StatusLevel::Info);
                }
                Err(e) => {
                    self.set_status(format!("Clipboard write failed: {e}"), StatusLevel::Error);
                }
            },
            Err(e) => {
                self.set_status(format!("Clipboard unavailable: {e}"), StatusLevel::Error);
            }
        }
    }

    pub(crate) fn handle_btw_query(&mut self, question: String) {
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

        let stack = match self.state.agent.stack() {
            Ok(s) => Arc::clone(s),
            Err(msg) => {
                self.set_status(format!("Cannot run /btw: {msg}"), StatusLevel::Error);
                return;
            }
        };

        let provider_name = self.state.agent.provider_name.clone();
        let model_name = self.state.agent.model_name.clone();

        self.state.btw.pending = true;
        self.state.btw.response = None;
        self.set_status("Thinking about your side question...", StatusLevel::Info);

        let question_clone = question.clone();
        let btw_result: Arc<std::sync::Mutex<Option<crate::state::btw::BtwResponse>>> =
            Arc::new(std::sync::Mutex::new(None));
        let btw_result_clone = Arc::clone(&btw_result);
        self.state.btw.pending_result = Some(btw_result);

        tokio::spawn(async move {
            let mut messages = Vec::with_capacity(history.len() + 2);
            messages.push(ava_types::Message::new(
                ava_types::Role::System,
                "Answer this side question briefly and directly. You have access to the conversation context but no tools. Keep your answer concise."
                    .to_string(),
            ));
            messages.extend(history);
            messages.push(ava_types::Message::new(
                ava_types::Role::User,
                question_clone.clone(),
            ));

            let result = stack
                .router
                .route_required(&provider_name, &model_name)
                .await;
            match result {
                Ok(provider) => match provider.generate(&messages).await {
                    Ok(answer) => {
                        let response = crate::state::btw::BtwResponse {
                            question: question_clone,
                            answer,
                        };
                        if let Ok(mut slot) = btw_result_clone.lock() {
                            *slot = Some(response);
                        }
                    }
                    Err(e) => {
                        let response = crate::state::btw::BtwResponse {
                            question: question_clone,
                            answer: format!("Error: {e}"),
                        };
                        if let Ok(mut slot) = btw_result_clone.lock() {
                            *slot = Some(response);
                        }
                    }
                },
                Err(e) => {
                    let response = crate::state::btw::BtwResponse {
                        question: question_clone,
                        answer: format!("Provider error: {e}"),
                    };
                    if let Ok(mut slot) = btw_result_clone.lock() {
                        *slot = Some(response);
                    }
                }
            }
        });
    }

    pub(crate) fn open_rewind_modal(&mut self) {
        if self.state.rewind.checkpoints.is_empty() {
            self.set_status("No checkpoints to rewind to", StatusLevel::Warn);
            return;
        }
        self.state.rewind.open();
        self.state.active_modal = Some(ModalType::Rewind);
    }

    pub(crate) fn execute_rewind(&mut self, option: crate::state::rewind::RewindOption) {
        use crate::state::rewind::RewindOption;

        let checkpoint_idx = match self.state.rewind.checkpoints.len().checked_sub(1) {
            Some(idx) => idx,
            None => return,
        };

        let checkpoint = &self.state.rewind.checkpoints[checkpoint_idx];
        let msg_index = checkpoint.message_index;
        let preview: String = crate::text_utils::truncate_display(&checkpoint.message_preview, 50);

        match option {
            RewindOption::RestoreCodeAndConversation => {
                let (file_count, errors) = self.state.rewind.restore_files_after(checkpoint_idx);
                if msg_index < self.state.messages.messages.len() {
                    self.state.messages.messages.truncate(msg_index);
                }
                self.state.messages.reset_scroll();
                self.state.rewind.truncate_after(checkpoint_idx);

                let mut status = format!("Rewound to before: '{preview}'");
                if file_count > 0 {
                    status.push_str(&format!(" ({file_count} files restored)"));
                }
                if !errors.is_empty() {
                    status.push_str(&format!(" ({} errors)", errors.len()));
                }
                self.state
                    .messages
                    .push(UiMessage::new(MessageKind::System, &status));
                self.set_status(&status, StatusLevel::Info);
            }
            RewindOption::RestoreConversation => {
                if msg_index < self.state.messages.messages.len() {
                    self.state.messages.messages.truncate(msg_index);
                }
                self.state.messages.reset_scroll();
                self.state.rewind.truncate_after(checkpoint_idx);

                let status = format!("Rewound conversation to before: '{preview}'");
                self.state
                    .messages
                    .push(UiMessage::new(MessageKind::System, &status));
                self.set_status(&status, StatusLevel::Info);
            }
            RewindOption::RestoreCode => {
                let (file_count, errors) = self.state.rewind.restore_files_after(checkpoint_idx);
                if let Some(cp) = self.state.rewind.checkpoints.get_mut(checkpoint_idx) {
                    cp.file_changes.clear();
                }

                let mut status = format!("Restored {file_count} file(s) to before: '{preview}'");
                if !errors.is_empty() {
                    status.push_str(&format!(" ({} errors)", errors.len()));
                }
                self.state
                    .messages
                    .push(UiMessage::new(MessageKind::System, &status));
                self.set_status(&status, StatusLevel::Info);
            }
            RewindOption::SummarizeFromHere => {
                if msg_index > 0 && msg_index <= self.state.messages.messages.len() {
                    let summary_msg = UiMessage::new(
                        MessageKind::System,
                        format!(
                            "--- Earlier conversation summarized ({msg_index} messages) ---\nLast topic before summary: \"{preview}\""
                        ),
                    );
                    let kept: Vec<UiMessage> =
                        self.state.messages.messages.drain(msg_index..).collect();
                    self.state.messages.messages.clear();
                    self.state.messages.messages.push(summary_msg);
                    self.state.messages.messages.extend(kept);
                    self.state.messages.reset_scroll();
                }
                self.set_status("Conversation summarized", StatusLevel::Info);
            }
            RewindOption::Cancel => {}
        }

        self.state.rewind.close();
        self.state.active_modal = None;
    }

    pub(crate) fn extract_code_blocks(content: &str) -> Vec<CodeBlock> {
        let mut blocks = Vec::new();
        let mut in_block = false;
        let mut current_lang = String::new();
        let mut current_content = String::new();
        let mut start_line = 0usize;

        for (i, line) in content.lines().enumerate() {
            let trimmed = line.trim();
            if !in_block && trimmed.starts_with("```") {
                in_block = true;
                current_lang = trimmed[3..].trim().to_string();
                current_content.clear();
                start_line = i + 1;
            } else if in_block && trimmed.starts_with("```") {
                in_block = false;
                if current_content.ends_with('\n') {
                    current_content.pop();
                }
                blocks.push(CodeBlock {
                    language: current_lang.clone(),
                    content: current_content.clone(),
                    start_line: start_line + 1,
                    end_line: i,
                });
            } else if in_block {
                if !current_content.is_empty() {
                    current_content.push('\n');
                }
                current_content.push_str(line);
            }
        }

        blocks
    }
}
