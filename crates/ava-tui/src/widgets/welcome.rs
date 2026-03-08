use crate::app::AppState;
use ratatui::layout::{Alignment, Constraint, Direction, Layout, Rect};
use ratatui::style::{Modifier, Style};
use ratatui::text::{Line, Span};
use ratatui::widgets::Paragraph;
use ratatui::Frame;

const ASCII_ART: &[&str] = &[
    "   _   __   __  _   ",
    "  /_\\ \\ \\ / / /_\\  ",
    " / _ \\ \\ V / / _ \\ ",
    "/_/ \\_\\ \\_/ /_/ \\_\\",
];

pub fn render_welcome(frame: &mut Frame<'_>, area: Rect, state: &AppState) {
    let cwd = std::env::current_dir()
        .map(|p| p.display().to_string())
        .unwrap_or_else(|_| "unknown".to_string());

    let show_art = area.width >= 40 && area.height >= 16;

    let mut lines: Vec<Line<'static>> = Vec::new();
    lines.push(Line::from(""));

    if show_art {
        for art_line in ASCII_ART {
            lines.push(Line::from(Span::styled(
                *art_line,
                Style::default()
                    .fg(state.theme.primary)
                    .add_modifier(Modifier::BOLD),
            )));
        }
        lines.push(Line::from(""));
    } else {
        lines.push(Line::from(Span::styled(
            "AVA",
            Style::default()
                .fg(state.theme.primary)
                .add_modifier(Modifier::BOLD),
        )));
    }

    lines.push(Line::from(Span::styled(
        "AI Coding Agent",
        Style::default().fg(state.theme.text_muted),
    )));
    lines.push(Line::from(""));

    // Info lines
    lines.push(Line::from(vec![
        Span::styled("Model     ", Style::default().fg(state.theme.text_dimmed)),
        Span::styled(
            format!("{}/{}", state.agent.provider_name, state.agent.model_name),
            Style::default().fg(state.theme.text),
        ),
    ]));
    lines.push(Line::from(vec![
        Span::styled("Directory ", Style::default().fg(state.theme.text_dimmed)),
        Span::styled(cwd, Style::default().fg(state.theme.text)),
    ]));
    lines.push(Line::from(""));

    // Hints
    let sep = Span::styled(
        " \u{2502} ",
        Style::default().fg(state.theme.border),
    );
    lines.push(Line::from(vec![
        Span::styled("Type a message to start", Style::default().fg(state.theme.text_muted)),
        sep.clone(),
        Span::styled("/", Style::default().fg(state.theme.text)),
        Span::styled(" commands", Style::default().fg(state.theme.text_muted)),
        sep,
        Span::styled("Ctrl+M", Style::default().fg(state.theme.text)),
        Span::styled(" model", Style::default().fg(state.theme.text_muted)),
    ]));

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
