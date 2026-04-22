use super::*;

/// Collapse clipboard backend errors to one line so status rendering stays intact.
fn single_line_status_text(text: &str) -> String {
    text.split_whitespace().collect::<Vec<_>>().join(" ")
}

fn with_cached_resource<T, R, Create, Op>(
    resource: &mut Option<R>,
    mut create: Create,
    mut op: Op,
) -> Result<T, arboard::Error>
where
    Create: FnMut() -> Result<R, arboard::Error>,
    Op: FnMut(&mut R) -> Result<T, arboard::Error>,
{
    let had_cached_resource = resource.is_some();
    if resource.is_none() {
        *resource = Some(create()?);
    }

    match op(resource.as_mut().expect("resource initialized")) {
        Ok(value) => Ok(value),
        Err(_) if had_cached_resource => {
            *resource = Some(create()?);
            op(resource.as_mut().expect("resource reinitialized"))
        }
        Err(err) => Err(err),
    }
}

impl App {
    fn with_clipboard<T>(
        &mut self,
        op: impl FnMut(&mut arboard::Clipboard) -> Result<T, arboard::Error>,
    ) -> Result<T, arboard::Error> {
        with_cached_resource(&mut self.clipboard, arboard::Clipboard::new, op)
    }

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
  Tab / Shift+Tab          Cycle mode (Code/Plan)
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
        // Try to get image data directly from clipboard
        match self.with_clipboard(|clipboard| clipboard.get_image()) {
            Ok(img) => match Self::encode_rgba_to_png(img.width, img.height, &img.bytes) {
                Ok(png_bytes) => {
                    use base64::Engine;
                    let data = base64::engine::general_purpose::STANDARD.encode(&png_bytes);
                    let image = ava_types::ImageContent::new(data, ava_types::ImageMediaType::Png);
                    self.pending_images.push(image);
                    let count = self.pending_images.len();
                    self.state.pending_image_count = count;
                    self.set_status("Image attached", StatusLevel::Info);
                    return;
                }
                Err(e) => {
                    self.set_status(
                        format!("Failed to encode clipboard image: {e}"),
                        StatusLevel::Error,
                    );
                    return;
                }
            },
            Err(arboard::Error::ContentNotAvailable) => {}
            Err(e) => {
                self.set_status(
                    format!(
                        "Clipboard read failed: {}",
                        single_line_status_text(&e.to_string())
                    ),
                    StatusLevel::Error,
                );
                return;
            }
        }

        let text = self.with_clipboard(|clipboard| clipboard.get_text());
        if let Err(e) = &text {
            if !matches!(e, arboard::Error::ContentNotAvailable) {
                self.set_status(
                    format!(
                        "Clipboard read failed: {}",
                        single_line_status_text(&e.to_string())
                    ),
                    StatusLevel::Error,
                );
                return;
            }
        }

