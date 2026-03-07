use crate::rendering::diff::render_diff;
use crate::state::theme::Theme;
use ratatui::text::Line;

pub fn diff_preview_lines(old: &str, new: &str, theme: &Theme) -> Vec<Line<'static>> {
    render_diff(old, new, theme)
}
