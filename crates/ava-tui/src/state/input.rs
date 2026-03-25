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
                        "Start a side conversation branch (like git stash for chat)",
                    ),
                    AutocompleteItem::new("help", "Show available commands"),
                    AutocompleteItem::new("model", "Switch model"),
                    AutocompleteItem::new("sessions", "Session picker"),
                    AutocompleteItem::new("connect", "Add provider credentials"),
                    AutocompleteItem::new("clear", "Clear chat"),
                    AutocompleteItem::new("compact", "Compact conversation to save context window"),
                    AutocompleteItem::new("think", "Toggle thinking visibility"),
                    AutocompleteItem::new("theme", "Switch theme"),
                    AutocompleteItem::new("new", "Start a new session"),
                    AutocompleteItem::new("models", "Switch model (alias)"),
                    AutocompleteItem::new(
                        "commit",
                        "Inspect commit readiness and suggest a message",
                    ),
                    AutocompleteItem::new("providers", "Show provider status"),
                    AutocompleteItem::new("disconnect", "Remove provider credentials"),
                    AutocompleteItem::new("mcp", "List MCP servers"),
                    AutocompleteItem::new("mcp reload", "Reload MCP config"),
                    AutocompleteItem::new("tasks", "Show background task list"),
                    AutocompleteItem::new("permissions", "Toggle permission level"),
                    AutocompleteItem::new("copy", "Copy last response to clipboard"),
                    AutocompleteItem::new("export", "Export conversation to file"),
                    AutocompleteItem::new("hooks", "List/manage lifecycle hooks"),
                    AutocompleteItem::new("hooks reload", "Reload hooks from disk"),
                    AutocompleteItem::new("hooks dry-run", "Simulate hook execution"),
                    AutocompleteItem::new("later", "Queue a post-complete message (Tier 3)"),
                    AutocompleteItem::new("queue", "Show queued messages"),
                    AutocompleteItem::new("shortcuts", "Show keyboard shortcuts (Ctrl+?)"),
                    AutocompleteItem::new("init", "Create project configuration files"),
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
mod tests;
