use ava_tui::rendering::diff::render_diff;
use ava_tui::rendering::markdown::markdown_to_lines;
use ava_tui::rendering::syntax::highlight_code;
use ava_tui::state::theme::Theme;

#[test]
fn markdown_renders_lines() {
    let theme = Theme::default_theme();
    let lines = markdown_to_lines("# title\nhello **world**", &theme);
    assert!(!lines.is_empty());
}

#[test]
fn syntax_highlighting_produces_lines() {
    let lines = highlight_code("fn main() {}", "rust", None);
    assert!(!lines.is_empty());
}

#[test]
fn diff_marks_added_removed_lines() {
    let theme = Theme::default_theme();
    let lines = render_diff("a\n", "b\n", &theme);
    let text = lines
        .iter()
        .flat_map(|l| l.spans.iter())
        .map(|s| s.content.as_ref())
        .collect::<String>();
    assert!(text.contains('-') || text.contains('+'));
}
