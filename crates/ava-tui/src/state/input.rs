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
                    AutocompleteItem::new("compact", "Compact context"),
                    AutocompleteItem::new("think", "Set thinking level"),
                    AutocompleteItem::new("status", "Show session info"),
                    AutocompleteItem::new("diff", "Show git changes"),
                    AutocompleteItem::new("providers", "Show provider status"),
                    AutocompleteItem::new("disconnect", "Remove provider credentials"),
                    AutocompleteItem::new("tools reload", "Reload tools from disk"),
                    AutocompleteItem::new("tools init", "Create tool templates"),
                    AutocompleteItem::new("mcp", "List MCP servers"),
                    AutocompleteItem::new("mcp reload", "Reload MCP config"),
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
