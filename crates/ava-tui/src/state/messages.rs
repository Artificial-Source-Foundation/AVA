use crate::rendering::markdown::markdown_to_lines;
use crate::state::theme::Theme;
use crate::text_utils::{display_width, safe_char_width};
use ratatui::layout::Alignment;
use ratatui::style::{Modifier, Style};
use ratatui::text::{Line, Span};
use std::time::Duration;

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum MessageKind {
    User,
    Assistant,
    ToolCall,
    ToolResult,
    Thinking,
    Error,
    System,
    SubAgent,
}

/// Extra data for sub-agent (task tool) messages.
#[derive(Debug, Clone)]
pub struct SubAgentData {
    /// The task prompt/description sent to the sub-agent.
    pub description: String,
    /// Number of tools the sub-agent used (populated on completion).
    pub tool_count: usize,
    /// How long the sub-agent took (populated on completion).
    pub duration: Option<Duration>,
    /// Whether the sub-agent is still executing.
    pub is_running: bool,
    /// Whether the sub-agent failed (set on completion from `ToolResult.is_error`).
    pub failed: bool,
    /// The tool call ID, used to match the ToolResult back.
    pub call_id: String,
    /// The sub-agent's session ID (set on completion via `SubAgentComplete` event).
    pub session_id: Option<String>,
    /// The sub-agent's full conversation as UI messages (set on completion).
    pub session_messages: Vec<UiMessage>,
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
    /// Sub-agent metadata (only set for `MessageKind::SubAgent`).
    pub sub_agent: Option<SubAgentData>,
    /// Tool name (for ToolCall/ToolResult messages).
    pub tool_name: Option<String>,
    /// Agent mode when this message was created.
    pub agent_mode: Option<String>,
    /// When the message started (for computing duration in footer).
    pub started_at: Option<std::time::Instant>,
    /// Transient messages are removed when the user sends a new message.
    /// Used for system info commands (/help, /queue, etc.) that should not
    /// pollute the chat history.
    pub transient: bool,
    /// Whether thinking content is expanded (show all lines) or collapsed (first 5 lines).
    /// Only meaningful for `MessageKind::Thinking` messages. Default: `false` (collapsed).
    pub thinking_expanded: bool,
    /// Whether this message was cancelled by the user pressing Esc.
    /// Cancelled tool calls render dimmed with `[interrupted]` suffix.
    pub cancelled: bool,
    /// Whether the action group containing this tool message is expanded.
    /// Only meaningful for `MessageKind::ToolCall` / `MessageKind::ToolResult`.
    /// When toggled on any message in a group, the renderer checks all group
    /// members and uses this flag for per-group expand/collapse.
    pub tool_group_expanded: bool,
}

/// Minimal dot-pulse spinner — calmer than braille, safe single-width chars.
pub const SPINNER_FRAMES: &[&str] = &["\u{00b7}", ":", "\u{00b7}", " "];

/// Divisor to slow spinner animation. At 16ms ticks, each frame lasts ~150ms
/// giving a smooth ~1.2s full cycle — a calm breathing pulse.
const SPINNER_FRAME_DIVISOR: usize = 9;

/// Returns the current spinner frame based on a tick counter.
/// The tick is divided down so the animation feels calm rather than frantic.
pub fn spinner_frame(tick: usize) -> &'static str {
    SPINNER_FRAMES[(tick / SPINNER_FRAME_DIVISOR) % SPINNER_FRAMES.len()]
}

/// Left-border character — full block for visual weight (design: 3px bar).
const LEFT_BAR: &str = "\u{258E}";
/// Padding after the left bar (design: content padding 16px ≈ 2 chars).
const BAR_PAD: &str = "  ";

/// Format a duration in seconds for display: `3.2s`, `1m 24s`, `2m 0s`.
fn format_duration_secs(secs: f64) -> String {
    if secs < 60.0 {
        format!("{secs:.1}s")
    } else {
        let mins = secs as u64 / 60;
        let rem = secs as u64 % 60;
        format!("{mins}m {rem}s")
    }
}

