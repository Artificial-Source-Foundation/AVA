use crate::app::AppState;
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

pub fn render_welcome(frame: &mut Frame<'_>, area: Rect, state: &AppState) {
    let theme = &state.theme;
    let cwd = std::env::current_dir()
        .map(|p| p.display().to_string())
        .unwrap_or_else(|_| "unknown".to_string());

    let model_display = format!("{}/{}", state.agent.provider_name, state.agent.model_name);
    let show_art = area.height >= 14 && area.width >= 30;
    let show_shortcuts = area.height >= 18 && area.width >= 50;

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

    // Design gap: 24px → 1 blank line
    lines.push(Line::from(""));

    // 2. Subtitle
    lines.push(Line::from(Span::styled(
        "AI Coding Agent",
        Style::default().fg(theme.text_muted),
    )));

    // Design gap: 24px → 1 blank line
    lines.push(Line::from(""));

    // 3. Info block (design gap: 8px → adjacent, no blank line)
    // Right-justify labels: "Model" and "cwd" both get 6-char label columns.
    // Pad the shorter line so both have equal total width for correct center alignment.
    let label_width = 6; // "Model " and "  cwd " are both 6 chars
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

    // 4. Shortcuts section
    if show_shortcuts {
        // Design gap: 24px → 1 blank line
        lines.push(Line::from(""));

        lines.push(Line::from(Span::styled(
            "Keyboard Shortcuts",
            Style::default()
                .fg(theme.text_dimmed)
                .add_modifier(Modifier::BOLD),
        )));

        // Design gap: 12px → 1 blank line
        lines.push(Line::from(""));

        let key_style = Style::default().fg(theme.text).add_modifier(Modifier::BOLD);
        let desc_style = Style::default().fg(theme.text_muted);

        let grid: &[(&str, &str, &str, &str)] = &[
            ("Ctrl+K", "Command palette", "Ctrl+M", "Switch model"),
            ("Ctrl+S", "Switch session", "Ctrl+V", "Voice input"),
        ];

        // All grid rows must have the same total width for center alignment.
        // Full row: key1(8) + desc1(20) + key2(8) + desc2(max_d2)
        let max_d2_len = grid.iter().map(|(_, _, _, d2)| d2.len()).max().unwrap_or(0);
        let grid_row_width = 8 + 20 + 8 + max_d2_len;

        for (k1, d1, k2, d2) in grid {
            lines.push(Line::from(vec![
                Span::styled(format!("{k1:<8}"), key_style),
                Span::styled(format!("{d1:<20}"), desc_style),
                Span::styled(format!("{k2:<8}"), key_style),
                Span::styled(format!("{d2:<width$}", width = max_d2_len), desc_style),
            ]));
        }
        // Pad the half-width row to match full grid row width
        lines.push(Line::from(vec![
            Span::styled(format!("{:<8}", "Ctrl+N"), key_style),
            Span::styled(
                format!("{:<width$}", "New session", width = grid_row_width - 8),
                desc_style,
            ),
        ]));
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
