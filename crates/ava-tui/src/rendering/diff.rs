use crate::state::theme::Theme;
use ratatui::style::{Modifier, Style};
use ratatui::text::{Line, Span};
use similar::{ChangeTag, TextDiff};

pub fn render_diff(old: &str, new: &str, theme: &Theme) -> Vec<Line<'static>> {
    let diff = TextDiff::from_lines(old, new);
    let changes: Vec<_> = diff.iter_all_changes().collect();
    let mut lines = Vec::new();
    let mut i = 0;

    while i < changes.len() {
        let change = &changes[i];
        match change.tag() {
            ChangeTag::Equal => {
                let value = change.value().trim_end_matches('\n');
                lines.push(Line::from(Span::styled(
                    format!(" {value}"),
                    Style::default().fg(theme.diff_context),
                )));
                i += 1;
            }
            ChangeTag::Delete => {
                // Check if next change is an Insert (delete+insert pair for word-level diff)
                if i + 1 < changes.len() && changes[i + 1].tag() == ChangeTag::Insert {
                    let old_line = change.value().trim_end_matches('\n');
                    let new_line = changes[i + 1].value().trim_end_matches('\n');
                    let (del_spans, add_spans) =
                        word_level_spans(old_line, new_line, theme);
                    lines.push(Line::from(del_spans));
                    lines.push(Line::from(add_spans));
                    i += 2;
                } else {
                    let value = change.value().trim_end_matches('\n');
                    lines.push(Line::from(Span::styled(
                        format!("-{value}"),
                        Style::default().fg(theme.diff_removed),
                    )));
                    i += 1;
                }
            }
            ChangeTag::Insert => {
                let value = change.value().trim_end_matches('\n');
                lines.push(Line::from(Span::styled(
                    format!("+{value}"),
                    Style::default().fg(theme.diff_added),
                )));
                i += 1;
            }
        }
    }

    lines
}

fn word_level_spans(
    old_line: &str,
    new_line: &str,
    theme: &Theme,
) -> (Vec<Span<'static>>, Vec<Span<'static>>) {
    let word_diff = TextDiff::from_words(old_line, new_line);
    let mut del_spans: Vec<Span<'static>> = vec![Span::styled(
        "-".to_string(),
        Style::default().fg(theme.diff_removed),
    )];
    let mut add_spans: Vec<Span<'static>> = vec![Span::styled(
        "+".to_string(),
        Style::default().fg(theme.diff_added),
    )];

    for change in word_diff.iter_all_changes() {
        let value = change.value().to_string();
        match change.tag() {
            ChangeTag::Equal => {
                del_spans.push(Span::styled(
                    value.clone(),
                    Style::default().fg(theme.diff_removed),
                ));
                add_spans.push(Span::styled(
                    value,
                    Style::default().fg(theme.diff_added),
                ));
            }
            ChangeTag::Delete => {
                del_spans.push(Span::styled(
                    value,
                    Style::default()
                        .fg(theme.diff_removed_highlight)
                        .add_modifier(Modifier::BOLD),
                ));
            }
            ChangeTag::Insert => {
                add_spans.push(Span::styled(
                    value,
                    Style::default()
                        .fg(theme.diff_added_highlight)
                        .add_modifier(Modifier::BOLD),
                ));
            }
        }
    }

    (del_spans, add_spans)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn word_level_diff_produces_highlighted_spans() {
        let theme = Theme::default_theme();
        let old = "fn hello() {}";
        let new = "fn world() {}";
        let (del, add) = word_level_spans(old, new, &theme);
        // Should have prefix "-" + at least some spans
        assert!(del.len() >= 2);
        assert!(add.len() >= 2);
        // Check that bold modifier is present on changed words
        let bold_del = del.iter().any(|s| s.style.add_modifier.contains(Modifier::BOLD));
        let bold_add = add.iter().any(|s| s.style.add_modifier.contains(Modifier::BOLD));
        assert!(bold_del, "delete line should have bold highlighted word");
        assert!(bold_add, "add line should have bold highlighted word");
    }

    #[test]
    fn render_diff_equal_lines() {
        let theme = Theme::default_theme();
        let lines = render_diff("a\nb\n", "a\nb\n", &theme);
        assert_eq!(lines.len(), 2);
    }

    #[test]
    fn render_diff_paired_change_uses_word_level() {
        let theme = Theme::default_theme();
        let lines = render_diff("hello world\n", "hello rust\n", &theme);
        // Should produce 2 lines (word-level delete + insert pair)
        assert_eq!(lines.len(), 2);
        // First line should have multiple spans (word-level)
        assert!(lines[0].spans.len() > 1);
    }
}
