use crate::rendering::markdown::markdown_to_lines;
use crate::state::theme::Theme;
use ratatui::text::Line;

#[derive(Debug, Clone)]
pub enum MessageKind {
    User,
    Assistant,
    ToolCall,
    ToolResult,
    Thinking,
    Error,
    System,
}

#[derive(Debug, Clone)]
pub struct UiMessage {
    pub kind: MessageKind,
    pub content: String,
}

impl UiMessage {
    pub fn new(kind: MessageKind, content: impl Into<String>) -> Self {
        Self {
            kind,
            content: content.into(),
        }
    }

    pub fn to_lines(&self, theme: &Theme) -> Vec<Line<'static>> {
        match self.kind {
            MessageKind::Assistant => markdown_to_lines(&self.content, theme),
            MessageKind::User => vec![Line::raw(format!("> {}", self.content))],
            MessageKind::ToolCall => vec![Line::raw(format!("[tool] {}", self.content))],
            MessageKind::ToolResult => vec![Line::raw(format!("[result] {}", self.content))],
            MessageKind::Thinking => vec![Line::raw(format!("[thinking] {}", self.content))],
            MessageKind::Error => vec![Line::raw(format!("[error] {}", self.content))],
            MessageKind::System => vec![Line::raw(format!("[system] {}", self.content))],
        }
    }
}

#[derive(Debug, Default)]
pub struct MessageState {
    pub messages: Vec<UiMessage>,
    pub scroll_offset: u16,
    pub auto_scroll: bool,
    pub unseen_count: usize,
}

impl MessageState {
    pub fn push(&mut self, message: UiMessage) {
        self.messages.push(message);
        if self.auto_scroll {
            self.scroll_to_bottom();
        } else {
            self.unseen_count += 1;
        }
    }

    pub fn scroll_up(&mut self, by: u16) {
        self.auto_scroll = false;
        self.scroll_offset = self.scroll_offset.saturating_add(by);
    }

    pub fn scroll_down(&mut self, by: u16) {
        self.scroll_offset = self.scroll_offset.saturating_sub(by);
        if self.scroll_offset == 0 {
            self.auto_scroll = true;
            self.unseen_count = 0;
        }
    }

    pub fn scroll_to_top(&mut self) {
        self.auto_scroll = false;
        self.scroll_offset = u16::MAX;
    }

    pub fn scroll_to_bottom(&mut self) {
        self.auto_scroll = true;
        self.scroll_offset = 0;
        self.unseen_count = 0;
    }
}
