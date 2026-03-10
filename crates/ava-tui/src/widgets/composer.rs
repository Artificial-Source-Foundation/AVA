use crate::app::AppState;
use crate::state::input::InputState;
use crate::state::theme::Theme;
use crate::state::voice::VoicePhase;
use ratatui::layout::{Constraint, Direction, Layout, Rect};
use ratatui::style::{Modifier, Style};
use ratatui::text::{Line, Span};
use ratatui::widgets::{Block, Paragraph, Wrap};
use ratatui::Frame;

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
                if earliest.is_none() || pos < earliest.unwrap().0 {
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
///   Content: padding=[12,16], gap=4, justify=center, layout=vertical
///     Line 1+: ❯ (bold) + input text (multi-line), gap=8
///     Last line: provider (bold blue) + model name (muted), gap=12
pub fn render_composer(frame: &mut Frame<'_>, area: Rect, state: &AppState) {
    let bar_color = match state.voice.phase {
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

    // Design: 3px bar → 1 char full-block
    let bar = Span::styled("\u{258E}", Style::default().fg(bar_color));
    // Design: content padding 16px → 2 chars after bar
    let pad = "  ";

    // Build prompt lines (potentially multi-line)
    let prompt_lines: Vec<Line<'_>> = match state.voice.phase {
        VoicePhase::Recording => {
            let elapsed = state.voice.recording_duration();
            vec![Line::from(vec![
                bar.clone(),
                Span::raw(pad),
                Span::styled(
                    "\u{276f} ",
                    Style::default().fg(state.theme.accent),
                ),
                Span::styled(
                    format!("Listening... ({elapsed:.1}s)"),
                    Style::default()
                        .fg(state.theme.accent)
                        .add_modifier(Modifier::ITALIC),
                ),
            ])]
        }
        VoicePhase::Transcribing => vec![Line::from(vec![
            bar.clone(),
            Span::raw(pad),
            Span::styled(
                "\u{276f} ",
                Style::default().fg(state.theme.accent),
            ),
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
                    bar.clone(),
                    Span::raw(pad),
                    Span::styled(
                        "\u{276f} ",
                        Style::default()
                            .fg(state.theme.primary)
                            .add_modifier(Modifier::BOLD),
                    ),
                    Span::styled(
                        "Type a message... (Shift+Enter for newline)",
                        Style::default().fg(state.theme.text_dimmed),
                    ),
                ])]
            } else {
                let (cursor_line, cursor_col) = state.input.cursor_line_col();
                let input_lines: Vec<&str> = state.input.buffer.split('\n').collect();
                let mut lines = Vec::with_capacity(input_lines.len());

                for (i, line_text) in input_lines.iter().enumerate() {
                    let prompt_char = if i == 0 { "\u{276f} " } else { "  " };
                    let is_cursor_line = i == cursor_line;

                    let mut spans = vec![
                        bar.clone(),
                        Span::raw(pad),
                        Span::styled(
                            prompt_char,
                            Style::default()
                                .fg(state.theme.primary)
                                .add_modifier(Modifier::BOLD),
                        ),
                    ];

                    if is_cursor_line {
                        // Split text at cursor position to show block cursor
                        let col = cursor_col.min(line_text.len());
                        let before = &line_text[..col];
                        let after = &line_text[col..];

                        if !before.is_empty() {
                            spans.extend(styled_text_spans(before, &state.input, &state.theme));
                        }
                        // Block cursor character
                        if after.is_empty() {
                            spans.push(Span::styled(
                                "\u{2588}",
                                Style::default().fg(state.theme.text_muted),
                            ));
                        } else {
                            // Show cursor on the next character
                            let mut char_end = 1;
                            while char_end < after.len()
                                && !after.is_char_boundary(char_end)
                            {
                                char_end += 1;
                            }
                            spans.push(Span::styled(
                                after[..char_end].to_string(),
                                Style::default()
                                    .fg(state.theme.bg_elevated)
                                    .bg(state.theme.text),
                            ));
                            if char_end < after.len() {
                                spans.extend(styled_text_spans(&after[char_end..], &state.input, &state.theme));
                            }
                        }
                    } else {
                        spans.extend(styled_text_spans(line_text, &state.input, &state.theme));
                    }

                    lines.push(Line::from(spans));
                }
                lines
            }
        }
    };

    // -- Model info line --
    let mode_color = match state.agent_mode {
        crate::state::agent::AgentMode::Code => state.theme.success,
        crate::state::agent::AgentMode::Plan => state.theme.primary,
    };
    let model_info_line = Line::from(vec![
        bar,
        Span::raw(pad),
        Span::styled(
            format!("[{}]", state.agent_mode.label()),
            Style::default()
                .fg(mode_color)
                .add_modifier(Modifier::BOLD),
        ),
        Span::styled("  ", Style::default()),
        Span::styled(
            &state.agent.provider_name,
            Style::default()
                .fg(state.theme.primary)
                .add_modifier(Modifier::BOLD),
        ),
        Span::styled("  ", Style::default()),
        Span::styled(
            &state.agent.model_name,
            Style::default().fg(state.theme.text_muted),
        ),
    ]);

    // Fill entire composer area with bg_elevated first
    let bg = Block::default().style(Style::default().bg(state.theme.bg_elevated));
    frame.render_widget(bg, area);

    // Combine prompt lines + model info line
    let mut all_lines = prompt_lines;
    all_lines.push(model_info_line);

    let content_lines = all_lines.len() as u16;
    let top_pad = area.height.saturating_sub(content_lines) / 2;
    let inner = Layout::default()
        .direction(Direction::Vertical)
        .constraints([
            Constraint::Length(top_pad),
            Constraint::Length(content_lines),
            Constraint::Min(0),
        ])
        .split(area)[1];

    let paragraph = Paragraph::new(all_lines)
        .style(Style::default().bg(state.theme.bg_elevated))
        .wrap(Wrap { trim: false });
    frame.render_widget(paragraph, inner);
}
