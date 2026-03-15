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

    pub(crate) fn show_shortcuts_overlay(&mut self) {
        let shortcuts = "\
Navigation
  Tab / Shift+Tab          Cycle mode (Code/Plan/Praxis)
  Ctrl+K / Ctrl+/          Command palette
  Ctrl+M                   Switch model
  Ctrl+L                   Session picker
  Ctrl+N                   New session
  Ctrl+B                   Background current agent
  Ctrl+S                   Toggle sidebar

Input
  Enter                    Submit message (or steer running agent)
  Shift+Enter              New line
  Alt+Enter                Submit follow-up (while agent running)
  Ctrl+Alt+Enter           Submit post-complete (while agent running)
  Ctrl+V                   Paste image from clipboard
  Ctrl+Y                   Copy last response to clipboard
  Ctrl+C                   Cancel / clear input / quit
  Ctrl+Z                   End /btw branch (restore conversation)
  Esc                      Cancel / close modal
  Esc Esc                  Open rewind (undo) picker

Thinking
  Ctrl+T                   Cycle thinking level
  Ctrl+E                   Expand/collapse thinking blocks

Voice
  Ctrl+R                   Voice input (requires --features voice)

Commands
  /help                    Show all commands
  /shortcuts               Show this overlay
  /model                   Switch model
  /new                     New session
  /btw                     Start side conversation
  /compact                 Compress context
  /commit                  Git commit helper
  /export                  Export conversation
  /later                   Queue post-complete message
  /queue                   View queued messages";
        self.state.info_panel = Some(super::InfoPanelState {
            title: "Keyboard Shortcuts".to_string(),
            content: shortcuts.to_string(),
            scroll: 0,
        });
        self.state.active_modal = Some(super::ModalType::InfoPanel);
    }

    /// Try to paste an image from the system clipboard. If image data is found,
    /// encode it as PNG, create an `ImageContent`, and push it to `pending_images`.
    /// Falls back to pasting text if the clipboard contains a file path to an image.
    pub(crate) fn paste_image_from_clipboard(&mut self) {
        let mut clipboard = match arboard::Clipboard::new() {
            Ok(cb) => cb,
            Err(e) => {
                self.set_status(format!("Clipboard unavailable: {e}"), StatusLevel::Error);
                return;
            }
        };

        // Try to get image data directly from clipboard
        if let Ok(img) = clipboard.get_image() {
            match Self::encode_rgba_to_png(img.width, img.height, &img.bytes) {
                Ok(png_bytes) => {
                    use base64::Engine;
                    let data = base64::engine::general_purpose::STANDARD.encode(&png_bytes);
                    let image = ava_types::ImageContent::new(data, ava_types::ImageMediaType::Png);
                    self.pending_images.push(image);
                    let count = self.pending_images.len();
                    self.state.pending_image_count = count;
                    self.set_status(
                        format!(
                            "Image pasted from clipboard ({count} image{} attached)",
                            if count == 1 { "" } else { "s" }
                        ),
                        StatusLevel::Info,
                    );
                    return;
                }
                Err(e) => {
                    self.set_status(
                        format!("Failed to encode clipboard image: {e}"),
                        StatusLevel::Error,
                    );
                    return;
                }
            }
        }

        // Fall back: check if clipboard text is a path to an image file
        if let Ok(text) = clipboard.get_text() {
            let trimmed = text.trim();
            let path = std::path::Path::new(trimmed);
            if path.is_file() {
                if let Some(ext) = path.extension().and_then(|e| e.to_str()) {
                    if ava_types::ImageMediaType::is_supported_extension(ext) {
                        match ava_types::ImageContent::from_file(path) {
                            Ok(image) => {
                                self.pending_images.push(image);
                                let count = self.pending_images.len();
                                self.state.pending_image_count = count;
                                self.set_status(
                                    format!(
                                        "Image attached from path ({count} image{} attached)",
                                        if count == 1 { "" } else { "s" }
                                    ),
                                    StatusLevel::Info,
                                );
                                return;
                            }
                            Err(e) => {
                                self.set_status(
                                    format!("Failed to read image: {e}"),
                                    StatusLevel::Error,
                                );
                                return;
                            }
                        }
                    }
                }
            }
        }

        self.set_status(
            "No image in clipboard (use Ctrl+V to paste images, bracketed paste for text)",
            StatusLevel::Warn,
        );
    }

    /// Encode raw RGBA pixel data to PNG bytes.
    fn encode_rgba_to_png(width: usize, height: usize, rgba: &[u8]) -> Result<Vec<u8>, String> {
        let mut buf = Vec::new();
        {
            let mut encoder =
                png::Encoder::new(std::io::Cursor::new(&mut buf), width as u32, height as u32);
            encoder.set_color(png::ColorType::Rgba);
            encoder.set_depth(png::BitDepth::Eight);
            let mut writer = encoder
                .write_header()
                .map_err(|e| format!("PNG header error: {e}"))?;
            writer
                .write_image_data(rgba)
                .map_err(|e| format!("PNG write error: {e}"))?;
        }
        Ok(buf)
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

    /// Start a `/btw` conversation branch. Saves current messages + scroll
    /// as a checkpoint, clears chat for the side conversation, and optionally
    /// seeds the composer with a question.
    pub(crate) fn start_btw_branch(&mut self, question: Option<String>) {
        // Save checkpoint
        self.state.btw.checkpoint_messages = self.state.messages.messages.clone();
        self.state.btw.checkpoint_scroll = self.state.messages.scroll_offset;
        self.state.btw.active = true;

        // Clear the chat for the branch conversation
        self.state.messages.messages.clear();
        self.state.messages.reset_scroll();

        self.set_status(
            "Entered /btw branch \u{2014} use /btw end or Ctrl+Z to restore",
            StatusLevel::Info,
        );
        self.state.messages.push(UiMessage::new(
            MessageKind::System,
            "Entered a /btw conversation branch. Your main conversation is saved. Use /btw end or Ctrl+Z to restore it.",
        ));

        // If a question was provided, seed it into the composer
        if let Some(q) = question {
            self.state.input.buffer = q;
            self.state.input.cursor = self.state.input.buffer.len();
        }
    }

    /// End the current `/btw` branch: restore the checkpoint messages and scroll,
    /// discarding everything from the branch.
    pub(crate) fn end_btw_branch(&mut self) {
        if !self.state.btw.active {
            return;
        }

        // Restore checkpoint
        self.state.messages.messages = std::mem::take(&mut self.state.btw.checkpoint_messages);
        self.state.messages.scroll_offset = self.state.btw.checkpoint_scroll;
        self.state.btw.active = false;

        self.set_status("Restored main conversation", StatusLevel::Info);
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

        let Some(checkpoint_idx) = self.state.rewind.checkpoints.len().checked_sub(1) else {
            return;
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
