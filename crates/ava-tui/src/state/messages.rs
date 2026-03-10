use crate::rendering::markdown::markdown_to_lines;
use crate::state::theme::Theme;
use ratatui::layout::Alignment;
use ratatui::style::{Modifier, Style};
use ratatui::text::{Line, Span};
use unicode_width::UnicodeWidthStr;

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
    /// Model name for assistant messages (shown as metadata).
    pub model_name: Option<String>,
    /// Response time in seconds for assistant messages.
    pub response_time: Option<f64>,
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

/// Left-border character — full block for visual weight (design: 3px bar).
const LEFT_BAR: &str = "\u{258E}";
/// Padding after the left bar (design: content padding 16px ≈ 2 chars).
const BAR_PAD: &str = "  ";

impl UiMessage {
    pub fn new(kind: MessageKind, content: impl Into<String>) -> Self {
        Self {
            kind,
            content: content.into(),
            is_streaming: false,
            model_name: None,
            response_time: None,
        }
    }

    /// Returns the left-bar color for this message kind.
    fn bar_color(&self, theme: &Theme) -> ratatui::style::Color {
        match self.kind {
            MessageKind::User => theme.secondary,
            MessageKind::Assistant => theme.primary,
            MessageKind::ToolCall | MessageKind::ToolResult => theme.text_dimmed,
            MessageKind::Thinking => theme.primary,
            MessageKind::Error => theme.error,
            MessageKind::System => theme.text_dimmed,
        }
    }

    /// Width of the bar prefix (LEFT_BAR + BAR_PAD) in columns.
    const BAR_PREFIX_WIDTH: u16 = 3; // "▎" (1) + "  " (2)

    /// Prepend a colored left bar + padding to each line, then manually
    /// wrap so that every visual row carries its own bar prefix.
    fn prepend_bars(
        lines: &mut Vec<Line<'static>>,
        color: ratatui::style::Color,
        width: u16,
    ) {
        let content_width = if width > Self::BAR_PREFIX_WIDTH {
            (width - Self::BAR_PREFIX_WIDTH) as usize
        } else {
            // Terminal too narrow — just prepend bar, skip wrapping.
            for line in lines.iter_mut() {
                let bar = Span::styled(LEFT_BAR, Style::default().fg(color));
                let space = Span::raw(BAR_PAD);
                let mut new_spans = vec![bar, space];
                new_spans.append(&mut line.spans);
                line.spans = new_spans;
            }
            return;
        };

        let original = std::mem::take(lines);
        for line in original {
            let wrapped = Self::wrap_line_spans(line.spans, content_width);
            for sub_spans in wrapped {
                let bar = Span::styled(LEFT_BAR, Style::default().fg(color));
                let space = Span::raw(BAR_PAD);
                let mut new_spans = vec![bar, space];
                new_spans.extend(sub_spans);
                lines.push(Line::from(new_spans));
            }
        }
    }

    /// Break a sequence of spans into multiple rows, each fitting within
    /// `max_width` display columns. Splits on word boundaries when possible,
    /// falling back to character-level splits for long words.
    fn wrap_line_spans(
        spans: Vec<Span<'static>>,
        max_width: usize,
    ) -> Vec<Vec<Span<'static>>> {
        if max_width == 0 {
            return vec![spans];
        }

        // Flatten spans into styled segments we can split.
        struct Segment {
            text: String,
            style: Style,
        }
        let segments: Vec<Segment> = spans
            .into_iter()
            .map(|s| Segment {
                text: s.content.into_owned(),
                style: s.style,
            })
            .collect();

        // Check total width — fast path if no wrapping needed.
        let total_width: usize = segments.iter().map(|s| s.text.width()).sum();
        if total_width <= max_width {
            return vec![segments
                .into_iter()
                .map(|s| Span::styled(s.text, s.style))
                .collect()];
        }

