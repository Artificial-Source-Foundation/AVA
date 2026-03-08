use crate::app::AppState;
use crate::state::voice::VoicePhase;
use ratatui::layout::Rect;
use ratatui::style::Style;
use ratatui::widgets::{Block, Borders, Paragraph};
use ratatui::Frame;

pub fn render_composer(frame: &mut Frame<'_>, area: Rect, state: &AppState) {
    let (content, style) = match state.voice.phase {
        VoicePhase::Recording => {
            let elapsed = state.voice.recording_duration();
            (
                format!("Listening... ({elapsed:.1}s)"),
                Style::default().fg(state.theme.accent),
            )
        }
        VoicePhase::Transcribing => (
            "Transcribing...".to_string(),
            Style::default().fg(state.theme.text_muted),
        ),
        VoicePhase::Idle => (
            state.input.buffer.clone(),
            Style::default().fg(state.theme.text),
        ),
    };

    let widget = Paragraph::new(content)
        .style(style)
        .block(Block::default().title("Composer").borders(Borders::ALL));
    frame.render_widget(widget, area);
}
