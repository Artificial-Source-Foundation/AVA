use crate::state::btw::BtwState;
use crate::state::theme::Theme;
use ratatui::layout::{Constraint, Direction, Layout, Rect};
use ratatui::style::{Modifier, Style};
use ratatui::text::{Line, Span};
use ratatui::widgets::{Block, Borders, Clear, Paragraph, Wrap};
use ratatui::Frame;

/// Render the /btw side-question overlay on top of the main UI.
///
/// Shows a floating popup with the question, answer (or loading indicator),
/// and dismiss instructions. Styled with a subtle border to distinguish
/// it from regular modals.
pub fn render_btw_overlay(frame: &mut Frame<'_>, btw: &BtwState, theme: &Theme) {
    if btw.pending && btw.response.is_none() {
        // Show a small "thinking..." indicator at bottom-right
        render_pending_indicator(frame, theme);
        return;
    }

    let Some(ref response) = btw.response else {
        return;
    };

    let area = frame.area();
    let popup = centered_rect(55, 60, area);

    // Dimmed backdrop
    let backdrop = Block::default().style(
        Style::default().bg(ratatui::style::Color::Rgb(0, 0, 0)),
    );
    frame.render_widget(backdrop, area);

    // Clear and draw the overlay box
    frame.render_widget(Clear, popup);

    let block = Block::default()
        .title(Span::styled(
            " /btw ",
            Style::default()
                .fg(theme.accent)
                .add_modifier(Modifier::BOLD),
        ))
        .borders(Borders::ALL)
        .border_style(Style::default().fg(theme.border))
        .style(Style::default().bg(theme.bg_elevated));

    let inner = block.inner(popup);
    frame.render_widget(block, popup);

    // Layout: question (2-3 lines) + answer (fill) + footer (1 line)
    let chunks = Layout::default()
        .direction(Direction::Vertical)
        .constraints([
            Constraint::Length(3), // question
            Constraint::Min(1),   // answer
            Constraint::Length(1), // footer
        ])
        .split(inner);

    // Question header
    let question_text = format!("Q: {}", response.question);
    let question_lines = textwrap(&question_text, chunks[0].width as usize);
    let question_paragraph = Paragraph::new(
        question_lines
            .into_iter()
            .map(|l| {
                Line::from(Span::styled(
                    l,
                    Style::default()
                        .fg(theme.text)
                        .add_modifier(Modifier::BOLD),
                ))
            })
            .collect::<Vec<_>>(),
    );
    frame.render_widget(question_paragraph, chunks[0]);

    // Answer body
    let answer_paragraph = Paragraph::new(response.answer.as_str())
        .style(Style::default().fg(theme.text))
        .wrap(Wrap { trim: false });
    frame.render_widget(answer_paragraph, chunks[1]);

    // Footer
    let footer = Line::from(vec![
        Span::styled(
            "Space",
            Style::default()
                .fg(theme.text)
                .add_modifier(Modifier::BOLD),
        ),
        Span::styled(" / ", Style::default().fg(theme.text_muted)),
        Span::styled(
            "Enter",
            Style::default()
                .fg(theme.text)
                .add_modifier(Modifier::BOLD),
        ),
        Span::styled(" / ", Style::default().fg(theme.text_muted)),
        Span::styled(
            "Esc",
            Style::default()
                .fg(theme.text)
                .add_modifier(Modifier::BOLD),
        ),
        Span::styled(" to dismiss", Style::default().fg(theme.text_muted)),
    ]);
    frame.render_widget(Paragraph::new(footer), chunks[2]);
}

/// Show a small loading indicator while the btw query is in flight.
fn render_pending_indicator(frame: &mut Frame<'_>, theme: &Theme) {
    let area = frame.area();
    let width = 28u16;
    let height = 1u16;
    let x = area.right().saturating_sub(width + 2);
    let y = area.bottom().saturating_sub(height + 3);
    let indicator_area = Rect::new(x, y, width, height);

    let text = Line::from(vec![
        Span::styled("/btw ", Style::default().fg(theme.accent)),
        Span::styled(
            "thinking...",
            Style::default()
                .fg(theme.text_muted)
                .add_modifier(Modifier::ITALIC),
        ),
    ]);
    frame.render_widget(Paragraph::new(text), indicator_area);
}

fn centered_rect(percent_x: u16, percent_y: u16, r: Rect) -> Rect {
    let popup_layout = Layout::default()
        .direction(Direction::Vertical)
        .constraints([
            Constraint::Percentage((100 - percent_y) / 2),
            Constraint::Percentage(percent_y),
            Constraint::Percentage((100 - percent_y) / 2),
        ])
        .split(r);

    Layout::default()
        .direction(Direction::Horizontal)
        .constraints([
            Constraint::Percentage((100 - percent_x) / 2),
            Constraint::Percentage(percent_x),
            Constraint::Percentage((100 - percent_x) / 2),
        ])
        .split(popup_layout[1])[1]
}

/// Simple text wrapping by word boundaries.
fn textwrap(text: &str, width: usize) -> Vec<String> {
    if width == 0 || text.len() <= width {
        return vec![text.to_string()];
    }
    let mut lines = Vec::new();
    let mut current = String::new();
    for word in text.split_whitespace() {
        if current.is_empty() {
            current = word.to_string();
        } else if current.len() + 1 + word.len() <= width {
            current.push(' ');
            current.push_str(word);
        } else {
            lines.push(current);
            current = word.to_string();
        }
    }
    if !current.is_empty() {
        lines.push(current);
    }
    if lines.is_empty() {
        lines.push(String::new());
    }
    lines
}
