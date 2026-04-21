use crate::rendering::markdown::markdown_to_lines;
use crate::state::theme::Theme;
use crate::text_utils::display_width;
use ratatui::layout::Alignment;
use ratatui::style::{Modifier, Style};
use ratatui::text::{Line, Span};

use super::spinner::inline_spinner_frame;
use super::{MessageKind, UiMessage};

fn normalize_subagent_title(description: &str, agent_type: Option<&str>) -> (String, String) {
    let trimmed = description.trim();
    let parsed = trimmed
        .strip_prefix('[')
        .and_then(|rest| rest.split_once(']'))
        .map(|(label, tail)| (label.trim(), tail.trim()));

    let agent_label = agent_type
        .filter(|value| !value.trim().is_empty())
        .map(|value| value.trim().to_string())
        .or_else(|| {
            parsed
                .as_ref()
                .map(|(label, _)| (*label).to_string())
                .filter(|value| !value.is_empty())
        })
        .unwrap_or_else(|| "subagent".to_string());

    let prompt = parsed
        .map(|(_, tail)| tail.to_string())
        .filter(|tail| !tail.is_empty())
        .unwrap_or_else(|| trimmed.to_string());

    (agent_label, prompt)
}

fn format_subagent_agent_label(agent_label: &str) -> String {
    agent_label
        .split(|ch: char| matches!(ch, '-' | '_' | ' '))
        .filter(|part| !part.is_empty())
        .map(|part| {
            let mut chars = part.chars();
            match chars.next() {
                Some(first) => format!(
                    "{}{}",
                    first.to_ascii_uppercase(),
                    chars.as_str().to_ascii_lowercase()
                ),
                None => String::new(),
            }
        })
        .collect::<Vec<_>>()
        .join("-")
}

fn recent_subagent_activity_lines(data: Option<&super::SubAgentData>) -> Vec<String> {
    let Some(data) = data else {
        return Vec::new();
    };

    data.session_messages
        .iter()
        .filter(|message| matches!(message.kind, MessageKind::ToolCall))
        .rev()
        .take(2)
        .map(|message| crate::widgets::message::tool_activity_line(&message.content, false))
        .collect::<Vec<_>>()
        .into_iter()
        .rev()
        .collect()
}

/// Left-border character for message grouping.
const LEFT_BAR: &str = "│";
/// Padding after the left bar (design: content padding 16px ≈ 2 chars).
const BAR_PAD: &str = "  ";

