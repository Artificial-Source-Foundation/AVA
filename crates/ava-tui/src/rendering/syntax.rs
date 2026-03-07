use ratatui::style::{Color, Style};
use ratatui::text::{Line, Span};
use syntect::easy::HighlightLines;
use syntect::highlighting::{Color as SynColor, ThemeSet};
use syntect::parsing::SyntaxSet;

fn syntect_to_ratatui(color: SynColor) -> Color {
    Color::Rgb(color.r, color.g, color.b)
}

pub fn highlight_code(code: &str, language: &str) -> Vec<Line<'static>> {
    let syntax_set = SyntaxSet::load_defaults_newlines();
    let theme_set = ThemeSet::load_defaults();
    let syntax = syntax_set
        .find_syntax_by_token(language)
        .unwrap_or_else(|| syntax_set.find_syntax_plain_text());

    let theme = theme_set
        .themes
        .get("base16-ocean.dark")
        .or_else(|| theme_set.themes.values().next());
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
                .highlight_line(line, &syntax_set)
                .unwrap_or_default();
            let spans = ranges
                .into_iter()
                .map(|(style, text)| {
                    Span::styled(
                        text.to_string(),
                        Style::default().fg(syntect_to_ratatui(style.foreground)),
                    )
                })
                .collect::<Vec<_>>();
            Line::from(spans)
        })
        .collect()
}
