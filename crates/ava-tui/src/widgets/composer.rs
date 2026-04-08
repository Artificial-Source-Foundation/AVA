use crate::app::AppState;
use crate::state::input::InputState;
use crate::state::messages::UiMessage;
use crate::state::theme::Theme;
use crate::state::voice::VoicePhase;
use crate::text_utils::truncate_display;
use crate::widgets::safe_render::{to_static_line, to_static_lines};
use ratatui::layout::Rect;
use ratatui::style::{Modifier, Style};
use ratatui::text::{Line, Span};
use ratatui::widgets::{Block, Paragraph};
use ratatui::Frame;
use unicode_width::UnicodeWidthChar;

/// Split a line of text into spans, styling paste placeholders with accent color.
fn styled_text_spans<'a>(text: &str, input: &InputState, theme: &Theme) -> Vec<Span<'a>> {
    if input.pending_pastes.is_empty() {
        return vec![Span::styled(
            text.to_string(),
            Style::default().fg(theme.text),
        )];
    }

    let mut spans = Vec::new();
    let mut remaining = text;

    while !remaining.is_empty() {
        // Find the earliest placeholder in the remaining text
        let mut earliest: Option<(usize, &str)> = None;
        for placeholder in input.pending_pastes.keys() {
            if let Some(pos) = remaining.find(placeholder.as_str()) {
                if earliest.is_none() || earliest.is_some_and(|(ep, _)| pos < ep) {
                    earliest = Some((pos, placeholder.as_str()));
                }
            }
        }

        match earliest {
            Some((pos, placeholder)) => {
                // Text before the placeholder
                if pos > 0 {
                    spans.push(Span::styled(
                        remaining[..pos].to_string(),
                        Style::default().fg(theme.text),
                    ));
                }
                // The placeholder itself — styled distinctly
                spans.push(Span::styled(
                    placeholder.to_string(),
                    Style::default()
                        .fg(theme.accent)
                        .add_modifier(Modifier::ITALIC),
                ));
                remaining = &remaining[pos + placeholder.len()..];
            }
            None => {
                // No more placeholders
                spans.push(Span::styled(
                    remaining.to_string(),
                    Style::default().fg(theme.text),
                ));
                break;
            }
        }
    }

    spans
}

