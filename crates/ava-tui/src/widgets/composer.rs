use crate::app::AppState;
use crate::state::voice::VoicePhase;
use ratatui::layout::{Constraint, Direction, Layout, Rect};
use ratatui::style::{Modifier, Style};
use ratatui::text::{Line, Span};
use ratatui::widgets::{Block, Paragraph, Wrap};
use ratatui::Frame;

/// Render the composer widget.
///
/// Design spec (Pencil):
///   Composer: bg=#1A1F2E, left bar 3px (#4D9EF6)
///   Content: padding=[12,16], gap=4, justify=center, layout=vertical
///     Line 1: ❯ (bold) + input text, gap=8
///     Line 2: provider (bold blue) + model name (muted), gap=12
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

    // -- Line 1: Prompt --
    let prompt_line = match state.voice.phase {
        VoicePhase::Recording => {
            let elapsed = state.voice.recording_duration();
            Line::from(vec![
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
            ])
        }
        VoicePhase::Transcribing => Line::from(vec![
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
        ]),
        VoicePhase::Idle => {
            let mut spans = vec![
                bar.clone(),
                Span::raw(pad),
                Span::styled(
                    "\u{276f} ",
                    Style::default()
                        .fg(state.theme.primary)
                        .add_modifier(Modifier::BOLD),
                ),
            ];
            if state.input.buffer.is_empty() {
                spans.push(Span::styled(
                    "Type a message...",
                    Style::default().fg(state.theme.text_dimmed),
                ));
            } else {
                spans.push(Span::styled(
                    state.input.buffer.clone(),
                    Style::default().fg(state.theme.text),
                ));
                spans.push(Span::styled(
                    "\u{2588}",
                    Style::default().fg(state.theme.text_muted),
                ));
            }
            Line::from(spans)
        }
    };

    // -- Line 2: Mode badge + model info --
    let mode_color = match state.agent_mode {
        crate::state::agent::AgentMode::Code => state.theme.success,
        crate::state::agent::AgentMode::Plan => state.theme.primary,
        crate::state::agent::AgentMode::Architect => state.theme.accent,
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

    // Center the 2 content lines vertically within the area
    // Design: padding=[12,16], justifyContent=center
    let content_lines = 2u16;
    let top_pad = area.height.saturating_sub(content_lines) / 2;
    let inner = Layout::default()
        .direction(Direction::Vertical)
        .constraints([
            Constraint::Length(top_pad),
            Constraint::Length(content_lines),
            Constraint::Min(0),
        ])
        .split(area)[1];

    let paragraph = Paragraph::new(vec![prompt_line, model_info_line])
        .style(Style::default().bg(state.theme.bg_elevated))
        .wrap(Wrap { trim: false });
    frame.render_widget(paragraph, inner);
}
