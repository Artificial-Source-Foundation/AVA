use crate::rendering::syntax::highlight_code;
use crate::state::theme::Theme;
use pulldown_cmark::{CodeBlockKind, Event, Parser, Tag, TagEnd};
use ratatui::style::{Modifier, Style};
use ratatui::text::{Line, Span};

pub fn markdown_to_lines(md: &str, theme: &Theme) -> Vec<Line<'static>> {
    let parser = Parser::new(md);
    let mut lines = Vec::new();
    let mut current: Vec<Span<'static>> = Vec::new();
    let mut styles = vec![Style::default().fg(theme.text)];

    // Code block state
    let mut in_code_block = false;
    let mut code_lang = String::new();
    let mut code_buf = String::new();

    for event in parser {
        if in_code_block {
            match event {
                Event::Text(text) => code_buf.push_str(&text),
                Event::End(TagEnd::CodeBlock) => {
                    // Render code block with syntax highlighting
                    lines.push(Line::from(Span::styled(
                        format!("```{code_lang}"),
                        Style::default().fg(theme.text_muted),
                    )));
                    let highlighted = highlight_code(code_buf.trim_end(), &code_lang);
                    lines.extend(highlighted);
                    lines.push(Line::from(Span::styled(
                        "```",
                        Style::default().fg(theme.text_muted),
                    )));
                    in_code_block = false;
                    code_lang.clear();
                    code_buf.clear();
                }
                _ => {}
            }
            continue;
        }

        match event {
            Event::Start(Tag::CodeBlock(kind)) => {
                // Flush current line
                if !current.is_empty() {
                    lines.push(Line::from(std::mem::take(&mut current)));
                }
                in_code_block = true;
                code_lang = match kind {
                    CodeBlockKind::Fenced(lang) => lang.to_string(),
                    CodeBlockKind::Indented => String::new(),
                };
            }
            Event::Text(text) => {
                let style = *styles.last().unwrap_or(&Style::default());
                current.push(Span::styled(text.to_string(), style));
            }
            Event::Code(code) => {
                current.push(Span::styled(
                    format!("`{code}`"),
                    Style::default().fg(theme.accent),
                ));
            }
            Event::Start(Tag::Strong) => {
                styles.push(
                    styles
                        .last()
                        .copied()
                        .unwrap_or_default()
                        .add_modifier(Modifier::BOLD),
                );
            }
            Event::Start(Tag::Emphasis) => {
                styles.push(
                    styles
                        .last()
                        .copied()
                        .unwrap_or_default()
                        .add_modifier(Modifier::ITALIC),
                );
            }
            Event::Start(Tag::Heading { .. }) => {
                // Flush current line
                if !current.is_empty() {
                    lines.push(Line::from(std::mem::take(&mut current)));
                }
                styles.push(
                    Style::default()
                        .fg(theme.primary)
                        .add_modifier(Modifier::BOLD),
                );
            }
            Event::Start(Tag::Item) => {
                if !current.is_empty() {
                    lines.push(Line::from(std::mem::take(&mut current)));
                }
                current.push(Span::styled(
                    "  • ",
                    Style::default().fg(theme.text_muted),
                ));
            }
            Event::Start(Tag::Paragraph) => {
                if !current.is_empty() {
                    lines.push(Line::from(std::mem::take(&mut current)));
                }
            }
            Event::End(TagEnd::Paragraph) => {
                if !current.is_empty() {
                    lines.push(Line::from(std::mem::take(&mut current)));
                }
            }
            Event::End(TagEnd::Heading(_)) => {
                if !current.is_empty() {
                    lines.push(Line::from(std::mem::take(&mut current)));
                }
                if styles.len() > 1 {
                    styles.pop();
                }
            }
            Event::End(TagEnd::Strong) | Event::End(TagEnd::Emphasis) => {
                if styles.len() > 1 {
                    styles.pop();
                }
            }
            Event::End(_) => {
                // Don't pop for tags that didn't push (Paragraph, Item, List, etc.)
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

    // Ensure at least one line
    if lines.is_empty() {
        lines.push(Line::raw(""));
    }

    lines
}
