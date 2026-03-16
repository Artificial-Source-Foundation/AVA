use crate::app::AppState;
use crate::widgets::safe_render::truncate_str;
use ratatui::layout::{Alignment, Constraint, Direction, Layout, Rect};
use ratatui::style::{Modifier, Style};
use ratatui::text::{Line, Span};
use ratatui::widgets::Paragraph;
use ratatui::Frame;

/// ASCII art from Pencil design (4-line version).
const ASCII_ART: &[&str] = &[
    "   _   __   __  _   ",
    "  /_\\ \\ \\ / / /_\\  ",
    " / _ \\ \\ V / / _ \\ ",
    "/_/ \\_\\ \\_/ /_/ \\_\\",
];

/// Version from workspace Cargo.toml.
const VERSION: &str = env!("CARGO_PKG_VERSION");

pub fn render_welcome(frame: &mut Frame<'_>, area: Rect, state: &AppState) {
    let theme = &state.theme;
    let cwd = std::env::current_dir()
        .map(|p| p.display().to_string())
        .unwrap_or_else(|_| "unknown".to_string());

    let raw_model = format!("{}/{}", state.agent.provider_name, state.agent.model_name);
    // Truncate model and cwd to fit within area width (leave room for label)
    let max_val = (area.width as usize).saturating_sub(8); // 6-char label + 2 margin
    let model_display = truncate_str(&raw_model, max_val);
    let cwd = truncate_str(&cwd, max_val);
    let show_art = area.height >= 14 && area.width >= 30;
    let show_shortcuts = area.height >= 20 && area.width >= 50;
    let show_providers = area.height >= 16 && !state.configured_providers.is_empty();

    let mut lines: Vec<Line<'static>> = Vec::new();

    // 1. ASCII art logo
    if show_art {
        for art_line in ASCII_ART {
            lines.push(Line::from(Span::styled(
                *art_line,
                Style::default()
                    .fg(theme.primary)
                    .add_modifier(Modifier::BOLD),
            )));
        }
    } else {
        lines.push(Line::from(Span::styled(
            "A V A",
            Style::default()
                .fg(theme.primary)
                .add_modifier(Modifier::BOLD),
        )));
    }

    // Tagline + version
    lines.push(Line::from(Span::styled(
        "Multi-agent AI Coding Assistant",
        Style::default().fg(theme.text_muted),
    )));
    lines.push(Line::from(Span::styled(
        format!("v{VERSION}"),
        Style::default().fg(theme.text_dimmed),
    )));

    // Design gap
    lines.push(Line::from(""));

    // 2. Info block
    let label_width = 6;
    let model_line_content = format!(
        "{:>width$} {}",
        "Model",
        &model_display,
        width = label_width - 1
    );
    let cwd_line_content = format!("{:>width$} {}", "cwd", &cwd, width = label_width - 1);
    let model_line_len = model_line_content.len();
    let cwd_line_len = cwd_line_content.len();
    let max_info_width = model_line_len.max(cwd_line_len);

    lines.push(Line::from(vec![
        Span::styled(
            format!("{:>width$} ", "Model", width = label_width - 1),
            Style::default().fg(theme.text_dimmed),
        ),
        Span::styled(
            format!(
                "{:<width$}",
                model_display,
                width = max_info_width - label_width
            ),
            Style::default().fg(theme.text).add_modifier(Modifier::BOLD),
        ),
    ]));
    lines.push(Line::from(vec![
        Span::styled(
            format!("{:>width$} ", "cwd", width = label_width - 1),
            Style::default().fg(theme.text_dimmed),
        ),
        Span::styled(
            format!("{:<width$}", cwd, width = max_info_width - label_width),
            Style::default().fg(theme.text),
        ),
    ]));

    // 3. Configured providers
    if show_providers {
        lines.push(Line::from(""));

        let mut provider_spans: Vec<Span<'static>> = Vec::new();
        provider_spans.push(Span::styled(
            "Providers  ",
            Style::default().fg(theme.text_dimmed),
        ));
        for (i, name) in state.configured_providers.iter().enumerate() {
            if i > 0 {
                provider_spans.push(Span::styled("  ", Style::default().fg(theme.text_dimmed)));
            }
            provider_spans.push(Span::styled(name.clone(), Style::default().fg(theme.text)));
            provider_spans.push(Span::styled(
                " \u{2713}",
                Style::default().fg(theme.success),
            ));
        }
        lines.push(Line::from(provider_spans));
    }

    // 4. Shortcuts section
    if show_shortcuts {
        lines.push(Line::from(""));

        lines.push(Line::from(Span::styled(
            "Keyboard Shortcuts",
            Style::default()
                .fg(theme.text_dimmed)
                .add_modifier(Modifier::BOLD),
        )));

        lines.push(Line::from(""));

        let key_style = Style::default().fg(theme.text).add_modifier(Modifier::BOLD);
        let desc_style = Style::default().fg(theme.text_muted);

        // Two-column aligned grid
        let col1_key = 10; // key column width
        let col1_desc = 20; // description column width
        let col2_key = 10;

        let grid: &[(&str, &str, &str, &str)] = &[
            ("Ctrl+K", "Command palette", "Ctrl+M", "Switch model"),
            ("Ctrl+S", "Switch session", "Ctrl+N", "New session"),
            ("Ctrl+V", "Paste image", "", ""),
        ];

        for (k1, d1, k2, d2) in grid {
            let mut spans = vec![
                Span::styled(format!("{k1:<col1_key$}"), key_style),
                Span::styled(format!("{d1:<col1_desc$}"), desc_style),
            ];
            if !k2.is_empty() {
                spans.push(Span::styled(format!("{k2:<col2_key$}"), key_style));
                spans.push(Span::styled((*d2).to_string(), desc_style));
            }
            lines.push(Line::from(spans));
        }
    }

    // Vertical centering with upward bias (40% top / 60% bottom)
    let content_height = lines.len() as u16;
    let top_space = area.height.saturating_sub(content_height) * 2 / 5;

    let chunks = Layout::default()
        .direction(Direction::Vertical)
        .constraints([
            Constraint::Length(top_space),
            Constraint::Length(content_height),
            Constraint::Min(0),
        ])
        .split(area);

    let content = Paragraph::new(lines).alignment(Alignment::Center);
    frame.render_widget(content, chunks[1]);
}
