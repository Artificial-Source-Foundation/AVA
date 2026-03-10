use crate::app::AppState;
use crate::widgets::todo_list;
use ava_types::TodoStatus;
use ratatui::layout::Rect;
use ratatui::style::{Color, Modifier, Style};
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

    let mut lines = vec![
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
    ];

    // Todo section — only show when there are incomplete items
    if todo_list::has_incomplete(&state.todo_items) {
        lines.push(Line::from(""));
        lines.push(Line::from(Span::styled("Todos", label_style)));

        let max_content_len = area.width.saturating_sub(6) as usize;

        for item in &state.todo_items {
            let (icon, style) = match item.status {
                TodoStatus::Completed => (
                    "\u{2713}",
                    Style::default()
                        .fg(Color::Green)
                        .add_modifier(Modifier::DIM),
                ),
                TodoStatus::InProgress => (
                    "\u{25CF}",
                    Style::default().fg(Color::Yellow),
                ),
                TodoStatus::Pending => (
                    "\u{25CB}",
                    Style::default().fg(state.theme.text),
                ),
                TodoStatus::Cancelled => (
                    "\u{2717}",
                    Style::default()
                        .fg(state.theme.text_dimmed)
                        .add_modifier(Modifier::DIM),
                ),
            };

            let priority_prefix = match item.priority {
                ava_types::TodoPriority::High => "! ",
                _ => "",
            };

            let display = if item.content.len() > max_content_len {
                format!("{}...", &item.content[..max_content_len.saturating_sub(3)])
            } else {
                item.content.clone()
            };

            if priority_prefix.is_empty() {
                lines.push(Line::from(vec![
                    Span::styled(format!("  {icon} "), style),
                    Span::styled(display, style),
                ]));
            } else {
                lines.push(Line::from(vec![
                    Span::styled(format!("  {icon} "), style),
                    Span::styled(priority_prefix.to_string(), Style::default().fg(Color::Red)),
                    Span::styled(display, style),
                ]));
            }
        }
    }

    lines.push(Line::from(""));
    lines.push(Line::from(Span::styled("Keys", label_style)));
    lines.push(Line::from(Span::styled("  Ctrl+K  palette", dim_style)));
    lines.push(Line::from(Span::styled("  Ctrl+M  model", dim_style)));
    lines.push(Line::from(Span::styled("  Ctrl+N  new session", dim_style)));
    lines.push(Line::from(Span::styled("  Ctrl+L  sessions", dim_style)));
    lines.push(Line::from(Span::styled("  Ctrl+S  sidebar", dim_style)));
    lines.push(Line::from(Span::styled("  Ctrl+C  cancel/quit", dim_style)));

    let widget = Paragraph::new(lines).block(
        Block::default()
            .borders(Borders::LEFT)
            .border_style(Style::default().fg(state.theme.border)),
    );
    frame.render_widget(widget, area);
}