        // Wrap by walking through characters.
        let mut rows: Vec<Vec<Span<'static>>> = Vec::new();
        let mut current_row: Vec<Span<'static>> = Vec::new();
        let mut current_width: usize = 0;

        for seg in segments {
            let style = seg.style;
            let text = seg.text;

            if text.is_empty() {
                current_row.push(Span::styled(String::new(), style));
                continue;
            }

            let mut remaining = text.as_str();
            while !remaining.is_empty() {
                let avail = max_width.saturating_sub(current_width);
                if avail == 0 {
                    rows.push(std::mem::take(&mut current_row));
                    current_width = 0;
                    continue;
                }

                let rem_width = remaining.width();
                if rem_width <= avail {
                    current_row.push(Span::styled(remaining.to_owned(), style));
                    current_width += rem_width;
                    break;
                }

                // Need to split — find a break point at `avail` columns.
                let break_at = Self::find_break_point(remaining, avail);
                if break_at == 0 {
                    // Can't fit even one char — flush row first.
                    if !current_row.is_empty() {
                        rows.push(std::mem::take(&mut current_row));
                        current_width = 0;
                    } else {
                        // Force at least one char to avoid infinite loop.
                        let ch = remaining.chars().next().unwrap();
                        let clen = ch.len_utf8();
                        current_row.push(Span::styled(
                            remaining[..clen].to_owned(),
                            style,
                        ));
                        remaining = &remaining[clen..];
                        rows.push(std::mem::take(&mut current_row));
                        current_width = 0;
                    }
                    continue;
                }

                let chunk = &remaining[..break_at];
                current_row.push(Span::styled(chunk.to_owned(), style));
                remaining = &remaining[break_at..];

                // Flush this row.
                rows.push(std::mem::take(&mut current_row));
                current_width = 0;

                // Skip leading space on next line (word-wrap behavior).
                if remaining.starts_with(' ') {
                    remaining = &remaining[1..];
                }
            }
        }

