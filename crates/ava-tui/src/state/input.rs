use crate::state::file_scanner::MentionFileCache;
use crate::state::message_queue::MessageQueueDisplay;
use crate::widgets::autocomplete::{AutocompleteItem, AutocompleteState, AutocompleteTrigger};
use ava_types::ContextAttachment;
use std::collections::HashMap;

/// Threshold for collapsing pasted text into a placeholder.
const PASTE_LINE_THRESHOLD: usize = 5;
const PASTE_CHAR_THRESHOLD: usize = 500;

/// Regex-like prefix/suffix for paste placeholders.
const PASTE_PREFIX: &str = "[Pasted Text: ";
const PASTE_SUFFIX: &str = "]";

#[derive(Debug, Default)]
pub struct InputState {
    pub buffer: String,
    pub cursor: usize,
    pub history: Vec<String>,
    pub history_index: Option<usize>,
    pub saved_input: String,
    pub autocomplete: Option<AutocompleteState>,
    /// Maps placeholder string (e.g. "[Pasted Text: 42 lines]") to the full paste content.
    pub pending_pastes: HashMap<String, String>,
    /// Tracks how many times a given description has been used, for dedup numbering.
    paste_counter: HashMap<String, usize>,
    /// Additional slash-command autocomplete items from custom commands.
    pub custom_slash_items: Vec<AutocompleteItem>,
    /// Display state for queued mid-stream messages.
    pub queue_display: MessageQueueDisplay,
    /// Context attachments from @-mentions (resolved on submit).
    pub attachments: Vec<ContextAttachment>,
    /// Cached project file scan results for @-mention autocomplete.
    mention_cache: MentionFileCache,
}

impl InputState {
    pub fn insert_char(&mut self, ch: char) {
        self.buffer.insert(self.cursor, ch);
        self.cursor += ch.len_utf8();
        self.refresh_autocomplete();
    }

    pub fn insert_str(&mut self, value: &str) {
        self.buffer.insert_str(self.cursor, value);
        self.cursor += value.len();
        self.refresh_autocomplete();
    }

    pub fn delete_backward(&mut self) {
        if self.cursor == 0 {
            return;
        }
        let mut prev = self.cursor - 1;
        while !self.buffer.is_char_boundary(prev) {
            prev -= 1;
        }
        self.buffer.replace_range(prev..self.cursor, "");
        self.cursor = prev;
        self.refresh_autocomplete();
    }

    /// Delete character at cursor (or merge with next line if at end of line).
    pub fn delete_forward(&mut self) {
        if self.cursor >= self.buffer.len() {
            return;
        }
        let mut next = self.cursor + 1;
        while next < self.buffer.len() && !self.buffer.is_char_boundary(next) {
            next += 1;
        }
        self.buffer.replace_range(self.cursor..next, "");
        self.refresh_autocomplete();
    }

    pub fn clear(&mut self) {
        self.buffer.clear();
        self.cursor = 0;
        self.autocomplete = None;
        self.pending_pastes.clear();
        self.paste_counter.clear();
        self.attachments.clear();
        self.mention_cache.invalidate();
    }

    pub fn move_left(&mut self) {
        if self.cursor == 0 {
            return;
        }
        let mut prev = self.cursor - 1;
        while !self.buffer.is_char_boundary(prev) {
            prev -= 1;
        }
        self.cursor = prev;
    }

    pub fn move_right(&mut self) {
        if self.cursor >= self.buffer.len() {
            return;
        }
        let mut next = self.cursor + 1;
        while next < self.buffer.len() && !self.buffer.is_char_boundary(next) {
            next += 1;
        }
        self.cursor = next;
    }

    /// Move cursor to start of current line.
    pub fn move_home(&mut self) {
        let line_start = self.buffer[..self.cursor]
            .rfind('\n')
            .map(|p| p + 1)
            .unwrap_or(0);
        self.cursor = line_start;
    }

    /// Move cursor to end of current line.
    pub fn move_end(&mut self) {
        let line_end = self.buffer[self.cursor..]
            .find('\n')
            .map(|p| self.cursor + p)
            .unwrap_or(self.buffer.len());
        self.cursor = line_end;
    }

