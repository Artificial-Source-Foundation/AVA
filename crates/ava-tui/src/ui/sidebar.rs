use crate::app::AppState;
use ratatui::layout::Rect;
use ratatui::style::Style;
use ratatui::text::{Line, Span};
use ratatui::widgets::{Block, Borders, Paragraph};
use ratatui::Frame;

pub fn render_sidebar(frame: &mut Frame<'_>, area: Rect, state: &AppState) {
    let session_label = state
        .session
        .current_session
        .as_ref()
        .map(|s| s.id.to_string()[..8].to_string())
        .unwrap_or_else(|| "none".to_string());

    let lines = vec![
        Line::from(Span::styled(
            "Session",
            Style::default().fg(state.theme.primary),
        )),
        Line::raw(format!("  {session_label}")),
        Line::raw(""),
        Line::from(Span::styled(
            "Provider",
            Style::default().fg(state.theme.primary),
        )),
        Line::raw(format!("  {}", state.agent.provider_name)),
        Line::raw(format!("  {}", state.agent.model_name)),
        Line::raw(""),
        Line::from(Span::styled(
            "Tokens",
            Style::default().fg(state.theme.primary),
        )),
        Line::raw(format!("  in:  {}", state.agent.tokens_used.input)),
        Line::raw(format!("  out: {}", state.agent.tokens_used.output)),
        Line::raw(""),
        Line::from(Span::styled(
            "Agent",
            Style::default().fg(state.theme.primary),
        )),
        Line::raw(format!(
            "  Turn {}/{}",
            state.agent.current_turn, state.agent.max_turns
        )),
        Line::raw(format!("  {}", state.agent.activity)),
        Line::raw(""),
        Line::from(Span::styled(
            "Messages",
            Style::default().fg(state.theme.primary),
        )),
        Line::raw(format!("  {} total", state.messages.messages.len())),
        Line::raw(""),
        Line::from(Span::styled(
            "Keybindings",
            Style::default().fg(state.theme.primary),
        )),
        Line::raw("  Ctrl+/  palette"),
        Line::raw("  Ctrl+D  quit"),
        Line::raw("  Ctrl+C  cancel"),
        Line::raw("  Ctrl+N  new session"),
        Line::raw("  Ctrl+B  sidebar"),
    ];

    let widget = Paragraph::new(lines).block(
        Block::default()
            .title("Info")
            .borders(Borders::ALL)
            .border_style(Style::default().fg(state.theme.border)),
    );
    frame.render_widget(widget, area);
}
