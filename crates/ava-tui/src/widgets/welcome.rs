use crate::app::AppState;
use crate::widgets::safe_render::truncate_str;
use ratatui::layout::{Alignment, Constraint, Direction, Layout, Rect};
use ratatui::style::{Modifier, Style};
use ratatui::text::{Line, Span};
use ratatui::widgets::Paragraph;
use ratatui::Frame;

/// Version from workspace Cargo.toml.
const VERSION: &str = env!("CARGO_PKG_VERSION");

const ASCII_ART: &[&str] = &[
    "   _  __   __  _   ",
    "  /_\\ \\ \\ / / /_\\  ",
    " / _ \\ \\ V / / _ \\ ",
    "/_/ \\_\\ \\_/ /_/ \\_\\",
];

pub fn render_welcome(frame: &mut Frame<'_>, area: Rect, state: &AppState) {
    let theme = &state.theme;
    let cwd = std::env::current_dir()
        .map(|p| format_workspace_path(&p))
        .unwrap_or_else(|_| "unknown".to_string());

    let raw_model = format!("{}/{}", state.agent.provider_name, state.agent.model_name);
    let show_art = area.height >= 14 && area.width >= 30;
    // Truncate model and cwd to fit within a centered info block.
    let max_val = (area.width as usize).saturating_sub(20);
    let model_display = truncate_str(&raw_model, max_val);
    let cwd = truncate_str(&cwd, max_val);
    let mut lines: Vec<Line<'static>> = Vec::new();

    if show_art {
        for art_line in ASCII_ART {
            lines.push(Line::from(Span::styled(
                *art_line,
                Style::default().fg(theme.text).add_modifier(Modifier::BOLD),
            )));
        }
        lines.push(Line::from(""));
    } else {
        lines.push(Line::from(Span::styled(
            "AVA",
            Style::default().fg(theme.text).add_modifier(Modifier::BOLD),
        )));
    }

    lines.push(Line::from(Span::styled(
        "A focused AI workspace for coding in the terminal",
        Style::default().fg(theme.text_dimmed),
    )));
    lines.push(Line::from(Span::styled(
        format!("v{VERSION}"),
        Style::default().fg(theme.text_dimmed),
    )));

    // Design gap
    lines.push(Line::from(""));

    // Minimal context block
    let label_width = 7;
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

    let divider = "·".repeat(max_info_width.max(12));
    lines.push(Line::from(Span::styled(
        divider,
        Style::default().fg(theme.border),
    )));

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
    lines.push(Line::from(Span::styled(
        "·".repeat(max_info_width.max(12)),
        Style::default().fg(theme.border_subtle),
    )));

    // Vertical centering with a slight upward bias.
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

fn format_workspace_path(path: &std::path::Path) -> String {
    let parts: Vec<String> = path
        .components()
        .filter_map(|component| match component {
            std::path::Component::Normal(part) => Some(part.to_string_lossy().to_string()),
            _ => None,
        })
        .collect();

    if let Some(idx) = parts.iter().position(|part| part == "Personal") {
        return format!("/{}", parts[idx..].join("/"));
    }

    if parts.len() >= 2 {
        return format!("/{}/{}", parts[parts.len() - 2], parts[parts.len() - 1]);
    }

    path.display().to_string()
}