    /// Move cursor up one line, preserving column where possible.
    /// Returns `false` if already on the first line (caller can fall through to history).
    pub fn move_up(&mut self) -> bool {
        let (line, col) = self.cursor_line_col();
        if line == 0 {
            return false;
        }
        self.set_cursor_line_col(line - 1, col);
        true
    }

    /// Move cursor down one line, preserving column where possible.
    /// Returns `false` if already on the last line (caller can fall through to history).
    pub fn move_down(&mut self) -> bool {
        let (line, col) = self.cursor_line_col();
        let line_count = self.buffer.split('\n').count();
        if line + 1 >= line_count {
            return false;
        }
        self.set_cursor_line_col(line + 1, col);
        true
    }

    /// Returns `true` if the buffer contains multiple lines.
    pub fn is_multiline(&self) -> bool {
        self.buffer.contains('\n')
    }

    /// Return (line_index, column_byte_offset_within_line) of the cursor.
    pub fn cursor_line_col(&self) -> (usize, usize) {
        let before = &self.buffer[..self.cursor];
        let line = before.matches('\n').count();
        let line_start = before.rfind('\n').map(|p| p + 1).unwrap_or(0);
        let col = self.cursor - line_start;
        (line, col)
    }

    /// Set cursor to the given line and column (clamped to line length).
    fn set_cursor_line_col(&mut self, target_line: usize, target_col: usize) {
        let mut offset = 0;
        for (i, line_str) in self.buffer.split('\n').enumerate() {
            if i == target_line {
                let col = target_col.min(line_str.len());
                self.cursor = offset + col;
                return;
            }
            offset += line_str.len() + 1; // +1 for '\n'
        }
        // Shouldn't happen, but clamp to end
        self.cursor = self.buffer.len();
    }

    /// Handle a paste event. If the text exceeds the threshold (5+ lines or 500+ chars),
    /// insert a compact placeholder and store the full content for later expansion.
    /// Below the threshold, insert text directly.
    pub fn handle_paste(&mut self, text: String) {
        let line_count = text.lines().count();
        let char_count = text.len();

        if line_count >= PASTE_LINE_THRESHOLD || char_count >= PASTE_CHAR_THRESHOLD {
            // Build description based on which threshold was hit
            let desc = if line_count >= PASTE_LINE_THRESHOLD {
                format!("{line_count} lines")
            } else {
                format!("{char_count} chars")
            };

            // Dedup: track how many times this description has been used
            let count = self.paste_counter.entry(desc.clone()).or_insert(0);
            *count += 1;
            let placeholder = if *count > 1 {
                format!("{PASTE_PREFIX}{desc} #{}{PASTE_SUFFIX}", *count)
            } else {
                format!("{PASTE_PREFIX}{desc}{PASTE_SUFFIX}")
            };

            self.pending_pastes.insert(placeholder.clone(), text);
            self.insert_str(&placeholder);
        } else {
            self.insert_str(&text);
        }
    }

    /// Expand all paste placeholders in the given buffer, replacing them with actual content.
    pub fn expand_pastes(&self, buffer: &str) -> String {
        let mut result = buffer.to_string();
        for (placeholder, content) in &self.pending_pastes {
            // Replace all occurrences (though normally each placeholder is unique)
            result = result.replace(placeholder.as_str(), content.as_str());
        }
        result
    }

    /// Check if the cursor is inside or at the end of a paste placeholder.
    /// Returns `Some((start, end))` byte range of the placeholder if found.
    pub fn find_placeholder_at_cursor(&self) -> Option<(usize, usize)> {
        // Search backward from cursor for the start of a placeholder
        let before = &self.buffer[..self.cursor];
        // Find the last '[Pasted Text: ' before or at cursor
        if let Some(start_offset) = before.rfind(PASTE_PREFIX) {
            // Find the closing ']' after the prefix
            if let Some(end_rel) = self.buffer[start_offset..].find(PASTE_SUFFIX) {
                let end = start_offset + end_rel + PASTE_SUFFIX.len();
                // Cursor must be within the placeholder range (inclusive of end)
                if self.cursor > start_offset && self.cursor <= end {
                    let candidate = &self.buffer[start_offset..end];
                    if self.pending_pastes.contains_key(candidate) {
                        return Some((start_offset, end));
                    }
                }
            }
        }
        None
    }

