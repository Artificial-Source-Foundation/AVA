use crate::app::AppState;
use ratatui::layout::Rect;
use ratatui::text::Line;
use ratatui::widgets::{Block, Borders, Paragraph};
use ratatui::Frame;

pub fn render_sidebar(frame: &mut Frame<'_>, area: Rect, state: &AppState) {
    let session_label = state
        .session
        .current_session
        .as_ref()
        .map(|s| s.id.to_string())
        .unwrap_or_else(|| "none".to_string());

    let lines = vec![
        Line::raw("Session"),
        Line::raw(format!("  {}", session_label)),
        Line::raw(""),
        Line::raw("Model"),
        Line::raw("  configured by stack"),
        Line::raw(format!("  tokens: {}", state.agent.tokens_used.output)),
        Line::raw(""),
        Line::raw("Agent Status"),
        Line::raw(format!(
            "  Turn {}/{}",
            state.agent.current_turn, state.agent.max_turns
        )),
        Line::raw(format!(
            "  {}",
            if state.agent.is_running {
                "Running"
            } else {
                "Idle"
            }
        )),
        Line::raw(""),
        Line::raw("Messages"),
        Line::raw(format!("  {} total", state.messages.messages.len())),
    ];

    let widget = Paragraph::new(lines).block(
        Block::default()
            .title("Sidebar")
            .borders(Borders::ALL)
            .border_style(ratatui::style::Style::default().fg(state.theme.border)),
    );
    frame.render_widget(widget, area);
}