/// Render the composer widget.
///
/// Design spec (Pencil):
///   Composer: bg=#1A1F2E, left bar 3px (#4D9EF6)
///   Content: `padding=[12,16]`, gap=4, justify=center, layout=vertical
///     Line 1+: ❯ (bold) + input text (multi-line), gap=8
///     Last line: provider (bold blue) + model name (muted), gap=12
pub fn render_composer(frame: &mut Frame<'_>, area: Rect, state: &AppState) {
    let prompt_color = match state.voice.phase {
        VoicePhase::Recording => state.theme.error,
        VoicePhase::Transcribing => state.theme.accent,
        VoicePhase::Idle => {
            if state.active_modal.is_some() {
                state.theme.text_muted
            } else {
                state.theme.primary
            }
        }
    };
    let pad = "";

    // Build prompt lines (potentially multi-line)
    let prompt_lines: Vec<Line<'static>> = match state.voice.phase {
        VoicePhase::Recording => {
            let elapsed = state.voice.recording_duration();
            vec![Line::from(vec![
                Span::raw(pad),
                Span::styled("\u{276f} ", Style::default().fg(prompt_color)),
                Span::styled(
                    format!("Listening... ({elapsed:.1}s)"),
                    Style::default()
                        .fg(state.theme.accent)
                        .add_modifier(Modifier::ITALIC),
                ),
            ])]
        }
        VoicePhase::Transcribing => vec![Line::from(vec![
            Span::raw(pad),
            Span::styled("\u{276f} ", Style::default().fg(prompt_color)),
            Span::styled(
                "Transcribing...",
                Style::default()
                    .fg(state.theme.text_muted)
                    .add_modifier(Modifier::ITALIC),
            ),
        ])],
        VoicePhase::Idle => {
            if state.input.buffer.is_empty() {
                vec![Line::from(vec![
                    Span::raw(pad),
                    Span::styled(
                        "\u{276f} ",
                        Style::default()
                            .fg(prompt_color)
                            .add_modifier(Modifier::BOLD),
                    ),
                    Span::styled(
                        "Type a message",
                        Style::default().fg(state.theme.text_dimmed),
                    ),
                ])]
            } else {
                let (cursor_line, cursor_col) = state.input.cursor_line_col();
                let input_lines: Vec<&str> = state.input.buffer.split('\n').collect();
                let mut lines = Vec::with_capacity(input_lines.len());
                let wrap_width = area.width.saturating_sub(4) as usize;

                for (i, line_text) in input_lines.iter().enumerate() {
                    let prompt_char = if i == 0 { "❯ " } else { "  " };
                    let is_cursor_line = i == cursor_line;

                    let mut body_spans = Vec::new();

                    if is_cursor_line {
                        // Split text at cursor position to show block cursor
                        let col = cursor_col.min(line_text.len());
                        let before = &line_text[..col];
                        let after = &line_text[col..];

                        if !before.is_empty() {
                            body_spans.extend(styled_text_spans(
                                before,
                                &state.input,
                                &state.theme,
                            ));
                        }
                        // Block cursor character
                        if after.is_empty() {
                            body_spans.push(Span::styled(
                                "█",
                                Style::default().fg(state.theme.text_muted),
                            ));
                        } else {
                            // Show cursor on the next character
                            let mut char_end = 1;
                            while char_end < after.len() && !after.is_char_boundary(char_end) {
                                char_end += 1;
                            }
                            body_spans.push(Span::styled(
                                after[..char_end].to_string(),
                                Style::default()
                                    .fg(state.theme.bg_elevated)
                                    .bg(state.theme.text),
                            ));
                            if char_end < after.len() {
                                body_spans.extend(styled_text_spans(
                                    &after[char_end..],
                                    &state.input,
                                    &state.theme,
                                ));
                            }
                        }
                    } else {
                        body_spans.extend(styled_text_spans(line_text, &state.input, &state.theme));
                    }

                    let first_prefix = vec![
                        Span::raw(pad),
                        Span::styled(
                            prompt_char,
                            Style::default()
                                .fg(prompt_color)
                                .add_modifier(Modifier::BOLD),
                        ),
                    ];
                    let continuation_prefix = vec![Span::raw(pad), Span::raw("  ")];
                    lines.extend(wrap_prefixed_spans(
                        body_spans,
                        first_prefix,
                        continuation_prefix,
                        wrap_width,
                    ));
                }
                lines
            }
        }
    };

    // -- Model info line --
    // Clamp provider + model names so the line fits the composer width.
    let inner_w = area.width.saturating_sub(4) as usize;
    let mode_label = match state.agent_mode {
        crate::state::agent::AgentMode::Code => "Build".to_string(),
        crate::state::agent::AgentMode::Plan => "Plan".to_string(),
    };
    let prefix_len = 2 + mode_label.len() + 3;
    let remaining = inner_w.saturating_sub(prefix_len);
    let prov_max = remaining / 2;
    let model_max = remaining.saturating_sub(prov_max.min(state.agent.provider_name.len()) + 2);
    let prov_display = truncate_display(&state.agent.provider_name, prov_max);
    let model_display = truncate_display(&state.agent.model_name, model_max);
    let token_total = state.agent.tokens_used.input + state.agent.tokens_used.output;
    let token_text = if let Some(ctx) = state.agent.context_window {
        let pct = if ctx > 0 {
            (token_total as f64 / ctx as f64 * 100.0).round() as usize
        } else {
            0
        };
        format!("{} ({pct}%)", format_tokens(token_total))
    } else {
        format_tokens(token_total)
    };
    let thinking_text = if state.agent.model_supports_thinking()
        && state.agent.thinking_level != ava_types::ThinkingLevel::Off
    {
        Some(state.agent.thinking_level.label().to_lowercase())
    } else {
        None
    };

    let mode_style = match state.agent_mode {
        crate::state::agent::AgentMode::Code => Style::default()
            .fg(state.theme.primary)
            .add_modifier(Modifier::BOLD),
        crate::state::agent::AgentMode::Plan => Style::default()
            .fg(state.theme.text_muted)
            .add_modifier(Modifier::BOLD),
    };
    let compact = inner_w < 72;

    let mut left_spans = vec![
        Span::raw(pad),
        Span::styled(format!(" {mode_label} "), mode_style),
        Span::styled("  ", Style::default()),
        Span::styled(model_display.clone(), Style::default().fg(state.theme.text)),
    ];
    if !compact {
        left_spans.push(Span::styled("  ", Style::default()));
        left_spans.push(Span::styled(
            prov_display,
            Style::default().fg(state.theme.text_dimmed),
        ));
    }
    if compact {
        left_spans = vec![
            Span::raw(pad),
            Span::styled(format!(" {mode_label} "), mode_style),
            Span::styled("  ", Style::default()),
            Span::styled(model_display, Style::default().fg(state.theme.text)),
        ];
    }
    let mut right_spans: Vec<Span<'_>> = Vec::new();
    if state.agent.is_running {
        right_spans.push(Span::styled(
            crate::state::messages::spinner_frame(state.messages.spinner_tick).to_string(),
            Style::default().fg(state.theme.text_muted),
        ));
        right_spans.push(Span::styled("  ", Style::default()));
    }
    if let Some(thinking_text) = thinking_text {
        right_spans.push(Span::styled(
            format!(" {thinking_text} "),
            Style::default()
                .fg(state.theme.warning)
                .bg(state.theme.bg_surface)
                .add_modifier(Modifier::BOLD),
        ));
        right_spans.push(Span::styled("  ", Style::default()));
    }
    let utility_right = if state.input.queue_display.total_count() > 0 {
        format!("{} queued", state.input.queue_display.total_count())
    } else {
        token_text.clone()
    };
    right_spans.push(Span::styled(
        utility_right,
        Style::default().fg(state.theme.text_dimmed),
    ));
    let left_width: usize = left_spans
        .iter()
        .map(|s| crate::text_utils::span_display_width(s))
        .sum();
    let right_width: usize = right_spans
        .iter()
        .map(|s| crate::text_utils::span_display_width(s))
        .sum();
    let gap = inner_w.saturating_sub(left_width + right_width);
    let mut model_spans = left_spans;
    model_spans.push(Span::raw(" ".repeat(gap.max(2))));
    model_spans.extend(right_spans);
    let model_info_line = Line::from(model_spans);

    let panel = Block::default().style(Style::default().bg(state.theme.bg_elevated));
    frame.render_widget(panel, area);

    let inner_area = Rect {
        x: area.x + 2,
        y: area.y + 1,
        width: area.width.saturating_sub(4),
        height: area.height.saturating_sub(1),
    };

    if inner_area.width == 0 || inner_area.height == 0 {
        return;
    }

    // Prompt lines are already wrapped correctly by `wrap_prefixed_spans` and
    // must not go through the generic safe-width wrapper again.
    let mut extra_lines: Vec<Line<'static>> = Vec::new();

    // Show context attachments as badges
    if !state.input.attachments.is_empty() {
        for attachment in &state.input.attachments {
            let badge = match attachment {
                ava_types::ContextAttachment::File { .. } => "[@file]",
                ava_types::ContextAttachment::Folder { .. } => "[@folder]",
                ava_types::ContextAttachment::CodebaseQuery { .. } => "[@search]",
            };
            let label = attachment.label();
            let truncated = crate::text_utils::truncate_display_start(&label, 45);
            extra_lines.push(Line::from(vec![
                Span::raw(pad),
                Span::styled(
                    badge,
                    Style::default()
                        .fg(state.theme.accent)
                        .add_modifier(Modifier::BOLD),
                ),
                Span::styled(
                    format!(" {truncated}"),
                    Style::default().fg(state.theme.text_muted),
                ),
            ]));
        }
    }

    // Show pending image attachments as individual badges
    if state.pending_image_count > 0 {
        for i in 1..=state.pending_image_count {
            extra_lines.push(Line::from(vec![
                Span::raw(pad),
                Span::styled(
                    format!("[IMAGE {i}]"),
                    Style::default()
                        .fg(state.theme.accent)
                        .add_modifier(Modifier::BOLD),
                ),
            ]));
        }
    }

    // Show pending queued messages between input and model info
    if !state.input.queue_display.is_empty() {
        for item in &state.input.queue_display.items {
            let (badge, badge_color) = match &item.tier {
                ava_types::MessageTier::Steering => ("[S]", state.theme.warning),
                ava_types::MessageTier::FollowUp => ("[F]", state.theme.accent),
                ava_types::MessageTier::PostComplete { group } => {
                    // We can't return a &str from format!, but we handle it below
                    let _ = group;
                    ("[G]", state.theme.text_muted)
                }
            };
            let badge_text = match &item.tier {
                ava_types::MessageTier::PostComplete { group } => format!("[G{group}]"),
                _ => badge.to_string(),
            };
            let truncated = crate::text_utils::truncate_display(&item.text, 50);
            extra_lines.push(Line::from(vec![
                Span::raw(pad),
                Span::styled(
                    badge_text,
                    Style::default()
                        .fg(badge_color)
                        .add_modifier(Modifier::BOLD),
                ),
                Span::styled(
                    format!(" {truncated}"),
                    Style::default().fg(state.theme.text_dimmed),
                ),
            ]));
        }
    }

    let max_width = inner_area.width as usize;
    let mut wrapped_body_lines = prompt_lines;
    wrapped_body_lines.extend(to_static_lines(
        extra_lines
            .into_iter()
            .flat_map(|line| {
                let wrapped = UiMessage::wrap_line_spans(to_static_line(line).spans, max_width);
                if wrapped.is_empty() {
                    vec![Line::from(Vec::new())]
                } else {
                    wrapped.into_iter().map(Line::from).collect::<Vec<_>>()
                }
            })
            .collect(),
    ));
    let wrapped_meta_core: Vec<Line<'static>> = to_static_lines(
        vec![model_info_line]
            .into_iter()
            .flat_map(|line| {
                let wrapped = UiMessage::wrap_line_spans(to_static_line(line).spans, max_width);
                if wrapped.is_empty() {
                    vec![Line::from(Vec::new())]
                } else {
                    wrapped.into_iter().map(Line::from).collect::<Vec<_>>()
                }
            })
            .collect(),
    );
    let body_len = wrapped_body_lines.len();
    let meta_len = wrapped_meta_core.len();
    let can_afford_spacer = body_len + meta_len < inner_area.height as usize;
    let wrapped_meta_lines: Vec<Line<'static>> = if can_afford_spacer {
        let mut lines = vec![Line::from("")];
        lines.extend(wrapped_meta_core);
        lines
    } else {
        wrapped_meta_core
    };
    let reserved_meta = wrapped_meta_lines.len().min(inner_area.height as usize);
    let available_body = inner_area.height as usize - reserved_meta;
    let body_start = wrapped_body_lines.len().saturating_sub(available_body);
    let mut clamped_lines = wrapped_body_lines[body_start..].to_vec();
    clamped_lines.extend(wrapped_meta_lines.into_iter().take(reserved_meta));
    let paragraph =
        Paragraph::new(clamped_lines).style(Style::default().bg(state.theme.bg_elevated));
    frame.render_widget(paragraph, inner_area);
}