    /// Delete backward, but if the cursor is inside/at-end-of a paste placeholder,
    /// delete the entire placeholder atomically.
    pub fn delete_backward_with_paste(&mut self) {
        if self.cursor == 0 {
            return;
        }
        if let Some((start, end)) = self.find_placeholder_at_cursor() {
            let placeholder = self.buffer[start..end].to_string();
            self.pending_pastes.remove(&placeholder);
            self.buffer.replace_range(start..end, "");
            self.cursor = start;
            self.refresh_autocomplete();
        } else {
            self.delete_backward();
        }
    }

    /// Toggle expansion of a paste placeholder at the cursor position (Ctrl+O).
    /// If the cursor is on a placeholder, expand it inline. Returns true if toggled.
    pub fn toggle_paste_expansion(&mut self) -> bool {
        if let Some((start, end)) = self.find_placeholder_at_cursor() {
            let placeholder = self.buffer[start..end].to_string();
            if let Some(content) = self.pending_pastes.remove(&placeholder) {
                self.buffer.replace_range(start..end, &content);
                self.cursor = start + content.len();
                self.refresh_autocomplete();
                return true;
            }
        }
        false
    }

    pub fn history_up(&mut self) {
        if self.history.is_empty() {
            return;
        }

        if self.history_index.is_none() {
            self.saved_input = self.buffer.clone();
            self.history_index = Some(self.history.len().saturating_sub(1));
        } else if let Some(idx) = self.history_index {
            self.history_index = Some(idx.saturating_sub(1));
        }

        if let Some(idx) = self.history_index {
            self.buffer = self.history.get(idx).cloned().unwrap_or_default();
            self.cursor = self.buffer.len();
        }
    }

    pub fn history_down(&mut self) {
        let Some(idx) = self.history_index else {
            return;
        };

        if idx + 1 < self.history.len() {
            self.history_index = Some(idx + 1);
            self.buffer = self.history[idx + 1].clone();
        } else {
            self.history_index = None;
            self.buffer = self.saved_input.clone();
        }
        self.cursor = self.buffer.len();
    }

    pub fn submit(&mut self) -> Option<String> {
        let trimmed = self.buffer.trim().to_string();
        if trimmed.is_empty() {
            return None;
        }

        // Expand paste placeholders before submitting
        let expanded = self.expand_pastes(&trimmed);

        self.history.push(trimmed);
        self.history_index = None;
        self.saved_input.clear();
        self.clear();
        Some(expanded)
    }

    /// Returns true if a slash-triggered autocomplete menu is currently visible.
    pub fn has_slash_autocomplete(&self) -> bool {
        matches!(
            self.autocomplete,
            Some(ref ac) if ac.trigger == AutocompleteTrigger::Slash && !ac.items.is_empty()
        )
    }

    /// Returns true if an @-mention autocomplete menu is currently visible.
    pub fn has_mention_autocomplete(&self) -> bool {
        matches!(
            self.autocomplete,
            Some(ref ac) if ac.trigger == AutocompleteTrigger::AtMention && !ac.items.is_empty()
        )
    }

    /// Add a context attachment and insert the mention text into the buffer.
    pub fn add_attachment(&mut self, attachment: ContextAttachment) {
        self.attachments.push(attachment);
    }

    /// Remove a context attachment by index.
    pub fn remove_attachment(&mut self, index: usize) {
        if index < self.attachments.len() {
            self.attachments.remove(index);
        }
    }

    /// Dismiss the autocomplete menu while preserving the current draft.
    pub fn dismiss_autocomplete(&mut self) {
        self.autocomplete = None;
        self.mention_cache.invalidate();
    }

    /// Move selection to the next autocomplete item.
    pub fn autocomplete_next(&mut self) {
        if let Some(ref mut ac) = self.autocomplete {
            ac.next();
        }
    }

    /// Move selection to the previous autocomplete item.
    pub fn autocomplete_prev(&mut self) {
        if let Some(ref mut ac) = self.autocomplete {
            ac.prev();
        }
    }

    /// Get the value string of the currently selected autocomplete item.
    pub fn autocomplete_selected_value(&self) -> Option<String> {
        self.autocomplete
            .as_ref()
            .and_then(|ac| ac.current())
            .map(|item| item.value.clone())
    }

