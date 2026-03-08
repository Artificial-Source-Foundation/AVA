use crate::app::AppState;
use crate::state::voice::VoicePhase;
use ratatui::layout::Rect;
use ratatui::style::{Modifier, Style};
use ratatui::text::{Line, Span};
use ratatui::widgets::{Paragraph, Wrap};
use ratatui::Frame;

pub fn render_composer(frame: &mut Frame<'_>, area: Rect, state: &AppState) {
    let line = match state.voice.phase {
        VoicePhase::Recording => {
            let elapsed = state.voice.recording_duration();
            Line::from(vec![
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
                Span::styled(
                    "\u{276f} ",
                    Style::default().fg(state.theme.text_muted),
                ),
            ];
            if state.input.buffer.is_empty() {
                spans.push(Span::styled(
                    "\u{2588}",
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

    let widget = Paragraph::new(line)
        .wrap(Wrap { trim: false });
    frame.render_widget(widget, area);
}

/// Render the thin separator line above the composer.
pub fn render_separator(frame: &mut Frame<'_>, area: Rect, state: &AppState) {
    let sep = "\u{2500}".repeat(area.width as usize);
    let line = Line::from(Span::styled(
        sep,
        Style::default().fg(state.theme.border),
    ));
    let widget = Paragraph::new(line);
    frame.render_widget(widget, area);
}
