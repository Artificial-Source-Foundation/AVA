use crate::app::AppState;
use ratatui::layout::{Alignment, Constraint, Direction, Layout, Rect};
use ratatui::style::{Modifier, Style};
use ratatui::text::{Line, Span};
use ratatui::widgets::Paragraph;
use ratatui::Frame;

pub fn render_welcome(frame: &mut Frame<'_>, area: Rect, state: &AppState) {
    let cwd = std::env::current_dir()
        .map(|p| p.display().to_string())
        .unwrap_or_else(|_| "unknown".to_string());

    let lines = vec![
        Line::from(""),
        Line::from(Span::styled(
            "AVA",
            Style::default()
                .fg(state.theme.primary)
                .add_modifier(Modifier::BOLD),
        )),
        Line::from(Span::styled(
            "AI Coding Agent",
            Style::default().fg(state.theme.text_muted),
        )),
        Line::from(""),
        Line::from(vec![
            Span::styled("Model:    ", Style::default().fg(state.theme.text_muted)),
            Span::styled(
                format!("{}/{}", state.agent.provider_name, state.agent.model_name),
                Style::default().fg(state.theme.text),
            ),
        ]),
        Line::from(vec![
            Span::styled("Provider: ", Style::default().fg(state.theme.text_muted)),
            Span::styled(
                &state.agent.provider_name,
                Style::default().fg(state.theme.text),
            ),
        ]),
        Line::from(vec![
            Span::styled("CWD:      ", Style::default().fg(state.theme.text_muted)),
            Span::styled(cwd, Style::default().fg(state.theme.text)),
        ]),
        Line::from(""),
        Line::from(vec![
            Span::styled("Enter", Style::default().fg(state.theme.text)),
            Span::styled(" send", Style::default().fg(state.theme.text_muted)),
            Span::styled(" | ", Style::default().fg(state.theme.border)),
            Span::styled("Ctrl+/", Style::default().fg(state.theme.text)),
            Span::styled(" commands", Style::default().fg(state.theme.text_muted)),
            Span::styled(" | ", Style::default().fg(state.theme.border)),
            Span::styled("Ctrl+M", Style::default().fg(state.theme.text)),
            Span::styled(" model", Style::default().fg(state.theme.text_muted)),
            Span::styled(" | ", Style::default().fg(state.theme.border)),
            Span::styled("Ctrl+D", Style::default().fg(state.theme.text)),
            Span::styled(" quit", Style::default().fg(state.theme.text_muted)),
        ]),
    ];

    // Center vertically
    let content_height = lines.len() as u16;
    let vertical = Layout::default()
        .direction(Direction::Vertical)
        .constraints([
            Constraint::Min(0),
            Constraint::Length(content_height),
            Constraint::Min(0),
        ])
        .split(area);

    let widget = Paragraph::new(lines).alignment(Alignment::Center);
    frame.render_widget(widget, vertical[1]);
}
