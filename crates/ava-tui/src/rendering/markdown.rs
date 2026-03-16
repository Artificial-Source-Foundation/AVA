use crate::rendering::syntax::highlight_code;
use crate::state::theme::Theme;
use pulldown_cmark::{CodeBlockKind, Event, HeadingLevel, LinkType, Options, Parser, Tag, TagEnd};
use ratatui::style::{Modifier, Style};
use ratatui::text::{Line, Span};

pub fn markdown_to_lines(md: &str, theme: &Theme) -> Vec<Line<'static>> {
    let options = Options::ENABLE_STRIKETHROUGH;
    let parser = Parser::new_ext(md, options);
    let mut lines = Vec::new();
    let mut current: Vec<Span<'static>> = Vec::new();
    let mut styles = vec![Style::default().fg(theme.text)];

    // Code block state
    let mut in_code_block = false;
    let mut code_lang = String::new();
    let mut code_buf = String::new();

    // Blockquote nesting depth
    let mut blockquote_depth: u32 = 0;

    // Link state
    let mut link_url = String::new();

    // List nesting: each entry is Some(counter) for ordered, None for unordered
    let mut list_stack: Vec<Option<u64>> = Vec::new();

    for event in parser {
        if in_code_block {
            match event {
                Event::Text(text) => code_buf.push_str(&text),
                Event::End(TagEnd::CodeBlock) => {
                    // Fence markers with bg_elevated background
                    let fence_style = Style::default().fg(theme.text_dimmed).bg(theme.bg_elevated);
                    lines.push(Line::from(Span::styled(
                        format!("```{code_lang}"),
                        fence_style,
                    )));
                    // Syntax-highlighted code lines with bg_elevated background
                    let highlighted =
                        highlight_code(code_buf.trim_end(), &code_lang, Some(theme.bg_elevated));
                    lines.extend(highlighted);
                    lines.push(Line::from(Span::styled("```", fence_style)));
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
                if !current.is_empty() {
                    lines.push(finalize_line(&mut current, blockquote_depth, theme));
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
                // Inline code: accent foreground + elevated background
                current.push(Span::styled(
                    format!("`{code}`"),
                    Style::default().fg(theme.accent).bg(theme.bg_elevated),
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
            Event::Start(Tag::Strikethrough) => {
                styles.push(
                    styles
                        .last()
                        .copied()
                        .unwrap_or_default()
                        .add_modifier(Modifier::CROSSED_OUT),
                );
            }
            Event::Start(Tag::Heading { level, .. }) => {
                if !current.is_empty() {
                    lines.push(finalize_line(&mut current, blockquote_depth, theme));
                }
                let heading_style = match level {
                    HeadingLevel::H1 => Style::default()
                        .fg(theme.primary)
                        .add_modifier(Modifier::BOLD | Modifier::UNDERLINED),
                    HeadingLevel::H2 => Style::default()
                        .fg(theme.accent)
                        .add_modifier(Modifier::BOLD),
                    HeadingLevel::H3 => Style::default()
                        .fg(theme.primary)
                        .add_modifier(Modifier::BOLD),
                    _ => Style::default()
                        .fg(theme.text_muted)
                        .add_modifier(Modifier::BOLD),
                };
                styles.push(heading_style);
            }
            Event::Start(Tag::BlockQuote(_)) => {
                if !current.is_empty() {
                    lines.push(finalize_line(&mut current, blockquote_depth, theme));
                }
                blockquote_depth += 1;
                styles.push(
                    Style::default()
                        .fg(theme.success)
                        .add_modifier(Modifier::ITALIC),
                );
            }
            Event::End(TagEnd::BlockQuote(_)) => {
                if !current.is_empty() {
                    lines.push(finalize_line(&mut current, blockquote_depth, theme));
                }
                blockquote_depth = blockquote_depth.saturating_sub(1);
                if styles.len() > 1 {
                    styles.pop();
                }
            }
            Event::Start(Tag::Link {
                link_type,
                dest_url,
                ..
            }) => {
                if !matches!(link_type, LinkType::Autolink) {
                    link_url = dest_url.to_string();
                }
                // Links: accent color + underline
                styles.push(
                    Style::default()
                        .fg(theme.accent)
                        .add_modifier(Modifier::UNDERLINED),
                );
            }
            Event::End(TagEnd::Link) => {
                if !link_url.is_empty() {
                    current.push(Span::styled(
                        format!(" ({link_url})"),
                        Style::default().fg(theme.text_dimmed),
                    ));
                    link_url.clear();
                }
                if styles.len() > 1 {
                    styles.pop();
                }
            }
            Event::Start(Tag::List(first_number)) => {
                list_stack.push(first_number);
            }
            Event::End(TagEnd::List(_)) => {
                list_stack.pop();
            }
            Event::Start(Tag::Item) => {
                if !current.is_empty() {
                    lines.push(finalize_line(&mut current, blockquote_depth, theme));
                }
                // Indentation based on nesting depth (2 spaces per level)
                let depth = list_stack.len().saturating_sub(1);
                let indent = "  ".repeat(depth);
                let bullet_style = Style::default().fg(theme.text_muted);

                if let Some(Some(counter)) = list_stack.last_mut() {
                    // Ordered list: show number with right-aligned padding
                    current.push(Span::styled(
                        format!("{indent}{counter:>2}. "),
                        bullet_style,
                    ));
                    *counter += 1;
                } else {
                    // Unordered list: bullet
                    current.push(Span::styled(format!("{indent}  \u{2022} "), bullet_style));
                }
            }
            Event::Start(Tag::Paragraph) => {
                if !current.is_empty() {
                    lines.push(finalize_line(&mut current, blockquote_depth, theme));
                }
            }
            Event::End(TagEnd::Paragraph) => {
                if !current.is_empty() {
                    lines.push(finalize_line(&mut current, blockquote_depth, theme));
                }
            }
            Event::End(TagEnd::Heading(_)) => {
                if !current.is_empty() {
                    lines.push(finalize_line(&mut current, blockquote_depth, theme));
                }
                if styles.len() > 1 {
                    styles.pop();
                }
            }
            Event::End(TagEnd::Strong | TagEnd::Emphasis | TagEnd::Strikethrough) => {
                if styles.len() > 1 {
                    styles.pop();
                }
            }
            Event::End(_) => {
                // Don't pop for tags that didn't push (Item, List, etc.)
            }
            Event::SoftBreak | Event::HardBreak => {
                lines.push(finalize_line(&mut current, blockquote_depth, theme));
            }
            _ => {}
        }
    }

    if !current.is_empty() {
        lines.push(finalize_line(&mut current, blockquote_depth, theme));
    }

    if lines.is_empty() {
        lines.push(Line::raw(""));
    }

    lines
}

/// Finalize a line, adding blockquote prefix if needed.
fn finalize_line(
    current: &mut Vec<Span<'static>>,
    blockquote_depth: u32,
    theme: &Theme,
) -> Line<'static> {
    let spans = std::mem::take(current);
    if blockquote_depth > 0 {
        let mut prefixed = Vec::with_capacity(spans.len() + 1);
        let prefix = "\u{2502} ".repeat(blockquote_depth as usize);
        prefixed.push(Span::styled(prefix, Style::default().fg(theme.success)));
        prefixed.extend(spans);
        Line::from(prefixed)
    } else {
        Line::from(spans)
    }
}
