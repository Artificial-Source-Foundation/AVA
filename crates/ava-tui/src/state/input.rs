use crate::widgets::autocomplete::{AutocompleteItem, AutocompleteState, AutocompleteTrigger};

#[derive(Debug, Default)]
pub struct InputState {
    pub buffer: String,
    pub cursor: usize,
    pub history: Vec<String>,
    pub history_index: Option<usize>,
    pub saved_input: String,
    pub autocomplete: Option<AutocompleteState>,
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

        self.history.push(trimmed.clone());
        self.history_index = None;
        self.saved_input.clear();
        self.clear();
        Some(trimmed)
    }

    /// Returns true if a slash-triggered autocomplete menu is currently visible.
    pub fn has_slash_autocomplete(&self) -> bool {
        matches!(
            self.autocomplete,
            Some(ref ac) if ac.trigger == AutocompleteTrigger::Slash && !ac.items.is_empty()
        )
    }

    /// Dismiss the autocomplete menu and clear the input buffer.
    pub fn dismiss_autocomplete(&mut self) {
        self.autocomplete = None;
        self.clear();
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
            (
                AutocompleteTrigger::Slash,
                rest.to_string(),
                vec![
                    AutocompleteItem::new("help", "Show available commands"),
                    AutocompleteItem::new("model", "Switch model"),
                    AutocompleteItem::new("sessions", "Session picker"),
                    AutocompleteItem::new("tools", "List all tools"),
                    AutocompleteItem::new("connect", "Add provider credentials"),
                    AutocompleteItem::new("clear", "Clear chat"),
                    AutocompleteItem::new("compact", "Show context usage"),
                    AutocompleteItem::new("think", "Set thinking level"),
                    AutocompleteItem::new("theme", "Switch theme"),
                    AutocompleteItem::new("status", "Show session info"),
                    AutocompleteItem::new("diff", "Show git changes"),
                    AutocompleteItem::new("commit", "Show git status for committing"),
                    AutocompleteItem::new("providers", "Show provider status"),
                    AutocompleteItem::new("disconnect", "Remove provider credentials"),
                    AutocompleteItem::new("tools reload", "Reload tools from disk"),
                    AutocompleteItem::new("tools init", "Create tool templates"),
                    AutocompleteItem::new("mcp", "List MCP servers"),
                    AutocompleteItem::new("mcp reload", "Reload MCP config"),
                    AutocompleteItem::new("permissions", "Toggle permission level"),
                ],
            )
        } else if let Some(rest) = token.strip_prefix('@') {
            (
                AutocompleteTrigger::AtMention,
                rest.to_string(),
                vec![
                    AutocompleteItem::new("README.md", "Include file in context"),
                    AutocompleteItem::new("AGENTS.md", "Include file in context"),
                ],
            )
        } else {
            self.autocomplete = None;
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
}
