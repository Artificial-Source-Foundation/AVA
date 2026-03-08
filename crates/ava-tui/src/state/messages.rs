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

/// Braille spinner frames for streaming/working animation.
pub const SPINNER_FRAMES: &[&str] = &[
    "\u{280b}", "\u{2819}", "\u{2839}", "\u{2838}", "\u{283c}", "\u{2834}", "\u{2826}",
    "\u{2827}", "\u{2807}", "\u{280f}",
];

/// Returns the current spinner frame based on a tick counter.
pub fn spinner_frame(tick: usize) -> &'static str {
    SPINNER_FRAMES[tick % SPINNER_FRAMES.len()]
}

impl UiMessage {
    pub fn new(kind: MessageKind, content: impl Into<String>) -> Self {
        Self {
            kind,
            content: content.into(),
            is_streaming: false,
        }
    }

    pub fn to_lines(&self, theme: &Theme, spinner_tick: usize) -> Vec<Line<'static>> {
        let mut lines = match self.kind {
            MessageKind::Assistant => markdown_to_lines(&self.content, theme),
            MessageKind::User => {
                vec![Line::from(vec![
                    Span::styled(
                        "You: ",
                        Style::default()
                            .fg(theme.text_muted)
                            .add_modifier(Modifier::BOLD),
                    ),
                    Span::styled(self.content.clone(), Style::default().fg(theme.text)),
                ])]
            }
            MessageKind::ToolCall => {
                vec![Line::from(vec![
                    Span::styled(
                        "$ ",
                        Style::default().fg(theme.text_muted),
                    ),
                    Span::styled(
                        self.content.clone(),
                        Style::default().fg(theme.text),
                    ),
                ])]
            }
            MessageKind::ToolResult => {
                // Truncate to max 5 lines, dimmed with └ prefix
                let content_lines: Vec<&str> = self.content.lines().collect();
                let truncated = content_lines.len() > 5;
                let total = content_lines.len();
                let display_lines = if truncated {
                    &content_lines[..5]
                } else {
                    &content_lines[..]
                };

                let mut result = Vec::new();
                for (i, line) in display_lines.iter().enumerate() {
                    let prefix = if i == 0 { "\u{2514} " } else { "  " };
                    result.push(Line::from(Span::styled(
                        format!("{prefix}{line}"),
                        Style::default()
                            .fg(theme.text_dimmed)
                            .add_modifier(Modifier::DIM),
                    )));
                }
                if truncated {
                    result.push(Line::from(Span::styled(
                        format!("  ... ({} more lines)", total - 5),
                        Style::default()
                            .fg(theme.text_dimmed)
                            .add_modifier(Modifier::DIM),
                    )));
                }
                result
            }
            MessageKind::Thinking => {
                vec![Line::from(Span::styled(
                    format!("  {}", self.content),
                    Style::default()
                        .fg(theme.text_dimmed)
                        .add_modifier(Modifier::DIM | Modifier::ITALIC),
                ))]
            }
            MessageKind::Error => vec![Line::from(Span::styled(
                format!("\u{2717} {}", self.content),
                Style::default().fg(theme.error),
            ))],
            MessageKind::System => vec![Line::from(Span::styled(
                self.content.clone(),
                Style::default()
                    .fg(theme.text_dimmed)
                    .add_modifier(Modifier::DIM),
            ))],
        };

        // Show streaming indicator
        if self.is_streaming {
            if let Some(last) = lines.last_mut() {
                let frame = spinner_frame(spinner_tick);
                last.spans.push(Span::styled(
                    format!(" {frame}"),
                    Style::default().fg(theme.accent),
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
    /// Tick counter for spinner animation.
    pub spinner_tick: usize,
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
            spinner_tick: 0,
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

    /// Reset scroll state (e.g. when switching sessions).
    pub fn reset_scroll(&mut self) {
        self.scroll_offset = 0;
        self.auto_scroll = true;
        self.unseen_count = 0;
        self.total_lines = 0;
    }
}
