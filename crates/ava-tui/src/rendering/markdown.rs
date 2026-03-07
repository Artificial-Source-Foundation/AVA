use crate::state::theme::Theme;
use pulldown_cmark::{Event, Parser, Tag};
use ratatui::style::{Modifier, Style};
use ratatui::text::{Line, Span};

pub fn markdown_to_lines(md: &str, theme: &Theme) -> Vec<Line<'static>> {
    let parser = Parser::new(md);
    let mut lines = Vec::new();
    let mut current: Vec<Span<'static>> = Vec::new();
    let mut styles = vec![Style::default().fg(theme.text)];

    for event in parser {
        match event {
            Event::Text(text) => {
                let style = *styles.last().unwrap_or(&Style::default());
                current.push(Span::styled(text.to_string(), style));
            }
            Event::Code(code) => {
                current.push(Span::styled(
                    format!("`{}`", code),
                    Style::default().fg(theme.accent),
                ));
            }
            Event::Start(Tag::Strong) => {
                styles.push(Style::default().add_modifier(Modifier::BOLD));
            }
            Event::Start(Tag::Emphasis) => {
                styles.push(Style::default().add_modifier(Modifier::ITALIC));
            }
            Event::Start(Tag::Heading { .. }) => {
                styles.push(
                    Style::default()
                        .fg(theme.primary)
                        .add_modifier(Modifier::BOLD),
                );
            }
            Event::End(_) => {
                if styles.len() > 1 {
                    styles.pop();
                }
            }
            Event::SoftBreak | Event::HardBreak => {
                lines.push(Line::from(std::mem::take(&mut current)));
            }
            _ => {}
        }
    }

    if !current.is_empty() {
        lines.push(Line::from(current));
    }
    lines
}