impl UiMessage {
    pub fn new(kind: MessageKind, content: impl Into<String>) -> Self {
        Self {
            kind,
            content: content.into(),
            is_streaming: false,
            model_name: None,
            response_time: None,
            sub_agent: None,
            tool_name: None,
            agent_mode: None,
            started_at: None,
            transient: false,
            thinking_expanded: false,
            cancelled: false,
            tool_group_expanded: false,
        }
    }

    /// Create a transient message that will be removed when the user sends
    /// their next message. Ideal for info-only output (/help, /queue, etc.).
    pub fn transient(kind: MessageKind, content: impl Into<String>) -> Self {
        let mut msg = Self::new(kind, content);
        msg.transient = true;
        msg
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
            MessageKind::SubAgent => {
                if let Some(data) = &self.sub_agent {
                    if data.failed {
                        theme.error
                    } else if !data.is_running {
                        theme.text_dimmed
                    } else {
                        theme.accent
                    }
                } else {
                    theme.accent
                }
            }
        }
    }

    /// Width of the bar prefix (LEFT_BAR + BAR_PAD) in columns.
    const BAR_PREFIX_WIDTH: u16 = 3; // "▎" (1) + "  " (2)

    /// Prepend a colored left bar + padding to each line, then manually
    /// wrap so that every visual row carries its own bar prefix.
    fn prepend_bars(lines: &mut Vec<Line<'static>>, color: ratatui::style::Color, width: u16) {
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
    fn wrap_line_spans(spans: Vec<Span<'static>>, max_width: usize) -> Vec<Vec<Span<'static>>> {
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
        let total_width: usize = segments.iter().map(|s| display_width(&s.text)).sum();
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

                let rem_width = display_width(remaining);
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
                    } else if remaining.is_empty() {
                        break;
                    } else {
                        // Force at least one char to avoid infinite loop.
                        let ch = remaining.chars().next().expect("remaining is non-empty");
                        let clen = ch.len_utf8();
                        current_row.push(Span::styled(remaining[..clen].to_owned(), style));
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
    /// columns. Prefers breaking at the last space or hyphen within the limit;
    /// falls back to exact column boundary only when no word boundary exists
    /// (e.g. long URLs or file paths without spaces).
    fn find_break_point(text: &str, max_cols: usize) -> usize {
        let mut col = 0usize;
        let mut last_break_byte = None;
        let mut byte_at_max = 0usize;

        for (i, ch) in text.char_indices() {
            let w = safe_char_width(ch);
            if col + w > max_cols {
                byte_at_max = i;
                break;
            }
            if ch == ' ' {
                // Break AT the space — space becomes the last char on this line,
                // and wrap_line_spans will skip the leading space on the next line.
                last_break_byte = Some(i);
            } else if ch == '-' {
                // Break AFTER the hyphen so it stays on the current line.
                last_break_byte = Some(i + ch.len_utf8());
            } else if ch == ',' {
                // Break AFTER comma so comma stays on current line.
                last_break_byte = Some(i + ch.len_utf8());
            }
            col += w;
            byte_at_max = i + ch.len_utf8();
        }

        // If we consumed the entire string without exceeding, return full length.
        if col <= max_cols {
            return byte_at_max;
        }

        // Prefer word boundary (space or hyphen).
        if let Some(bp) = last_break_byte {
            if bp > 0 {
                return bp;
            }
        }
        // No word boundary found — character-break as fallback (long URLs, paths).
        byte_at_max
    }

    pub fn to_lines(&self, theme: &Theme, spinner_tick: usize, width: u16) -> Vec<Line<'static>> {
        self.to_lines_with_options(theme, spinner_tick, width, true)
    }

    pub fn to_lines_with_options(
        &self,
        theme: &Theme,
        spinner_tick: usize,
        width: u16,
        show_thinking: bool,
    ) -> Vec<Line<'static>> {
        let bar_color = self.bar_color(theme);

        let mut lines = match self.kind {
            MessageKind::Assistant => {
                let mut content_lines = markdown_to_lines(&self.content, theme);

                // Append blinking cursor to last line while streaming
                if self.is_streaming {
                    let cursor_char = if (spinner_tick / 8) % 2 == 0 {
                        "\u{258c}" // ▌
                    } else {
                        " "
                    };
                    if let Some(last_line) = content_lines.last_mut() {
                        last_line.spans.push(Span::styled(
                            cursor_char.to_string(),
                            Style::default().fg(theme.primary),
                        ));
                    }
                }

                Self::prepend_bars(&mut content_lines, bar_color, width);

                // Per-message footer: ■ mode · model · duration (only after streaming completes)
                if !self.is_streaming {
                    if let Some(ref model) = self.model_name {
                        let mode_label = self.agent_mode.as_deref().unwrap_or("Code");
                        let duration_part = if let Some(secs) = self.response_time {
                            format!(" \u{00b7} {}", format_duration_secs(secs))
                        } else {
                            String::new()
                        };
                        let footer_raw =
                            format!("\u{25a0} {mode_label} \u{00b7} {model}{duration_part}");
                        // Truncate footer to fit within content area (width minus bar prefix)
                        let footer_budget = (width.saturating_sub(Self::BAR_PREFIX_WIDTH)) as usize;
                        let footer_text =
                            crate::text_utils::truncate_display(&footer_raw, footer_budget);
                        let mut footer_spans = vec![
                            Span::styled(LEFT_BAR, Style::default().fg(bar_color)),
                            Span::raw(BAR_PAD),
                        ];
                        footer_spans.push(Span::styled(
                            footer_text,
                            Style::default()
                                .fg(theme.text_dimmed)
                                .add_modifier(Modifier::DIM),
                        ));
                        content_lines.push(Line::from(footer_spans));
                    }
                }

                content_lines
            }
            MessageKind::User => {
                let mut result: Vec<Line<'static>> = self
                    .content
                    .lines()
                    .map(|l| {
                        Line::from(vec![Span::styled(
                            l.to_owned(),
                            Style::default().fg(theme.text),
                        )])
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
                // Human-readable activity line (Claude Code style)
                let activity =
                    crate::widgets::message::tool_activity_line(&self.content, self.is_streaming);

                if self.cancelled {
                    let dim = Style::default()
                        .fg(theme.text_dimmed)
                        .add_modifier(Modifier::DIM);
                    let mut result = vec![Line::from(vec![
                        Span::styled("\u{25cf} ", dim),
                        Span::styled(activity, dim),
                        Span::styled(" [interrupted]", dim),
                    ])];
                    Self::prepend_bars(&mut result, bar_color, width);
                    result
                } else {
                    let mut result = vec![Line::from(vec![
                        Span::styled(
                            "\u{25cf} ",
                            Style::default()
                                .fg(theme.text_dimmed)
                                .add_modifier(Modifier::DIM),
                        ),
                        Span::styled(
                            activity,
                            Style::default()
                                .fg(theme.text_dimmed)
                                .add_modifier(Modifier::DIM),
                        ),
                    ])];
                    Self::prepend_bars(&mut result, bar_color, width);
                    result
                }
            }
            MessageKind::ToolResult => {
                let dim_style = Style::default()
                    .fg(theme.text_dimmed)
                    .add_modifier(Modifier::DIM);
                let tool = self.tool_name.as_deref().unwrap_or("");
                let content_lines: Vec<&str> = self.content.lines().collect();
                let total = content_lines.len();

                let mut result = Vec::new();

                match tool {
                    // Show full output for edit/write tools with diff coloring
                    "edit" | "write" | "multiedit" | "apply_patch" => {
                        for (i, line) in content_lines.iter().enumerate() {
                            let prefix = if i == 0 { "\u{25be} " } else { "  " };
                            let line_style = if line.starts_with('+') {
                                Style::default().fg(ratatui::style::Color::Green)
                            } else if line.starts_with('-') {
                                Style::default().fg(ratatui::style::Color::Red)
                            } else if line.starts_with('@') {
                                Style::default().fg(ratatui::style::Color::Cyan)
                            } else {
                                dim_style
                            };
                            result.push(Line::from(Span::styled(
                                format!("{prefix}{line}"),
                                line_style,
                            )));
                        }
                    }
                    // Read: show summary
                    "read" => {
                        let filename = content_lines
                            .first()
                            .and_then(|l| l.split_whitespace().last())
                            .unwrap_or("file");
                        result.push(Line::from(Span::styled(
                            format!("\u{25be} \u{1f4c4} Read {filename} ({total} lines)"),
                            dim_style,
                        )));
                    }
                    // Bash: show command + max 5 lines
                    "bash" => {
                        let cmd_line = content_lines.first().copied().unwrap_or("");
                        result.push(Line::from(Span::styled(
                            format!("\u{25be} $ {cmd_line}"),
                            dim_style,
                        )));
                        let output_lines = if total > 1 { &content_lines[1..] } else { &[] };
                        let max_lines = 5;
                        let show = output_lines.len().min(max_lines);
                        for line in &output_lines[..show] {
                            result.push(Line::from(Span::styled(format!("  {line}"), dim_style)));
                        }
                        if output_lines.len() > max_lines {
                            result.push(Line::from(Span::styled(
                                format!("  ... ({} more lines)", output_lines.len() - max_lines),
                                dim_style,
                            )));
                        }
                    }
                    // Glob/grep: show match count summary
                    "glob" | "grep" => {
                        result.push(Line::from(Span::styled(
                            format!("\u{25be} {total} matches"),
                            dim_style,
                        )));
                    }
                    // Default: max 5 lines
                    _ => {
                        let truncated = total > 5;
                        let display_lines = if truncated {
                            &content_lines[..5]
                        } else {
                            &content_lines[..]
                        };
                        for (i, line) in display_lines.iter().enumerate() {
                            let prefix = if i == 0 { "\u{25be} " } else { "  " };
                            result.push(Line::from(Span::styled(
                                format!("{prefix}{line}"),
                                dim_style,
                            )));
                        }
                        if truncated {
                            result.push(Line::from(Span::styled(
                                format!("  ... ({} more lines)", total - 5),
                                dim_style,
                            )));
                        }
                    }
                }
                Self::prepend_bars(&mut result, bar_color, width);
                result
            }
            MessageKind::Thinking => {
                let style = Style::default()
                    .fg(theme.text_dimmed)
                    .add_modifier(Modifier::ITALIC);
                let dot = Span::styled("\u{25cf} ", Style::default().fg(theme.primary));

                if !show_thinking {
                    // Minimal single-line hint when thinking visibility is off
                    let dim_style = Style::default()
                        .fg(theme.text_dimmed)
                        .add_modifier(Modifier::DIM | Modifier::ITALIC);
                    let mut result = vec![Line::from(vec![
                        Span::styled("\u{00b7} ", dim_style), // · (middle dot)
                        Span::styled("thinking...", dim_style),
                    ])];
                    Self::prepend_bars(&mut result, bar_color, width);
                    result
                } else if self.content.is_empty() {
                    let mut result =
                        vec![Line::from(vec![dot, Span::styled("Thinking...", style)])];
                    Self::prepend_bars(&mut result, bar_color, width);
                    result
                } else {
                    let content_lines: Vec<&str> = self.content.lines().collect();
                    let total = content_lines.len();
                    let max_visible = 2;
                    let is_collapsible = total > max_visible;

                    // Header line with expand/collapse indicator
                    let header_label = if is_collapsible {
                        if self.thinking_expanded {
                            "Thinking \u{25bc}" // ▼ = expanded
                        } else {
                            "Thinking \u{25b6}" // ▶ = collapsed
                        }
                    } else {
                        "Thinking"
                    };
                    let mut result = vec![Line::from(vec![
                        dot,
                        Span::styled(header_label.to_owned(), style),
                    ])];

                    if self.thinking_expanded || !is_collapsible {
                        // Show ALL lines when expanded (or when content is short enough)
                        for text_line in &content_lines {
                            result
                                .push(Line::from(vec![Span::styled(text_line.to_string(), style)]));
                        }
                    } else {
                        // Collapsed: show first max_visible lines + indicator
                        for text_line in &content_lines[..max_visible] {
                            result
                                .push(Line::from(vec![Span::styled(text_line.to_string(), style)]));
                        }
                        let click_hint = if total > max_visible {
                            format!(
                                "\u{25b6} ... ({} more lines \u{2014} click or Ctrl+E to expand)",
                                total - max_visible
                            )
                        } else {
                            String::new()
                        };
                        if !click_hint.is_empty() {
                            result.push(Line::from(vec![Span::styled(click_hint, style)]));
                        }
                    }

                    Self::prepend_bars(&mut result, bar_color, width);
                    result
                }
            }
            MessageKind::Error => {
                let error_style = Style::default().fg(theme.error);
                let bold_error = Style::default()
                    .fg(theme.error)
                    .add_modifier(Modifier::BOLD);
                let hint_style = Style::default()
                    .fg(theme.warning)
                    .add_modifier(Modifier::ITALIC);

                // Header line: ✗ Error
                let mut result = vec![Line::from(vec![
                    Span::styled("\u{2717} ", bold_error),
                    Span::styled("Error", bold_error),
                ])];

                // Error body — each line gets its own Line entry for proper wrapping
                for text_line in self.content.lines() {
                    result.push(Line::from(vec![Span::styled(
                        text_line.to_owned(),
                        error_style,
                    )]));
                }

                // Contextual hints based on error content
                let lower = self.content.to_lowercase();
                let hint = if lower.contains("rate limit") || lower.contains("429") {
                    Some("Rate limited \u{2014} try again in a moment or switch to a different model")
                } else if lower.contains("timeout") || lower.contains("timed out") {
                    Some("Request timed out \u{2014} the model may be overloaded")
                } else if lower.contains("authentication")
                    || lower.contains("401")
                    || lower.contains("403")
                {
                    Some("Authentication failed \u{2014} check your credentials with /connect")
                } else if (lower.contains("context") || lower.contains("token"))
                    && lower.contains("exceed")
                {
                    Some("Context window exceeded \u{2014} try /compact to reduce context")
                } else {
                    None
                };

                if let Some(hint_text) = hint {
                    // Blank separator line
                    result.push(Line::from(Span::raw(String::new())));
                    result.push(Line::from(vec![Span::styled(
                        hint_text.to_owned(),
                        hint_style,
                    )]));
                }

                Self::prepend_bars(&mut result, bar_color, width);
                result
            }
            MessageKind::System => {
                // System messages: no left bar, italic, dimmed, centered.
                // Wrap to available width so long system messages don't overflow.
                let style = Style::default()
                    .fg(theme.text_dimmed)
                    .add_modifier(Modifier::ITALIC);
                let content_width = width as usize;
                let spans = vec![Span::styled(self.content.clone(), style)];
                let wrapped = Self::wrap_line_spans(spans, content_width);
                wrapped
                    .into_iter()
                    .map(|row| Line::from(row).alignment(Alignment::Center))
                    .collect()
            }
            MessageKind::SubAgent => {
                let data = self.sub_agent.as_ref();
                let description = data
                    .map(|d| d.description.as_str())
                    .unwrap_or(&self.content);
                let is_running = data.map(|d| d.is_running).unwrap_or(false);
                let is_failed = data.map(|d| d.failed).unwrap_or(false);

                // Box drawing characters
                let top_left = "\u{256d}"; // ╭
                let top_right = "\u{256e}"; // ╮
                let bot_left = "\u{2570}"; // ╰
                let bot_right = "\u{256f}"; // ╯
                let horiz = "\u{2500}"; // ─
                let vert = "\u{2502}"; // │

                // Calculate inner width: total width minus bar prefix (3) minus box border+padding on each side (2+1 left, 1+2 right = 6)
                // Box: "│ " (2) content "│" (1) = 3 chars for box borders
                // But we also have the left bar prefix (3 chars) prepended later.
                // Available width inside the box for content:
                let box_outer_width = if width > Self::BAR_PREFIX_WIDTH + 4 {
                    (width - Self::BAR_PREFIX_WIDTH) as usize
                } else {
                    40 // fallback minimum
                };
                // Inner content width: box_outer_width minus "│ " (2) and " │" (2)
                let inner_width = box_outer_width.saturating_sub(4);

                // Style varies by state: running=normal, failed=red, completed=dimmed
                let border_style = if is_failed {
                    Style::default().fg(theme.error).add_modifier(Modifier::DIM)
                } else if !is_running {
                    Style::default()
                        .fg(theme.text_dimmed)
                        .add_modifier(Modifier::DIM)
                } else {
                    Style::default().fg(theme.border)
                };
                let label_style = if is_failed {
                    Style::default()
                        .fg(theme.error)
                        .add_modifier(Modifier::BOLD)
                } else if !is_running {
                    Style::default()
                        .fg(theme.text_dimmed)
                        .add_modifier(Modifier::BOLD)
                } else {
                    Style::default()
                        .fg(theme.accent)
                        .add_modifier(Modifier::BOLD)
                };
                let desc_style = if is_failed {
                    Style::default().fg(theme.error).add_modifier(Modifier::DIM)
                } else if !is_running {
                    Style::default().fg(theme.text_dimmed)
                } else {
                    Style::default().fg(theme.text_muted)
                };
                let dimmed_style = Style::default().fg(theme.text_dimmed);

                // Truncate description to fit inner width (minus icon + "Sub-agent: " prefix)
                let prefix_len = 14; // icon(2) + "Sub-agent: "(11) + quote(1)
                let max_desc = inner_width.saturating_sub(prefix_len + 1); // +1 for closing quote
                let desc_display = crate::text_utils::truncate_display(description, max_desc);

                let mut result = Vec::new();

                let fill_len = box_outer_width.saturating_sub(2); // minus corners
                let top_border = format!("{top_left}{}{top_right}", horiz.repeat(fill_len));
                result.push(Line::from(Span::styled(top_border, border_style)));

                if is_running {
                    let frame = spinner_frame(spinner_tick);
                    let header_content_spans = vec![
                        Span::styled(format!("{frame} "), Style::default().fg(theme.accent)),
                        Span::styled("Sub-agent: ", label_style),
                        Span::styled(desc_display.clone(), desc_style),
                    ];
                    let header_text_width: usize = header_content_spans
                        .iter()
                        .map(|s| display_width(s.content.as_ref()))
                        .sum();
                    let header_pad = inner_width.saturating_sub(header_text_width);
                    let mut header_line_spans =
                        vec![Span::styled(format!("{vert} "), border_style)];
                    header_line_spans.extend(header_content_spans);
                    header_line_spans.push(Span::styled(
                        format!("{}{vert}", " ".repeat(header_pad)),
                        border_style,
                    ));
                    result.push(Line::from(header_line_spans));
                } else {
                    let tool_count = data.map(|d| d.tool_count).unwrap_or(0);
                    let duration_str = data
                        .and_then(|d| d.duration)
                        .map(|d| format!("{:.1}s", d.as_secs_f64()))
                        .unwrap_or_default();

                    let (icon, icon_style) = if is_failed {
                        (
                            "\u{2717} ",
                            Style::default()
                                .fg(theme.error)
                                .add_modifier(Modifier::BOLD),
                        )
                    } else {
                        (
                            "\u{2713} ",
                            Style::default()
                                .fg(theme.success)
                                .add_modifier(Modifier::DIM),
                        )
                    };

                    let header_content_spans = vec![
                        Span::styled(icon, icon_style),
                        Span::styled("Sub-agent: ", label_style),
                        Span::styled(desc_display.clone(), desc_style),
                    ];
                    let header_text_width: usize = header_content_spans
                        .iter()
                        .map(|s| display_width(s.content.as_ref()))
                        .sum();
                    let header_pad = inner_width.saturating_sub(header_text_width);
                    let mut header_line_spans =
                        vec![Span::styled(format!("{vert} "), border_style)];
                    header_line_spans.extend(header_content_spans);
                    header_line_spans.push(Span::styled(
                        format!("{}{vert}", " ".repeat(header_pad)),
                        border_style,
                    ));
                    result.push(Line::from(header_line_spans));

                    let stats = if !duration_str.is_empty() {
                        format!("{tool_count} tools, {duration_str}")
                    } else if tool_count > 0 {
                        format!("{tool_count} tools")
                    } else {
                        String::new()
                    };
                    if !stats.is_empty() {
                        let stats_width = display_width(&stats);
                        let stats_pad = inner_width.saturating_sub(stats_width);
                        result.push(Line::from(vec![
                            Span::styled(format!("{vert} "), border_style),
                            Span::styled(stats, dimmed_style),
                            Span::styled(format!("{}{vert}", " ".repeat(stats_pad)), border_style),
                        ]));
                    }

                    if !self.content.is_empty() {
                        let sep_fill = box_outer_width.saturating_sub(2);
                        let sep = format!("\u{251c}{}\u{2524}", horiz.repeat(sep_fill));
                        result.push(Line::from(Span::styled(sep, border_style)));

                        let content_lines: Vec<&str> = self.content.lines().collect();
                        let show_lines = 3;
                        let truncated = content_lines.len() > show_lines;
                        let display = if truncated {
                            &content_lines[..show_lines]
                        } else {
                            &content_lines[..]
                        };

                        let content_style = if is_failed {
                            Style::default().fg(theme.error).add_modifier(Modifier::DIM)
                        } else {
                            Style::default()
                                .fg(theme.text_dimmed)
                                .add_modifier(Modifier::DIM)
                        };

                        for line in display {
                            let display_line =
                                crate::text_utils::truncate_display(line, inner_width);
                            let line_width = display_width(&display_line);
                            let line_pad = inner_width.saturating_sub(line_width);
                            result.push(Line::from(vec![
                                Span::styled(format!("{vert} "), border_style),
                                Span::styled(display_line, content_style),
                                Span::styled(
                                    format!("{}{vert}", " ".repeat(line_pad)),
                                    border_style,
                                ),
                            ]));
                        }
                        if truncated {
                            let more_text =
                                format!("[+{} more lines]", content_lines.len() - show_lines);
                            let more_width = display_width(&more_text);
                            let more_pad = inner_width.saturating_sub(more_width);
                            result.push(Line::from(vec![
                                Span::styled(format!("{vert} "), border_style),
                                Span::styled(more_text, content_style),
                                Span::styled(
                                    format!("{}{vert}", " ".repeat(more_pad)),
                                    border_style,
                                ),
                            ]));
                        }
                    }

                    let has_conversation = data
                        .map(|d| !d.session_messages.is_empty())
                        .unwrap_or(false);
                    if has_conversation {
                        let msg_count = data.map(|d| d.session_messages.len()).unwrap_or(0);
                        let hint = format!("[{msg_count} messages \u{2014} Enter to view]");
                        let hint_width = display_width(&hint);
                        let hint_pad = inner_width.saturating_sub(hint_width);
                        let hint_style = if is_failed {
                            Style::default()
                                .fg(theme.error)
                                .add_modifier(Modifier::DIM | Modifier::ITALIC)
                        } else {
                            Style::default()
                                .fg(theme.text_dimmed)
                                .add_modifier(Modifier::DIM | Modifier::ITALIC)
                        };
                        result.push(Line::from(vec![
                            Span::styled(format!("{vert} "), border_style),
                            Span::styled(hint, hint_style),
                            Span::styled(format!("{}{vert}", " ".repeat(hint_pad)), border_style),
                        ]));
                    }
                }

                let bot_border = format!(
                    "{bot_left}{}{bot_right}",
                    horiz.repeat(box_outer_width.saturating_sub(2))
                );
                result.push(Line::from(Span::styled(bot_border, border_style)));

                Self::prepend_bars(&mut result, bar_color, width);
                result
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
