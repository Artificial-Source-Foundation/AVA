use crate::state::theme::Theme;
use ratatui::style::Style;
use ratatui::text::{Line, Span};
use similar::{ChangeTag, TextDiff};

pub fn render_diff(old: &str, new: &str, theme: &Theme) -> Vec<Line<'static>> {
    let diff = TextDiff::from_lines(old, new);
    let mut lines = Vec::new();

    for change in diff.iter_all_changes() {
        let (prefix, color) = match change.tag() {
            ChangeTag::Delete => ("-", theme.diff_removed),
            ChangeTag::Insert => ("+", theme.diff_added),
            ChangeTag::Equal => (" ", theme.diff_context),
        };
        let value = change.value().trim_end_matches('\n');
        lines.push(Line::from(Span::styled(
            format!("{}{}", prefix, value),
            Style::default().fg(color),
        )));
    }

    lines
}
