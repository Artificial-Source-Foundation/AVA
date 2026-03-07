use crate::rendering::markdown::markdown_to_lines;
use crate::state::theme::Theme;
use ratatui::style::{Modifier, Style};
use ratatui::text::{Line, Span};

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
    pub is_streaming: bool,
}

impl UiMessage {
    pub fn new(kind: MessageKind, content: impl Into<String>) -> Self {
        Self {
            kind,
            content: content.into(),
            is_streaming: false,
        }
    }

    pub fn to_lines(&self, theme: &Theme) -> Vec<Line<'static>> {
        let mut lines = match self.kind {
            MessageKind::Assistant => markdown_to_lines(&self.content, theme),
            MessageKind::User => vec![Line::from(Span::styled(
                format!("❯ {}", self.content),
                Style::default().fg(theme.primary),
            ))],
            MessageKind::ToolCall => vec![Line::from(vec![
                Span::styled("⚙ ", Style::default().fg(theme.accent)),
                Span::styled(self.content.clone(), Style::default().fg(theme.text_muted)),
            ])],
            MessageKind::ToolResult => vec![Line::from(vec![
                Span::styled("← ", Style::default().fg(theme.secondary)),
                Span::styled(self.content.clone(), Style::default().fg(theme.text_muted)),
            ])],
            MessageKind::Thinking => vec![Line::from(Span::styled(
                format!("💭 {}", self.content),
                Style::default().fg(theme.text_muted),
            ))],
            MessageKind::Error => vec![Line::from(Span::styled(
                format!("✗ {}", self.content),
                Style::default().fg(theme.error),
            ))],
            MessageKind::System => vec![Line::from(Span::styled(
                format!("• {}", self.content),
                Style::default().fg(theme.text_muted),
            ))],
        };
        // Show streaming cursor
        if self.is_streaming {
            if let Some(last) = lines.last_mut() {
                last.spans.push(Span::styled(
                    " █",
                    Style::default()
                        .fg(theme.accent)
                        .add_modifier(Modifier::SLOW_BLINK),
                ));
            }
        }
        // Add blank line after each message for spacing
        lines.push(Line::raw(""));
        lines
    }
}

#[derive(Debug)]
pub struct MessageState {
    pub messages: Vec<UiMessage>,
    pub scroll_offset: u16,
    pub auto_scroll: bool,
    pub unseen_count: usize,
    /// Set by the renderer each frame.
    pub total_lines: u16,
    /// Set by the renderer each frame (content area height minus borders).
    pub visible_height: u16,
}

impl Default for MessageState {
    fn default() -> Self {
        Self {
            messages: Vec::new(),
            scroll_offset: 0,
            auto_scroll: true,
            unseen_count: 0,
            total_lines: 0,
            visible_height: 0,
        }
    }
}

impl MessageState {
    pub fn push(&mut self, message: UiMessage) {
        self.messages.push(message);
        if !self.auto_scroll {
            self.unseen_count += 1;
        }
        // auto_scroll offset is computed in render_message_list each frame
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

    /// Reset scroll state (e.g. when switching sessions).
    pub fn reset_scroll(&mut self) {
        self.scroll_offset = 0;
        self.auto_scroll = true;
        self.unseen_count = 0;
        self.total_lines = 0;
    }
}