fn wrap_prefixed_spans(
    body_spans: Vec<Span<'_>>,
    first_prefix: Vec<Span<'static>>,
    continuation_prefix: Vec<Span<'static>>,
    max_width: usize,
) -> Vec<Line<'static>> {
    let first_prefix_width: usize = first_prefix.iter().map(actual_span_width).sum();
    let continuation_prefix_width: usize = continuation_prefix.iter().map(actual_span_width).sum();

    let body_spans: Vec<Span<'static>> = body_spans
        .into_iter()
        .map(|span| Span::styled(span.content.into_owned(), span.style))
        .collect();

    let mut rows: Vec<Line<'static>> = Vec::new();
    let mut current_row = first_prefix.clone();
    let mut current_width = first_prefix_width;
    let mut current_prefix_width = first_prefix_width;

    for span in body_spans {
        for ch in span.content.chars() {
            let ch_width = actual_char_width(ch);
            if current_width + ch_width > max_width && current_width > current_prefix_width {
                rows.push(Line::from(current_row));
                current_row = continuation_prefix.clone();
                current_width = continuation_prefix_width;
                current_prefix_width = continuation_prefix_width;
            }

            current_row.push(Span::styled(ch.to_string(), span.style));
            current_width += ch_width;
        }
    }

    if !current_row.is_empty() {
        rows.push(Line::from(current_row));
    }

    rows
}

fn actual_char_width(ch: char) -> usize {
    UnicodeWidthChar::width(ch).unwrap_or(0).max(1)
}

fn actual_span_width(span: &Span<'_>) -> usize {
    span.content.chars().map(actual_char_width).sum()
}

fn format_tokens(n: usize) -> String {
    if n >= 1_000_000 {
        format!("{:.1}M", n as f64 / 1_000_000.0)
    } else if n >= 1_000 {
        format!("{:.1}K", n as f64 / 1_000.0)
    } else {
        n.to_string()
    }
}