fn role_label(kind: &MessageKind) -> &'static str {
    match kind {
        MessageKind::User => "you",
        MessageKind::Assistant => "ava",
        MessageKind::Thinking => "thinking",
        MessageKind::Error => "error",
        MessageKind::SubAgent => "sub-agent",
        MessageKind::System => "system",
        MessageKind::ToolCall | MessageKind::ToolResult => "tools",
    }
}

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

    /// Width of the bar prefix (LEFT_BAR + BAR_PAD) in display columns.
    ///
    /// `display_width('│')` returns 1 plus `"  "` (2 columns) = 3 total. This MUST
    /// match the value returned by `display_width(LEFT_BAR) + display_width(BAR_PAD)`
    /// so that `wrap_line_spans` reserves the correct amount of space and lines
    /// are not truncated mid-word by the downstream `clamp_line_width` safety net.
    const BAR_PREFIX_WIDTH: u16 = 3;

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

    fn prepend_header(
        lines: &mut Vec<Line<'static>>,
        kind: &MessageKind,
        theme: &Theme,
        width: u16,
        meta: Option<String>,
    ) {
        if matches!(
            kind,
            MessageKind::System
                | MessageKind::ToolCall
                | MessageKind::ToolResult
                | MessageKind::Assistant
                | MessageKind::User
        ) {
            return;
        }

        let bar_color = match kind {
            MessageKind::User => theme.secondary,
            MessageKind::Assistant => theme.primary,
            MessageKind::Thinking => theme.primary,
            MessageKind::Error => theme.error,
            MessageKind::SubAgent => theme.accent,
            MessageKind::System | MessageKind::ToolCall | MessageKind::ToolResult => {
                theme.text_dimmed
            }
        };
        let label_style = Style::default().fg(theme.text_muted);
        let meta_style = Style::default()
            .fg(theme.text_dimmed)
            .add_modifier(Modifier::DIM);
        let budget = width.saturating_sub(Self::BAR_PREFIX_WIDTH) as usize;

        let mut content = role_label(kind).to_string();
        if let Some(meta) = meta {
            let meta_budget = budget.saturating_sub(display_width(&content) + 3);
            if meta_budget > 0 {
                let meta = crate::text_utils::truncate_display(&meta, meta_budget);
                if !meta.is_empty() {
                    content.push_str(" · ");
                    content.push_str(&meta);
                }
            }
        }

        let mut spans = vec![
            Span::styled(LEFT_BAR, Style::default().fg(bar_color)),
            Span::raw(BAR_PAD),
        ];

        let mut parts = content.splitn(2, " · ");
        let head = parts.next().unwrap_or_default();
        spans.push(Span::styled(head.to_string(), label_style));
        if let Some(tail) = parts.next() {
            spans.push(Span::styled(" · ", meta_style));
            spans.push(Span::styled(tail.to_string(), meta_style));
        }

        lines.insert(0, Line::from(spans));
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
                    let cursor_char = if (spinner_tick / 8).is_multiple_of(2) {
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

                // Per-message footer: mode · model · duration (only after streaming completes)
                if !self.is_streaming && self.model_name.is_some() {
                    let mode_label = match self.agent_mode.as_deref() {
                        Some("Code" | "code") | None => "build".to_string(),
                        Some("Plan" | "plan") => "plan".to_string(),
                        Some(other) => other.to_lowercase(),
                    };
                    let model_part = self.model_name.clone().unwrap_or_default();
                    let duration_part = if let Some(secs) = self.response_time {
                        format!(" \u{00b7} {}", format_duration_secs(secs))
                    } else {
                        String::new()
                    };
                    let footer_raw = format!("{mode_label} \u{00b7} {model_part}{duration_part}");
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
                            .add_modifier(Modifier::ITALIC),
                    ));
                    content_lines.push(Line::from(footer_spans));
                }

                content_lines
            }
            MessageKind::User => {
                let mut raw_lines: Vec<Line<'static>> = self
                    .content
                    .lines()
                    .map(|l| {
                        Line::from(vec![Span::styled(
                            l.to_owned(),
                            Style::default().fg(theme.text).bg(theme.bg_user_message),
                        )])
                    })
                    .collect();
                if raw_lines.is_empty() {
                    raw_lines.push(Line::from(Span::styled(
                        String::new(),
                        Style::default().fg(theme.text).bg(theme.bg_user_message),
                    )));
                }

                let content_width = width.saturating_sub(8) as usize;
                let mut result = Vec::new();
                for line in raw_lines {
                    let wrapped = Self::wrap_line_spans(line.spans, content_width.max(1));
                    for sub_spans in wrapped {
                        let line_width: usize = sub_spans
                            .iter()
                            .map(|span| display_width(span.content.as_ref()))
                            .sum();
                        let remaining = content_width.saturating_sub(line_width);
                        let mut spans = vec![
                            Span::styled("│ ", Style::default().fg(theme.primary)),
                            Span::raw(" "),
                            Span::styled(" ", Style::default().bg(theme.bg_user_message)),
                        ];
                        spans.extend(sub_spans.into_iter().map(|span| {
                            Span::styled(
                                span.content.into_owned(),
                                span.style.bg(theme.bg_user_message),
                            )
                        }));
                        spans.push(Span::styled(
                            " ".repeat(remaining + 3),
                            Style::default().bg(theme.bg_user_message),
                        ));
                        result.push(Line::from(spans));
                    }
                }
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
                        Span::styled("tool  ", dim),
                        Span::styled(activity, dim),
                        Span::styled(" [interrupted]", dim),
                    ])];
                    Self::prepend_bars(&mut result, bar_color, width);
                    result
                } else {
                    let mut result = vec![Line::from(vec![Span::styled(
                        format!("tool  {activity}"),
                        Style::default()
                            .fg(theme.text_dimmed)
                            .add_modifier(Modifier::DIM),
                    )])];
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
                    // Show side-by-side diff for edit/write tools when terminal is wide enough
                    "edit" | "write" | "multiedit" | "apply_patch" => {
                        let sbs_width = width.saturating_sub(Self::BAR_PREFIX_WIDTH);
                        if let Some(sbs_lines) = crate::rendering::diff::render_side_by_side(
                            &self.content,
                            theme,
                            sbs_width,
                        ) {
                            result.extend(sbs_lines);
                        } else {
                            // Fallback: unified diff coloring
                            for (i, line) in content_lines.iter().enumerate() {
                                let prefix = if i == 0 { "\u{25be} " } else { "  " };
                                let line_style = if line.starts_with('+') {
                                    Style::default().fg(theme.diff_added)
                                } else if line.starts_with('-') {
                                    Style::default().fg(theme.diff_removed)
                                } else if line.starts_with('@') {
                                    Style::default().fg(theme.diff_hunk_header)
                                } else {
                                    dim_style
                                };
                                result.push(Line::from(Span::styled(
                                    format!("{prefix}{line}"),
                                    line_style,
                                )));
                            }
                        }
                    }
                    // Read: show summary
                    "read" => {
                        let filename = content_lines
                            .first()
                            .and_then(|l| l.split_whitespace().last())
                            .unwrap_or("file");
                        result.push(Line::from(Span::styled(
                            format!("\u{25be} Read {filename} ({total} lines)"),
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
                                format!(
                                    "  ... ({} more lines, Ctrl+E to expand)",
                                    output_lines.len() - max_lines
                                ),
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
                                format!("  ... ({} more lines, Ctrl+E to expand)", total - 5),
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
                    Self::prepend_header(&mut result, &self.kind, theme, width, None);
                    result
                } else if self.content.is_empty() {
                    let mut result =
                        vec![Line::from(vec![dot, Span::styled("Thinking...", style)])];
                    Self::prepend_bars(&mut result, bar_color, width);
                    Self::prepend_header(&mut result, &self.kind, theme, width, None);
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
                                "\u{25b6} ... ({} more lines, click or Ctrl+E)",
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
                    Self::prepend_header(&mut result, &self.kind, theme, width, None);
                    result
                }
            }
            MessageKind::Error => {
                let error_style = Style::default().fg(theme.text);
                let bold_error = Style::default().fg(theme.text).add_modifier(Modifier::BOLD);
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
                let (hint, action) = if lower.contains("rate limit") || lower.contains("429") {
                    (
                        Some("Rate limited \u{2014} try again in a moment or switch to a different model"),
                        Some("  \u{2192} Press Enter to retry, or Ctrl+M to switch model"),
                    )
                } else if lower.contains("timeout") || lower.contains("timed out") {
                    (
                        Some("Request timed out \u{2014} the model may be overloaded"),
                        Some("  \u{2192} Press Enter to retry, or try a faster model"),
                    )
                } else if lower.contains("authentication")
                    || lower.contains("auth")
                    || lower.contains("401")
                    || lower.contains("403")
                {
                    (
                        Some("Authentication failed \u{2014} check your credentials with /connect"),
                        Some("  \u{2192} Use /connect to reconfigure credentials"),
                    )
                } else if (lower.contains("context") || lower.contains("token"))
                    && lower.contains("exceed")
                {
                    (
                        Some("Context window exceeded \u{2014} try /compact to reduce context"),
                        Some("  \u{2192} Use /compact to reduce context, or switch to a larger model"),
                    )
                } else if lower.contains("500")
                    || lower.contains("server_error")
                    || lower.contains("internal server error")
                {
                    (
                        Some("Server error \u{2014} the provider returned a 500 error"),
                        Some("  \u{2192} Press Enter to retry, or switch to a different model with Ctrl+M"),
                    )
                } else {
                    (None, None)
                };

                let action_style = Style::default()
                    .fg(theme.accent)
                    .add_modifier(Modifier::BOLD);

                if let Some(hint_text) = hint {
                    // Blank separator line
                    result.push(Line::from(Span::raw(String::new())));
                    result.push(Line::from(vec![Span::styled(
                        hint_text.to_owned(),
                        hint_style,
                    )]));
                }
                if let Some(action_text) = action {
                    result.push(Line::from(vec![Span::styled(
                        action_text.to_owned(),
                        action_style,
                    )]));
                }

                Self::prepend_bars(&mut result, bar_color, width);
                Self::prepend_header(&mut result, &self.kind, theme, width, None);
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
                let (agent_label, prompt) = normalize_subagent_title(
                    description,
                    data.and_then(|d| d.agent_type.as_deref()),
                );
                let agent_label = format_subagent_agent_label(&agent_label);
                let is_running = data.map(|d| d.is_running).unwrap_or(false);
                let is_failed = data.map(|d| d.failed).unwrap_or(false);

                let width_budget = width.saturating_sub(Self::BAR_PREFIX_WIDTH) as usize;
                let title_style = if is_failed {
                    Style::default().fg(theme.error).add_modifier(Modifier::DIM)
                } else if is_running {
                    Style::default().fg(theme.text).add_modifier(Modifier::BOLD)
                } else {
                    Style::default().fg(theme.text_muted)
                };
                let meta_style = Style::default().fg(theme.text_dimmed);
                let tree = "├─ ";
                let desc_budget = width_budget.saturating_sub(display_width(tree)).max(1);
                let title = format!("{} Task - {}", agent_label, prompt);
                let desc_display = crate::text_utils::truncate_display(&title, desc_budget);

                let mut title_spans =
                    vec![Span::styled(tree, Style::default().fg(theme.text_dimmed))];
                if is_running {
                    title_spans.push(Span::styled(
                        format!("{} ", inline_spinner_frame(spinner_tick)),
                        Style::default().fg(theme.warning),
                    ));
                }
                title_spans.push(Span::styled(desc_display, title_style));
                let mut result = vec![Line::from(title_spans)];

                let tool_count = data.map(|d| d.tool_count).unwrap_or(0);
                let duration_str = data
                    .and_then(|d| d.duration)
                    .map(|d| format!("{:.1}s", d.as_secs_f64()));
                let provider_str = data
                    .and_then(|d| d.provider.as_deref())
                    .map(|provider| format!("via {provider}"));
                let resumed_str = data.filter(|d| d.resumed).map(|_| "resumed".to_string());
                let cost_str = data
                    .and_then(|d| d.cost_usd)
                    .map(|cost| format!("${cost:.4}"));
                let token_str = data.and_then(|d| match (d.input_tokens, d.output_tokens) {
                    (Some(input), Some(output)) => Some(format!("{input}/{output} tok")),
                    _ => None,
                });
                let conversation_str = data
                    .map(|d| d.session_messages.len())
                    .filter(|count| *count > 0)
                    .map(|count| format!("{count} messages"));

                let mut stats_parts = Vec::new();
                if tool_count > 0 {
                    stats_parts.push(format!("{tool_count} toolcalls"));
                }
                if let Some(duration_str) = duration_str {
                    stats_parts.push(duration_str);
                }
                if let Some(provider_str) = provider_str {
                    stats_parts.push(provider_str);
                }
                if let Some(resumed_str) = resumed_str {
                    stats_parts.push(resumed_str);
                }
                if let Some(cost_str) = cost_str {
                    stats_parts.push(cost_str);
                }
                if let Some(token_str) = token_str {
                    stats_parts.push(token_str);
                }
                if let Some(conversation_str) = conversation_str {
                    stats_parts.push(conversation_str);
                }

                if is_running {
                    if let Some(tool) = data.and_then(|d| d.current_tool.as_deref()) {
                        stats_parts.insert(0, format!("running {tool}"));
                    } else {
                        stats_parts.insert(0, "initializing...".to_string());
                    }
                } else if is_failed {
                    stats_parts.insert(0, "failed".to_string());
                } else {
                    stats_parts.insert(0, "done".to_string());
                }

                let stats = stats_parts.join(" • ");
                if !stats.is_empty() {
                    result.push(Line::from(vec![
                        Span::styled("│  ", Style::default().fg(theme.text_dimmed)),
                        Span::styled(
                            crate::text_utils::truncate_display(
                                &stats,
                                width_budget.saturating_sub(2),
                            ),
                            meta_style,
                        ),
                    ]));
                }

                if !self.content.is_empty() {
                    let preview_lines: Vec<&str> = self.content.lines().collect();
                    let preview = preview_lines.first().copied().unwrap_or_default();
                    if !preview.is_empty() {
                        result.push(Line::from(vec![
                            Span::styled("│  ", Style::default().fg(theme.text_dimmed)),
                            Span::styled(
                                crate::text_utils::truncate_display(
                                    preview,
                                    width_budget.saturating_sub(2),
                                ),
                                Style::default().fg(theme.text_dimmed),
                            ),
                        ]));
                    }
                }

                for activity in recent_subagent_activity_lines(data) {
                    result.push(Line::from(vec![
                        Span::styled("│  ", Style::default().fg(theme.text_dimmed)),
                        Span::styled(
                            crate::text_utils::truncate_display(
                                &activity,
                                width_budget.saturating_sub(2),
                            ),
                            Style::default().fg(theme.text_dimmed),
                        ),
                    ]));
                }

                let has_conversation = data
                    .map(|d| !d.session_messages.is_empty())
                    .unwrap_or(false);
                if has_conversation || is_running {
                    let open_label = if is_running {
                        "Click to open live transcript"
                    } else {
                        "Click to open transcript"
                    };
                    result.push(Line::from(vec![
                        Span::styled("└─ ", Style::default().fg(theme.text_dimmed)),
                        Span::styled(open_label, Style::default().fg(theme.text_dimmed)),
                    ]));
                }

                result
            }
        };

        // Show streaming indicator with left bar
        if self.is_streaming {
            if let Some(last) = lines.last_mut() {
                let frame = inline_spinner_frame(spinner_tick);
                last.spans.push(Span::styled(
                    format!(" {frame}"),
                    Style::default().fg(theme.animated_accent(spinner_tick)),
                ));
            }
        }

        lines
    }
}
