use crate::app::AppState;
use ratatui::layout::Rect;
use ratatui::style::Style;
use ratatui::widgets::Paragraph;
use ratatui::Frame;

pub fn render_top(frame: &mut Frame<'_>, area: Rect, state: &AppState) {
    let session = state
        .session
        .current_session
        .as_ref()
        .map(|s| s.id.to_string())
        .unwrap_or_else(|| "none".to_string());

    let text = format!(
        "AVA v0.1.0 | theme: {} | tokens out: {} | session: {}",
        state.theme.name, state.agent.tokens_used.output, session
    );
    let widget = Paragraph::new(text).style(Style::default().fg(state.theme.text));
    frame.render_widget(widget, area);
}

pub fn render_bottom(frame: &mut Frame<'_>, area: Rect, state: &AppState) {
    let status = if state.agent.is_running {
        "running"
    } else {
        "idle"
    };

    let text = format!(
        "turn {}/{} | {} | Ctrl+/ palette | Ctrl+D quit",
        state.agent.current_turn, state.agent.max_turns, status
    );
    let widget = Paragraph::new(text).style(Style::default().fg(state.theme.text_muted));
    frame.render_widget(widget, area);
}