        if !current_row.is_empty() {
            rows.push(current_row);
        }
        if rows.is_empty() {
            rows.push(Vec::new());
        }
        rows
    }

    /// Find the byte offset to break `text` at, targeting `max_cols` display
    /// columns. Prefers breaking at the last space; falls back to exact column
    /// boundary.
    fn find_break_point(text: &str, max_cols: usize) -> usize {
        let mut col = 0usize;
        let mut last_space_byte = None;
        let mut byte_at_max = 0usize;

        for (i, ch) in text.char_indices() {
            let w = unicode_width::UnicodeWidthChar::width(ch).unwrap_or(0);
            if col + w > max_cols {
                byte_at_max = i;
                break;
            }
            if ch == ' ' {
                last_space_byte = Some(i + 1); // break after the space
            }
            col += w;
            byte_at_max = i + ch.len_utf8();
        }

        // If we consumed the entire string without exceeding, return full length.
        if col <= max_cols {
            return byte_at_max;
        }

        // Prefer word boundary.
        if let Some(sp) = last_space_byte {
            if sp > 0 {
                return sp;
            }
        }
        byte_at_max
    }

    pub fn to_lines(&self, theme: &Theme, spinner_tick: usize, width: u16) -> Vec<Line<'static>> {
        let bar_color = self.bar_color(theme);

        let mut lines = match self.kind {
            MessageKind::Assistant => {
                let mut content_lines = markdown_to_lines(&self.content, theme);
                Self::prepend_bars(&mut content_lines, bar_color, width);

                // Add metadata line (model name, response time)
                if let Some(ref model) = self.model_name {
                    let mut meta_spans = vec![
                        Span::styled(LEFT_BAR, Style::default().fg(bar_color)),
                        Span::raw(BAR_PAD),
                    ];
                    let meta_text = if let Some(secs) = self.response_time {
                        format!("{model} \u{00b7} {secs:.1}s")
                    } else {
                        model.clone()
                    };
                    meta_spans.push(Span::styled(
                        meta_text,
                        Style::default().fg(theme.text_dimmed),
                    ));
                    content_lines.push(Line::from(meta_spans));
                }

                content_lines
            }
            MessageKind::User => {
                let mut result: Vec<Line<'static>> = self
                    .content
                    .lines()
                    .map(|l| {
                        Line::from(vec![
                            Span::styled(l.to_owned(), Style::default().fg(theme.text)),
                        ])
                    })
                    .collect();
                if result.is_empty() {
                    result.push(Line::from(Span::styled(
                        String::new(),
                        Style::default().fg(theme.text),
                    )));
                }
                Self::prepend_bars(&mut result, bar_color, width);
                result
            }
            MessageKind::ToolCall => {
                // Compact OpenCode-style: "▸ tool_name · args"
                let tool_name = self.content.split_whitespace().next().unwrap_or("");
                let icon_color = match tool_name {
                    "edit" | "write" | "apply_patch" | "bash" => theme.warning,
                    _ => theme.success,
                };
                let rest = self.content[tool_name.len()..].trim_start();
                let mut spans = vec![
                    Span::styled(
                        "\u{25b8} ",
                        Style::default().fg(icon_color),
                    ),
                    Span::styled(
                        format!("{tool_name} "),
                        Style::default()
                            .fg(theme.text_muted)
                            .add_modifier(Modifier::BOLD),
                    ),
                ];
                if !rest.is_empty() {
                    spans.push(Span::styled(
                        rest.to_owned(),
                        Style::default().fg(theme.text_dimmed),
                    ));
                }
                let mut result = vec![Line::from(spans)];
                Self::prepend_bars(&mut result, bar_color, width);
                result
            }
            MessageKind::ToolResult => {
                // Truncate to max 5 lines, dimmed
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
                    let prefix = if i == 0 { "\u{25be} " } else { "  " };
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
                Self::prepend_bars(&mut result, bar_color, width);
                result
            }
            MessageKind::Thinking => {
                let style = Style::default()
                    .fg(theme.text_dimmed)
                    .add_modifier(Modifier::ITALIC);
                let dot = Span::styled(
                    "\u{25cf} ",
                    Style::default().fg(theme.primary),
                );

                if self.content.is_empty() {
                    let mut result = vec![Line::from(vec![
                        dot,
                        Span::styled("Thinking...", style),
                    ])];
                    Self::prepend_bars(&mut result, bar_color, width);
                    result
                } else {
                    // Header line
                    let mut result = vec![Line::from(vec![
                        dot,
                        Span::styled("Thinking", style),
                    ])];
                    // Full thinking content, split by newlines
                    for text_line in self.content.lines() {
                        result.push(Line::from(vec![
                            Span::styled(text_line.to_string(), style),
                        ]));
                    }
                    Self::prepend_bars(&mut result, bar_color, width);
                    result
                }
            }
            MessageKind::Error => {
                let mut result = vec![Line::from(vec![
                    Span::styled(
                        "\u{2717} ",
                        Style::default()
                            .fg(theme.error)
                            .add_modifier(Modifier::BOLD),
                    ),
                    Span::styled(
                        self.content.clone(),
                        Style::default().fg(theme.error),
                    ),
                ])];
                Self::prepend_bars(&mut result, bar_color, width);
                result
            }
            MessageKind::System => {
                // System messages: no left bar, italic, dimmed, centered
                vec![Line::from(Span::styled(
                    self.content.clone(),
                    Style::default()
                        .fg(theme.text_dimmed)
                        .add_modifier(Modifier::ITALIC),
                )).alignment(Alignment::Center)]
            }
        };

        // Show streaming indicator with left bar
        if self.is_streaming {
            if let Some(last) = lines.last_mut() {
                let frame = spinner_frame(spinner_tick);
                last.spans.push(Span::styled(
                    format!(" {frame}"),
                    Style::default().fg(theme.accent),
                ));
            }
        }

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
