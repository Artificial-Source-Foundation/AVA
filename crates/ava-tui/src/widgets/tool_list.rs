use ava_tools::registry::ToolSource;
use ratatui::layout::Rect;
use ratatui::style::{Modifier, Style};
use ratatui::text::{Line, Span};
use ratatui::widgets::Paragraph;
use ratatui::Frame;

use crate::state::theme::Theme;

#[derive(Debug, Clone)]
pub struct ToolListItem {
    pub name: String,
    pub description: String,
    pub source: ToolSource,
}

#[derive(Default)]
pub struct ToolListState {
    pub items: Vec<ToolListItem>,
    pub selected: usize,
    pub query: String,
    pub scroll: usize,
}

impl ToolListState {
    pub fn filtered(&self) -> Vec<&ToolListItem> {
        if self.query.is_empty() {
            self.items.iter().collect()
        } else {
            let q = self.query.to_lowercase();
            self.items
                .iter()
                .filter(|item| {
                    item.name.to_lowercase().contains(&q)
                        || item.source.to_string().to_lowercase().contains(&q)
                })
                .collect()
        }
    }
}

pub fn render_tool_list(frame: &mut Frame<'_>, area: Rect, state: &ToolListState, theme: &Theme) {
    let filtered = state.filtered();

    let mut lines = vec![
        Line::from(vec![
            Span::styled("> ", Style::default().fg(theme.primary)),
            Span::styled(&state.query, Style::default().fg(theme.text)),
            Span::styled("_", Style::default().fg(theme.text_muted)),
        ]),
        Line::from(vec![Span::styled(
            format!("{} tools", filtered.len()),
            Style::default().fg(theme.text_muted),
        )]),
        Line::from(""),
    ];

    // Group tools by source
    let mut builtin = Vec::new();
    let mut mcp: std::collections::BTreeMap<String, Vec<&ToolListItem>> =
        std::collections::BTreeMap::new();
    let mut custom = Vec::new();

    for item in &filtered {
        match &item.source {
            ToolSource::BuiltIn => builtin.push(*item),
            ToolSource::MCP { server } => mcp.entry(server.clone()).or_default().push(*item),
            ToolSource::Custom { .. } => custom.push(*item),
        }
    }

    let mut flat_idx = 0usize;

    // Built-in tools
    if !builtin.is_empty() {
        lines.push(Line::from(Span::styled(
            "Built-in",
            Style::default()
                .fg(theme.secondary)
                .add_modifier(Modifier::BOLD),
        )));
        for item in &builtin {
            lines.push(tool_line(item, flat_idx == state.selected, theme));
            flat_idx += 1;
        }
        lines.push(Line::from(""));
    }

    // MCP tools grouped by server
    for (server, tools) in &mcp {
        lines.push(Line::from(Span::styled(
            format!("MCP: {server}"),
            Style::default()
                .fg(theme.secondary)
                .add_modifier(Modifier::BOLD),
        )));
        for item in tools {
            lines.push(tool_line(item, flat_idx == state.selected, theme));
            flat_idx += 1;
        }
        lines.push(Line::from(""));
    }

    // Custom tools
    if !custom.is_empty() {
        lines.push(Line::from(Span::styled(
            "Custom",
            Style::default()
                .fg(theme.secondary)
                .add_modifier(Modifier::BOLD),
        )));
        for item in &custom {
            lines.push(tool_line(item, flat_idx == state.selected, theme));
            flat_idx += 1;
        }
    }

    let widget = Paragraph::new(lines).scroll((state.scroll as u16, 0));
    frame.render_widget(widget, area);
}

fn tool_line<'a>(item: &ToolListItem, selected: bool, theme: &Theme) -> Line<'a> {
    let name_style = if selected {
        Style::default()
            .fg(theme.primary)
            .add_modifier(Modifier::BOLD)
    } else {
        Style::default().fg(theme.text)
    };
    let prefix = if selected { "> " } else { "  " };

    // Truncate description to keep it readable
    let desc = if item.description.len() > 60 {
        format!("{}...", &item.description[..57])
    } else {
        item.description.clone()
    };

    Line::from(vec![
        Span::styled(prefix.to_string(), Style::default().fg(theme.primary)),
        Span::styled(item.name.clone(), name_style),
        Span::styled(format!("  {desc}"), Style::default().fg(theme.text_muted)),
    ])
}
