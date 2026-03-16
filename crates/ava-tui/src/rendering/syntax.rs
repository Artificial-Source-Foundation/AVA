use ratatui::style::{Color, Style};
use ratatui::text::{Line, Span};
use std::sync::LazyLock;
use syntect::easy::HighlightLines;
use syntect::highlighting::{Color as SynColor, ThemeSet};
use syntect::parsing::SyntaxSet;

static SYNTAX_SET: LazyLock<SyntaxSet> = LazyLock::new(SyntaxSet::load_defaults_newlines);
static THEME_SET: LazyLock<ThemeSet> = LazyLock::new(ThemeSet::load_defaults);

fn syntect_to_ratatui(color: SynColor) -> Color {
    Color::Rgb(color.r, color.g, color.b)
}

/// Highlight code with syntax coloring. If `bg` is `Some`, every span gets that background.
pub fn highlight_code(code: &str, language: &str, bg: Option<Color>) -> Vec<Line<'static>> {
    let syntax = SYNTAX_SET
        .find_syntax_by_token(language)
        .unwrap_or_else(|| SYNTAX_SET.find_syntax_plain_text());

    let theme = THEME_SET
        .themes
        .get("base16-ocean.dark")
        .or_else(|| THEME_SET.themes.values().next());
    let Some(theme) = theme else {
        return code
            .lines()
            .map(|line| Line::raw(line.to_string()))
            .collect();
    };

    let mut highlighter = HighlightLines::new(syntax, theme);
    code.lines()
        .map(|line| {
            let ranges = highlighter
                .highlight_line(line, &SYNTAX_SET)
                .unwrap_or_default();
            let spans = ranges
                .into_iter()
                .map(|(style, text)| {
                    let mut s = Style::default().fg(syntect_to_ratatui(style.foreground));
                    if let Some(bg_color) = bg {
                        s = s.bg(bg_color);
                    }
                    Span::styled(text.to_string(), s)
                })
                .collect::<Vec<_>>();
            Line::from(spans)
        })
        .collect()
}
