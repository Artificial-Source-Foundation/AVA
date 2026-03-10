use crate::app::AppState;
use ratatui::layout::Rect;
use ratatui::style::{Modifier, Style};
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

    let label_style = Style::default()
        .fg(state.theme.text_muted)
        .add_modifier(Modifier::BOLD);
    let value_style = Style::default().fg(state.theme.text);
    let dim_style = Style::default().fg(state.theme.text_dimmed);

    let lines = vec![
        Line::from(""),
        Line::from(Span::styled("Session", label_style)),
        Line::from(Span::styled(format!("  {session_label}"), value_style)),
        Line::from(""),
        Line::from(Span::styled("Provider", label_style)),
        Line::from(Span::styled(format!("  {}", state.agent.provider_name), value_style)),
        Line::from(Span::styled(format!("  {}", state.agent.model_name), value_style)),
        Line::from(""),
        Line::from(Span::styled("Tokens", label_style)),
        Line::from(Span::styled(format!("  in:  {}", state.agent.tokens_used.input), value_style)),
        Line::from(Span::styled(format!("  out: {}", state.agent.tokens_used.output), value_style)),
        Line::from(""),
        Line::from(Span::styled("Agent", label_style)),
        Line::from(Span::styled(
            format!("  Turn {}/{}", state.agent.current_turn, state.agent.max_turns),
            value_style,
        )),
        Line::from(Span::styled(
            format!("  {}", state.agent.activity),
            value_style,
        )),
        Line::from(""),
        Line::from(Span::styled("Keys", label_style)),
        Line::from(Span::styled("  Ctrl+K  palette", dim_style)),
        Line::from(Span::styled("  Ctrl+M  model", dim_style)),
        Line::from(Span::styled("  Ctrl+N  new session", dim_style)),
        Line::from(Span::styled("  Ctrl+L  sessions", dim_style)),
        Line::from(Span::styled("  Ctrl+S  sidebar", dim_style)),
        Line::from(Span::styled("  Ctrl+C  cancel/quit", dim_style)),
    ];

    let widget = Paragraph::new(lines).block(
        Block::default()
            .borders(Borders::LEFT)
            .border_style(Style::default().fg(state.theme.border)),
    );
    frame.render_widget(widget, area);
}