        // Fall back: check if clipboard text is a path to an image file
        if let Ok(text) = text {
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
                                self.set_status("Image attached", StatusLevel::Info);
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
        match self.with_clipboard(|clipboard| clipboard.set_text(text)) {
            Ok(()) => {
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
                self.set_status(
                    format!(
                        "Clipboard write failed: {}",
                        single_line_status_text(&e.to_string())
                    ),
                    StatusLevel::Error,
                );
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
        self.state.btw.checkpoint_session = self.state.session.current_session.clone();
        self.state.btw.active = true;

        // Clear the chat for the branch conversation
        self.state.messages.messages.clear();
        self.state.messages.reset_scroll();
        self.state.session.current_session = None;

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
    /// injecting a brief summary of what was discussed before discarding the branch.
    pub(crate) fn end_btw_branch(&mut self) {
        if !self.state.btw.active {
            return;
        }

        // Summarize the branch before discarding it
        let summary = Self::summarize_btw_branch(&self.state.messages.messages);

        // Restore checkpoint
        self.state.messages.messages = std::mem::take(&mut self.state.btw.checkpoint_messages);
        self.state.messages.scroll_offset = self.state.btw.checkpoint_scroll;
        self.state.session.current_session = self.state.btw.checkpoint_session.take();
        self.state.btw.active = false;

        // Inject summary so the main conversation retains a trace
        self.state
            .messages
            .push(UiMessage::new(MessageKind::System, &summary));

        self.set_status("Restored main conversation", StatusLevel::Info);
    }

    /// Generate a heuristic summary of a /btw branch conversation.
    fn summarize_btw_branch(messages: &[UiMessage]) -> String {
        let user_count = messages
            .iter()
            .filter(|m| matches!(m.kind, MessageKind::User))
            .count();
        let assistant_count = messages
            .iter()
            .filter(|m| matches!(m.kind, MessageKind::Assistant))
            .count();
        let tool_count = messages
            .iter()
            .filter(|m| matches!(m.kind, MessageKind::ToolCall))
            .count();

        // Get the first user question
        let first_question = messages
            .iter()
            .find(|m| matches!(m.kind, MessageKind::User))
            .map(|m| {
                let q = m.content.trim();
                if q.len() > 100 {
                    // Truncate at a char boundary
                    let mut end = 100;
                    while end > 0 && !q.is_char_boundary(end) {
                        end -= 1;
                    }
                    format!("{}...", &q[..end])
                } else {
                    q.to_string()
                }
            })
            .unwrap_or_else(|| "side conversation".to_string());

        format!(
            "[btw branch: asked about '{}' \u{2014} {} messages, {} tool calls]",
            first_question,
            user_count + assistant_count,
            tool_count
        )
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
        let snapshot_hash = checkpoint.snapshot_hash.clone();
        let checkpoint_session = checkpoint.session_snapshot.clone();

        match option {
            RewindOption::RestoreCodeAndConversation => {
                let (file_count, errors) =
                    self.restore_code_at_checkpoint(checkpoint_idx, &snapshot_hash);
                if msg_index < self.state.messages.messages.len() {
                    self.state.messages.messages.truncate(msg_index);
                }
                self.state.messages.reset_scroll();
                self.state.rewind.truncate_after(checkpoint_idx);
                self.state.session.current_session = checkpoint_session.clone();

                let mut status = format!("Rewound to before: '{preview}'");
                if file_count > 0 {
                    let method = if snapshot_hash.is_some() {
                        "via snapshot"
                    } else {
                        "via file backup"
                    };
                    status.push_str(&format!(" ({file_count} files restored {method})"));
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
                self.state.session.current_session = checkpoint_session.clone();

                let status = format!("Rewound conversation to before: '{preview}'");
                self.state
                    .messages
                    .push(UiMessage::new(MessageKind::System, &status));
                self.set_status(&status, StatusLevel::Info);
            }
            RewindOption::RestoreCode => {
                let (file_count, errors) =
                    self.restore_code_at_checkpoint(checkpoint_idx, &snapshot_hash);
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

    /// Restore code at a checkpoint, preferring snapshot-based restore when available.
    ///
    /// When a shadow git snapshot hash is present, uses synchronous git commands
    /// to restore the full project state from the shadow repo. Falls back to
    /// per-file content restore from the checkpoint's `file_changes` list.
    fn restore_code_at_checkpoint(
        &self,
        checkpoint_idx: usize,
        snapshot_hash: &Option<String>,
    ) -> (usize, Vec<String>) {
        if let Some(ref hash) = snapshot_hash {
            // Try snapshot-based restore using synchronous git commands.
            // This is acceptable because rewind is a rare, user-initiated action.
            if let Some(ref manager_handle) = self.state.snapshot_manager {
                if let Ok(guard) = manager_handle.try_read() {
                    if let Some(ref mgr) = *guard {
                        match Self::restore_snapshot_sync(mgr, hash) {
                            Ok(count) => {
                                tracing::info!(
                                    hash = %hash,
                                    files = count,
                                    "restored project state via snapshot"
                                );
                                return (count, Vec::new());
                            }
                            Err(e) => {
                                tracing::warn!(
                                    error = %e,
                                    "snapshot restore failed, falling back to per-file restore"
                                );
                            }
                        }
                    }
                }
            }
        }

        // Fallback: per-file content restore
        self.state.rewind.restore_files_after(checkpoint_idx)
    }

    /// Synchronous snapshot restore using `std::process::Command`.
    ///
    /// The `SnapshotManager`'s async `restore()` cannot be used from the TUI's
    /// sync event handler, so we replicate the essential git operations here.
    fn restore_snapshot_sync(
        mgr: &ava_tools::core::file_snapshot::SnapshotManager,
        snapshot_hash: &str,
    ) -> Result<usize, String> {
        let snapshot_dir = mgr.snapshot_dir();
        let project_root = mgr.project_root();

        // read-tree: load the snapshot's tree into the index
        let read_output = std::process::Command::new("git")
            .env("GIT_DIR", snapshot_dir)
            .env("GIT_WORK_TREE", project_root)
            .args(["read-tree", snapshot_hash])
            .output()
            .map_err(|e| format!("git read-tree failed: {e}"))?;

        if !read_output.status.success() {
            let stderr = String::from_utf8_lossy(&read_output.stderr);
            return Err(format!("git read-tree failed: {stderr}"));
        }

        // checkout-index: write the indexed files to the working directory
        let checkout_output = std::process::Command::new("git")
            .env("GIT_DIR", snapshot_dir)
            .env("GIT_WORK_TREE", project_root)
            .args(["checkout-index", "-a", "-f"])
            .output()
            .map_err(|e| format!("git checkout-index failed: {e}"))?;

        if !checkout_output.status.success() {
            let stderr = String::from_utf8_lossy(&checkout_output.stderr);
            return Err(format!("git checkout-index failed: {stderr}"));
        }

        // Count changed files by comparing HEAD to the target snapshot
        let diff_output = std::process::Command::new("git")
            .env("GIT_DIR", snapshot_dir)
            .env("GIT_WORK_TREE", project_root)
            .args(["diff", "--name-only", "HEAD", snapshot_hash])
            .output();

        let count = match diff_output {
            Ok(out) if out.status.success() => {
                let text = String::from_utf8_lossy(&out.stdout);
                text.lines().filter(|l| !l.trim().is_empty()).count()
            }
            _ => 0, // Non-fatal — we still restored successfully
        };

        Ok(count)
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

#[cfg(test)]
mod tests {
    use super::{single_line_status_text, with_cached_resource};
    use std::cell::Cell;

    #[test]
    fn single_line_status_text_collapses_multiline_errors() {
        assert_eq!(
            single_line_status_text("first line\nsecond   line\tthird"),
            "first line second line third"
        );
    }

    #[test]
    fn single_line_status_text_handles_empty_and_whitespace_only_input() {
        assert_eq!(single_line_status_text(""), "");
        assert_eq!(single_line_status_text("   \n\t  "), "");
    }

    #[test]
    fn cached_resource_retries_once_when_cached_instance_fails() {
        let create_calls = Cell::new(0);
        let op_calls = Cell::new(0);
        let mut resource = Some(1_u8);

        let result = with_cached_resource(
            &mut resource,
            || {
                create_calls.set(create_calls.get() + 1);
                Ok(2_u8)
            },
            |value| {
                op_calls.set(op_calls.get() + 1);
                if op_calls.get() == 1 {
                    Err(arboard::Error::ClipboardOccupied)
                } else {
                    Ok(*value)
                }
            },
        )
        .expect("retry should succeed");

        assert_eq!(result, 2);
        assert_eq!(create_calls.get(), 1);
        assert_eq!(op_calls.get(), 2);
        assert_eq!(resource, Some(2));
    }

    #[test]
    fn cached_resource_does_not_retry_first_use_failures() {
        let create_calls = Cell::new(0);
        let op_calls = Cell::new(0);
        let mut resource = None::<u8>;

        let err = with_cached_resource(
            &mut resource,
            || {
                create_calls.set(create_calls.get() + 1);
                Ok(7_u8)
            },
            |_| -> Result<u8, arboard::Error> {
                op_calls.set(op_calls.get() + 1);
                Err(arboard::Error::ClipboardOccupied)
            },
        )
        .expect_err("first-use failure should bubble up");

        assert!(matches!(err, arboard::Error::ClipboardOccupied));
        assert_eq!(create_calls.get(), 1);
        assert_eq!(op_calls.get(), 1);
        assert_eq!(resource, Some(7));
    }
}
