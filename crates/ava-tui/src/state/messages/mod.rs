mod rendering;
mod spinner;
mod types;
mod wrapping;

pub use spinner::{inline_spinner_frame, spinner_frame, INLINE_SPINNER_FRAMES, SPINNER_FRAMES};
pub use types::{MessageKind, SubAgentData, UiMessage};

#[derive(Debug)]
pub struct MessageState {
    pub messages: Vec<UiMessage>,
    pub scroll_offset: u16,
    pub auto_scroll: bool,
    pub unseen_count: usize,
    pub show_tools_expanded: bool,
    /// Set by the renderer each frame.
    pub total_lines: u16,
    /// Set by the renderer each frame (content area height minus borders).
    pub visible_height: u16,
    /// Tick counter for spinner animation.
    pub spinner_tick: usize,
    /// Set by the renderer each frame — the area where messages are drawn.
    /// Used for mouse click hit-testing.
    pub messages_area: ratatui::layout::Rect,
    /// Set by the renderer each frame — maps each source message index to
    /// its `(start_line, end_line)` in the total line buffer (exclusive end).
    /// Used for mapping click positions to message indices.
    pub message_line_ranges: Vec<(u16, u16)>,
}

impl Default for MessageState {
    fn default() -> Self {
        Self {
            messages: Vec::new(),
            scroll_offset: 0,
            auto_scroll: true,
            unseen_count: 0,
            show_tools_expanded: false,
            total_lines: 0,
            visible_height: 0,
            spinner_tick: 0,
            messages_area: ratatui::layout::Rect::default(),
            message_line_ranges: Vec::new(),
        }
    }
}

impl MessageState {
    pub fn push(&mut self, message: UiMessage) {
        // Remove transient messages when the user sends a new message.
        if matches!(message.kind, MessageKind::User) {
            self.messages.retain(|m| !m.transient);
        }
        self.messages.push(message);
        if !self.auto_scroll {
            self.unseen_count += 1;
        }
        // auto_scroll offset is computed in render_message_list each frame
    }

    pub fn advance_spinner(&mut self) {
        self.spinner_tick = self.spinner_tick.wrapping_add(1);
    }

    pub fn scroll_up(&mut self, by: u16) {
        self.auto_scroll = false;
        self.scroll_offset = self.scroll_offset.saturating_sub(by);
    }

    pub fn scroll_down(&mut self, by: u16) {
        self.scroll_offset = self.scroll_offset.saturating_add(by);
        let bottom = self.total_lines.saturating_sub(self.visible_height);
        if self.scroll_offset >= bottom {
            self.scroll_offset = bottom;
            self.auto_scroll = true;
            self.unseen_count = 0;
        }
    }

    pub fn scroll_to_top(&mut self) {
        self.auto_scroll = false;
        self.scroll_offset = 0;
    }

    pub fn scroll_to_bottom(&mut self) {
        self.auto_scroll = true;
        self.scroll_offset = self.total_lines.saturating_sub(self.visible_height);
        self.unseen_count = 0;
    }

    /// Toggle the expanded/collapsed state of a specific thinking message.
    pub fn toggle_thinking_at(&mut self, message_index: usize) {
        if let Some(msg) = self.messages.get_mut(message_index) {
            if matches!(msg.kind, MessageKind::Thinking) {
                msg.thinking_expanded = !msg.thinking_expanded;
            }
        }
    }

    /// Toggle the expanded/collapsed state of a tool action group.
    /// Finds the consecutive group of ToolCall/ToolResult messages surrounding
    /// the given index and flips `tool_group_expanded` on all of them.
    pub fn toggle_tool_group_at(&mut self, message_index: usize) {
        if let Some(msg) = self.messages.get(message_index) {
            if !matches!(msg.kind, MessageKind::ToolCall | MessageKind::ToolResult) {
                return;
            }
        } else {
            return;
        }

        // Walk backwards to find group start.
        let mut start = message_index;
        while start > 0 {
            if matches!(
                self.messages[start - 1].kind,
                MessageKind::ToolCall | MessageKind::ToolResult
            ) {
                start -= 1;
            } else {
                break;
            }
        }
        // Walk forwards to find group end (exclusive).
        let mut end = message_index + 1;
        while end < self.messages.len() {
            if matches!(
                self.messages[end].kind,
                MessageKind::ToolCall | MessageKind::ToolResult
            ) {
                end += 1;
            } else {
                break;
            }
        }

        // Determine new state: if any in the group is collapsed, expand all.
        let any_collapsed = self.messages[start..end]
            .iter()
            .any(|m| !m.tool_group_expanded);
        for msg in &mut self.messages[start..end] {
            msg.tool_group_expanded = any_collapsed;
        }
    }

    /// Toggle all thinking blocks between expanded and collapsed.
    /// If any are collapsed, expand all; otherwise collapse all.
    pub fn toggle_all_thinking(&mut self) {
        let any_collapsed = self
            .messages
            .iter()
            .any(|m| matches!(m.kind, MessageKind::Thinking) && !m.thinking_expanded);
        let new_state = any_collapsed; // expand all if any collapsed, else collapse all
        for msg in &mut self.messages {
            if matches!(msg.kind, MessageKind::Thinking) {
                msg.thinking_expanded = new_state;
            }
        }
    }

    /// Toggle all tool action groups between expanded and collapsed.
    /// If any are collapsed, expand all; otherwise collapse all.
    pub fn toggle_all_tool_groups(&mut self) {
        let any_collapsed = self.messages.iter().any(|m| {
            matches!(m.kind, MessageKind::ToolCall | MessageKind::ToolResult)
                && !m.tool_group_expanded
        });
        let new_state = any_collapsed;
        for msg in &mut self.messages {
            if matches!(msg.kind, MessageKind::ToolCall | MessageKind::ToolResult) {
                msg.tool_group_expanded = new_state;
            }
        }
    }

    /// Given an absolute screen row, return the source message index if it
    /// falls inside the messages area. Uses `messages_area`, `scroll_offset`,
    /// and `message_line_ranges` (all set by the renderer each frame).
    pub fn message_index_at_row(&self, row: u16) -> Option<usize> {
        let area = self.messages_area;
        if row < area.y || row >= area.y + area.height {
            return None;
        }
        let visual_row = row - area.y;
        let absolute_line = visual_row + self.scroll_offset;
        for (i, &(start, end)) in self.message_line_ranges.iter().enumerate() {
            if absolute_line >= start && absolute_line < end {
                return Some(i);
            }
        }
        None
    }

    /// Returns the content of the last assistant message, if any.
    pub fn last_assistant_content(&self) -> Option<&str> {
        self.messages
            .iter()
            .rev()
            .find(|m| matches!(m.kind, MessageKind::Assistant))
            .map(|m| m.content.as_str())
    }

    /// Reset scroll state (e.g. when switching sessions).
    pub fn reset_scroll(&mut self) {
        self.scroll_offset = 0;
        self.auto_scroll = true;
        self.unseen_count = 0;
        self.total_lines = 0;
    }
}