    fn refresh_autocomplete(&mut self) {
        let before = &self.buffer[..self.cursor];
        let token = before.split_whitespace().last().unwrap_or("");

        let (trigger, query, items) = if let Some(rest) = token.strip_prefix('/') {
            {
                let mut items = vec![
                    AutocompleteItem::new(
                        "btw",
                        "Ask a side question without interrupting the agent",
                    ),
                    AutocompleteItem::new("help", "Show available commands"),
                    AutocompleteItem::new("model", "Switch model"),
                    AutocompleteItem::new("sessions", "Session picker"),
                    AutocompleteItem::new("tools", "List all tools"),
                    AutocompleteItem::new("connect", "Add provider credentials"),
                    AutocompleteItem::new("clear", "Clear chat"),
                    AutocompleteItem::new("compact", "Compact conversation to save context window"),
                    AutocompleteItem::new("think", "Set thinking level"),
                    AutocompleteItem::new("theme", "Switch theme"),
                    AutocompleteItem::new("status", "Show session info"),
                    AutocompleteItem::new("init", "Initialize AVA for this project"),
                    AutocompleteItem::new("diff", "Show git changes"),
                    AutocompleteItem::new(
                        "commit",
                        "Inspect commit readiness and suggest a message",
                    ),
                    AutocompleteItem::new("providers", "Show provider status"),
                    AutocompleteItem::new("disconnect", "Remove provider credentials"),
                    AutocompleteItem::new(
                        "credentials",
                        "Manage provider API keys (list/add/remove)",
                    ),
                    AutocompleteItem::new("tools reload", "Reload tools from disk"),
                    AutocompleteItem::new("tools init", "Create tool templates"),
                    AutocompleteItem::new("mcp", "List MCP servers"),
                    AutocompleteItem::new("mcp reload", "Reload MCP config"),
                    AutocompleteItem::new("bg", "Launch a goal as a background task"),
                    AutocompleteItem::new("tasks", "Show background task list"),
                    AutocompleteItem::new("agents", "Show sub-agent configuration"),
                    AutocompleteItem::new("plan", "Switch to Plan mode"),
                    AutocompleteItem::new("code", "Switch to Code mode"),
                    AutocompleteItem::new("plans", "List plan files"),
                    AutocompleteItem::new("permissions", "Toggle permission level"),
                    AutocompleteItem::new("undo", "Rewind conversation/code to a previous point"),
                    AutocompleteItem::new("export", "Export conversation to file"),
                    AutocompleteItem::new("commands", "List/manage custom commands"),
                    AutocompleteItem::new("hooks", "List/manage lifecycle hooks"),
                    AutocompleteItem::new("hooks reload", "Reload hooks from disk"),
                    AutocompleteItem::new("hooks init", "Create hook templates"),
                    AutocompleteItem::new("hooks dry-run", "Simulate hook execution"),
                    AutocompleteItem::new("later", "Queue a post-complete message (Tier 3)"),
                    AutocompleteItem::new("queue", "Show queued messages"),
                    AutocompleteItem::new("image", "Attach image file (png/jpg/gif/webp)"),
                ];
                // Append custom command items
                items.extend(self.custom_slash_items.clone());
                (AutocompleteTrigger::Slash, rest.to_string(), items)
            }
        } else if let Some(rest) = token.strip_prefix('@') {
            {
                // Strip type prefix if present for the search query
                let (prefix, query) = if let Some(q) = rest.strip_prefix("file:") {
                    ("file:", q)
                } else if let Some(q) = rest.strip_prefix("folder:") {
                    ("folder:", q)
                } else if let Some(q) = rest.strip_prefix("codebase:") {
                    ("codebase:", q)
                } else {
                    ("", rest)
                };

                let mut items = Vec::new();

                if prefix == "codebase:" {
                    // For codebase queries, show a single item prompting the search
                    if !query.is_empty() {
                        items.push(AutocompleteItem::new(
                            format!("codebase:{query}"),
                            "Search codebase".to_string(),
                        ));
                    }
                } else {
                    // Use cached scan results — only re-scans when cwd or mode changes.
                    let folders_only = prefix == "folder:";
                    items = self.mention_cache.get_or_scan(folders_only).to_vec();
                }

                (AutocompleteTrigger::AtMention, query.to_string(), items)
            }
        } else {
            self.autocomplete = None;
            self.mention_cache.invalidate();
            return;
        };

        self.autocomplete = Some(AutocompleteState::new(trigger, query, items));
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn insert_newline_and_cursor_tracking() {
        let mut input = InputState::default();
        input.insert_str("hello");
        input.insert_char('\n');
        input.insert_str("world");

        assert_eq!(input.buffer, "hello\nworld");
        assert_eq!(input.cursor_line_col(), (1, 5)); // line 1, col 5
        assert!(input.is_multiline());
    }

    #[test]
    fn move_up_down_across_lines() {
        let mut input = InputState::default();
        input.insert_str("abc\ndef\nghi");
        // Cursor at end: line 2, col 3
        assert_eq!(input.cursor_line_col(), (2, 3));

        assert!(input.move_up());
        assert_eq!(input.cursor_line_col(), (1, 3));

        assert!(input.move_up());
        assert_eq!(input.cursor_line_col(), (0, 3));

        // Already on first line
        assert!(!input.move_up());

        assert!(input.move_down());
        assert_eq!(input.cursor_line_col(), (1, 3));

        assert!(input.move_down());
        assert_eq!(input.cursor_line_col(), (2, 3));

        // Already on last line
        assert!(!input.move_down());
    }

    #[test]
    fn move_up_clamps_column() {
        let mut input = InputState::default();
        input.insert_str("ab\nlong line\nxy");
        // line 2, col 2
        assert_eq!(input.cursor_line_col(), (2, 2));

        input.move_up(); // line 1, col 2
        assert_eq!(input.cursor_line_col(), (1, 2));

        input.move_end(); // col 9 ("long line")
        assert_eq!(input.cursor_line_col(), (1, 9));

        input.move_up(); // line 0 only has 2 chars, should clamp
        assert_eq!(input.cursor_line_col(), (0, 2));
    }

    #[test]
    fn home_end_within_line() {
        let mut input = InputState::default();
        input.insert_str("first\nsecond\nthird");
        // cursor at end of "third" → line 2, col 5
        input.move_up(); // line 1
        input.move_home();
        assert_eq!(input.cursor_line_col(), (1, 0));

        input.move_end();
        assert_eq!(input.cursor_line_col(), (1, 6)); // "second" = 6 chars
    }

    #[test]
    fn backspace_merges_lines() {
        let mut input = InputState::default();
        input.insert_str("hello\nworld");
        // Move to start of "world" (line 1, col 0)
        input.move_home();
        assert_eq!(input.cursor_line_col(), (1, 0));

        // Backspace should delete the '\n' and merge lines
        input.delete_backward();
        assert_eq!(input.buffer, "helloworld");
        assert_eq!(input.cursor_line_col(), (0, 5));
    }

    #[test]
    fn delete_forward_merges_lines() {
        let mut input = InputState::default();
        input.insert_str("hello\nworld");
        // Move cursor to end of "hello" (just before '\n')
        input.cursor = 5;
        assert_eq!(input.cursor_line_col(), (0, 5));

        input.delete_forward();
        assert_eq!(input.buffer, "helloworld");
    }

    #[test]
    fn single_line_up_down_returns_false() {
        let mut input = InputState::default();
        input.insert_str("hello");
        assert!(!input.move_up());
        assert!(!input.move_down());
    }

    #[test]
    fn submit_preserves_newlines_in_content() {
        let mut input = InputState::default();
        input.insert_str("line1\nline2");
        let submitted = input.submit();
        assert_eq!(submitted, Some("line1\nline2".to_string()));
    }

    // --- Paste collapsing tests ---

    #[test]
    fn paste_small_text_inserts_directly() {
        let mut input = InputState::default();
        input.handle_paste("hello world".to_string());
        assert_eq!(input.buffer, "hello world");
        assert!(input.pending_pastes.is_empty());
    }

    #[test]
    fn paste_below_both_thresholds_inserts_directly() {
        let mut input = InputState::default();
        // 4 lines, short text — below both thresholds
        input.handle_paste("a\nb\nc\nd".to_string());
        assert_eq!(input.buffer, "a\nb\nc\nd");
        assert!(input.pending_pastes.is_empty());
    }

    #[test]
    fn paste_many_lines_collapses() {
        let mut input = InputState::default();
        let text = "line1\nline2\nline3\nline4\nline5\nline6";
        input.handle_paste(text.to_string());
        assert_eq!(input.buffer, "[Pasted Text: 6 lines]");
        assert_eq!(input.pending_pastes.len(), 1);
        assert_eq!(
            input.pending_pastes.get("[Pasted Text: 6 lines]"),
            Some(&text.to_string())
        );
    }

    #[test]
    fn paste_long_single_line_collapses_by_chars() {
        let mut input = InputState::default();
        let text = "x".repeat(600);
        input.handle_paste(text.clone());
        assert_eq!(input.buffer, "[Pasted Text: 600 chars]");
        assert_eq!(input.pending_pastes.len(), 1);
        assert_eq!(
            input.pending_pastes.get("[Pasted Text: 600 chars]"),
            Some(&text)
        );
    }

    #[test]
    fn paste_dedup_numbering() {
        let mut input = InputState::default();
        // Both have 6 lines → same description
        let text1 = "a\nb\nc\nd\ne\nf";
        let text2 = "g\nh\ni\nj\nk\nl";
        input.handle_paste(text1.to_string());
        input.handle_paste(text2.to_string());

        assert!(input.buffer.contains("[Pasted Text: 6 lines]"));
        assert!(input.buffer.contains("[Pasted Text: 6 lines #2]"));
        assert_eq!(input.pending_pastes.len(), 2);
        assert_eq!(
            input.pending_pastes.get("[Pasted Text: 6 lines]"),
            Some(&text1.to_string())
        );
        assert_eq!(
            input.pending_pastes.get("[Pasted Text: 6 lines #2]"),
            Some(&text2.to_string())
        );
    }

    #[test]
    fn submit_expands_pastes() {
        let mut input = InputState::default();
        let text = "line1\nline2\nline3\nline4\nline5";
        input.handle_paste(text.to_string());
        input.insert_str(" and more");

        let submitted = input.submit().unwrap();
        assert!(submitted.contains("line1\nline2\nline3\nline4\nline5"));
        assert!(submitted.contains("and more"));
        // Placeholders should not appear in submitted text
        assert!(!submitted.contains("[Pasted Text:"));
    }

    #[test]
    fn backspace_deletes_placeholder_atomically() {
        let mut input = InputState::default();
        let text = "a\nb\nc\nd\ne\nf";
        input.handle_paste(text.to_string());
        // Cursor is at end of placeholder
        assert!(input.buffer.starts_with("[Pasted Text:"));

        input.delete_backward_with_paste();
        assert_eq!(input.buffer, "");
        assert!(input.pending_pastes.is_empty());
        assert_eq!(input.cursor, 0);
    }

    #[test]
    fn backspace_normal_when_not_on_placeholder() {
        let mut input = InputState::default();
        input.insert_str("hello");
        input.delete_backward_with_paste();
        assert_eq!(input.buffer, "hell");
    }

    #[test]
    fn expand_pastes_in_buffer() {
        let mut input = InputState::default();
        let text = "line1\nline2\nline3\nline4\nline5";
        input.handle_paste(text.to_string());
        let expanded = input.expand_pastes(&input.buffer.clone());
        assert_eq!(expanded, text);
    }

    #[test]
    fn toggle_paste_expansion() {
        let mut input = InputState::default();
        let text = "line1\nline2\nline3\nline4\nline5";
        input.handle_paste(text.to_string());
        assert!(input.buffer.starts_with("[Pasted Text:"));

        let toggled = input.toggle_paste_expansion();
        assert!(toggled);
        assert_eq!(input.buffer, text);
        assert!(input.pending_pastes.is_empty());
    }

    #[test]
    fn clear_resets_paste_state() {
        let mut input = InputState::default();
        input.handle_paste("a\nb\nc\nd\ne\nf".to_string());
        assert!(!input.pending_pastes.is_empty());

        input.clear();
        assert!(input.pending_pastes.is_empty());
        assert!(input.paste_counter.is_empty());
        assert!(input.buffer.is_empty());
    }

    #[test]
    fn paste_with_prefix_text() {
        let mut input = InputState::default();
        input.insert_str("Please review: ");
        let text = "line1\nline2\nline3\nline4\nline5";
        input.handle_paste(text.to_string());

        assert!(input.buffer.starts_with("Please review: [Pasted Text:"));
        let submitted = input.submit().unwrap();
        assert!(submitted.starts_with("Please review: line1\nline2"));
    }

    // --- @-mention / attachment tests ---

    #[test]
    fn at_triggers_mention_autocomplete() {
        let mut input = InputState::default();
        input.insert_char('@');
        // Should trigger AtMention autocomplete (items may be empty if no files match)
        assert!(matches!(
            input.autocomplete,
            Some(ref ac) if ac.trigger == AutocompleteTrigger::AtMention
        ));
    }

    #[test]
    fn add_and_remove_attachment() {
        let mut input = InputState::default();
        let attachment = ava_types::ContextAttachment::File {
            path: std::path::PathBuf::from("src/main.rs"),
        };
        input.add_attachment(attachment.clone());
        assert_eq!(input.attachments.len(), 1);
        assert_eq!(input.attachments[0], attachment);

        input.remove_attachment(0);
        assert!(input.attachments.is_empty());
    }

    #[test]
    fn clear_resets_attachments() {
        let mut input = InputState::default();
        input.add_attachment(ava_types::ContextAttachment::File {
            path: std::path::PathBuf::from("test.rs"),
        });
        assert!(!input.attachments.is_empty());
        input.clear();
        assert!(input.attachments.is_empty());
    }

    #[test]
    fn has_mention_autocomplete_when_at_typed() {
        let mut input = InputState::default();
        input.insert_char('@');
        // Even if no files match the empty query, the autocomplete should be AtMention type
        let is_at = matches!(
            input.autocomplete,
            Some(ref ac) if ac.trigger == AutocompleteTrigger::AtMention
        );
        assert!(is_at);
    }

    #[test]
    fn submit_clears_attachments() {
        let mut input = InputState::default();
        input.insert_str("hello");
        input.add_attachment(ava_types::ContextAttachment::File {
            path: std::path::PathBuf::from("test.rs"),
        });
        let _ = input.submit();
        assert!(input.attachments.is_empty());
    }

    // --- Mention file cache tests ---

    #[test]
    fn mention_cache_populated_on_first_at() {
        let mut input = InputState::default();
        input.insert_char('@');
        // After the first @, the cache should be populated (cwd set).
        assert!(
            input.mention_cache.cwd.is_some(),
            "cache cwd should be set after first @ trigger"
        );
    }

    #[test]
    fn mention_cache_reused_on_subsequent_keystrokes() {
        let mut input = InputState::default();
        input.insert_char('@');
        // Snapshot the cache state after first scan
        let cwd_after_first = input.mention_cache.cwd.clone();
        let items_len_first = input.mention_cache.items.len();

        // Type more characters — should reuse cache, not rescan
        input.insert_char('s');
        assert_eq!(
            input.mention_cache.cwd, cwd_after_first,
            "cache cwd should not change on refinement keystroke"
        );
        assert_eq!(
            input.mention_cache.items.len(),
            items_len_first,
            "cache item count should stay the same (filter is in AutocompleteState, not cache)"
        );

        input.insert_char('r');
        assert_eq!(input.mention_cache.cwd, cwd_after_first);
        assert_eq!(input.mention_cache.items.len(), items_len_first);
    }

    #[test]
    fn mention_cache_invalidated_on_clear() {
        let mut input = InputState::default();
        input.insert_char('@');
        assert!(input.mention_cache.cwd.is_some());

        input.clear();
        assert!(
            input.mention_cache.cwd.is_none(),
            "cache should be invalidated after clear()"
        );
        assert!(input.mention_cache.items.is_empty());
    }

    #[test]
    fn mention_cache_invalidated_when_leaving_at_context() {
        let mut input = InputState::default();
        input.insert_char('@');
        assert!(input.mention_cache.cwd.is_some());

        // Simulate clearing buffer and typing a non-@ token
        input.buffer.clear();
        input.cursor = 0;
        input.insert_str("hello");
        // After typing a plain word, the else branch fires and invalidates cache
        assert!(
            input.mention_cache.cwd.is_none(),
            "cache should be invalidated when no longer in @ context"
        );
    }

    #[test]
    fn mention_cache_invalidated_on_dismiss() {
        let mut input = InputState::default();
        input.insert_char('@');
        assert!(input.mention_cache.cwd.is_some());

        input.dismiss_autocomplete();
        assert!(
            input.mention_cache.cwd.is_none(),
            "cache should be invalidated on dismiss_autocomplete"
        );
    }

    #[test]
    fn dismiss_autocomplete_preserves_input_buffer() {
        let mut input = InputState::default();
        for ch in "/hel".chars() {
            input.insert_char(ch);
        }
        assert!(input.has_slash_autocomplete());

        input.dismiss_autocomplete();

        assert_eq!(input.buffer, "/hel");
        assert_eq!(input.cursor, 4);
        assert!(input.autocomplete.is_none());
    }

    #[test]
    fn mention_cache_respects_folders_only_mode_switch() {
        let mut input = InputState::default();

        // Start with general @ (folders_only = false)
        input.insert_char('@');
        assert!(!input.mention_cache.folders_only);
        let items_general = input.mention_cache.items.len();

        // Clear and switch to folder: prefix
        input.buffer.clear();
        input.cursor = 0;
        input.mention_cache.invalidate();
        input.insert_str("@folder:");
        assert!(
            input.mention_cache.folders_only,
            "cache should reflect folders_only = true for @folder: prefix"
        );
        // The folder-only scan should have <= items compared to general
        assert!(input.mention_cache.items.len() <= items_general);
    }

    #[test]
    fn mention_cache_query_filtering_preserves_full_cache() {
        let mut input = InputState::default();
        input.insert_char('@');
        let full_cache_len = input.mention_cache.items.len();

        // Type a query that likely filters down the visible items
        input.insert_str("zzz_unlikely_match");

        // The cache itself should still hold all items (filtering is in AutocompleteState)
        assert_eq!(
            input.mention_cache.items.len(),
            full_cache_len,
            "cache should hold all items; filtering happens in AutocompleteState"
        );

        // But the autocomplete visible items should be filtered (possibly empty)
        if let Some(ref ac) = input.autocomplete {
            assert!(
                ac.items.len() <= full_cache_len,
                "autocomplete items should be filtered subset of cache"
            );
        }
    }

    #[test]
    fn mention_cache_codebase_prefix_does_not_use_cache() {
        let mut input = InputState::default();
        input.insert_str("@codebase:query");

        // codebase: prefix should not populate the file cache
        // (it uses a synthetic single item, not file scanning)
        assert!(
            input.mention_cache.cwd.is_none(),
            "codebase: queries should not populate the file scan cache"
        );
    }

    #[test]
    fn mention_cache_survives_backspace_within_at_session() {
        let mut input = InputState::default();
        input.insert_str("@src");
        let cwd_snapshot = input.mention_cache.cwd.clone();
        let cache_len = input.mention_cache.items.len();
        assert!(cwd_snapshot.is_some());

        // Backspace one char — still in @ context
        input.delete_backward();
        assert_eq!(input.buffer, "@sr");
        assert_eq!(
            input.mention_cache.cwd, cwd_snapshot,
            "cache should survive backspace within @ session"
        );
        assert_eq!(input.mention_cache.items.len(), cache_len);

        // Backspace again
        input.delete_backward();
        assert_eq!(input.buffer, "@s");
        assert_eq!(input.mention_cache.cwd, cwd_snapshot);

        // Backspace to just "@"
        input.delete_backward();
        assert_eq!(input.buffer, "@");
        assert_eq!(input.mention_cache.cwd, cwd_snapshot);
    }

    #[test]
    fn mention_cache_invalidated_when_at_deleted() {
        let mut input = InputState::default();
        input.insert_char('@');
        assert!(input.mention_cache.cwd.is_some());

        // Delete the @ itself
        input.delete_backward();
        assert_eq!(input.buffer, "");
        assert!(
            input.mention_cache.cwd.is_none(),
            "cache should be invalidated when @ is deleted"
        );
    }

    #[test]
    fn slash_autocomplete_does_not_populate_mention_cache() {
        let mut input = InputState::default();
        input.insert_char('/');
        assert!(
            input.mention_cache.cwd.is_none(),
            "slash commands should not populate mention file cache"
        );
        assert!(input.mention_cache.items.is_empty());
    }
}
