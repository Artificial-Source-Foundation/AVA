use crate::app::{AppState, SidebarClickAction, SidebarClickTarget};
use crate::text_utils::truncate_display;
use crate::widgets::safe_render::{clamp_line, to_static_lines};
use crate::widgets::todo_list;
use ava_agent::stack::McpServerStatus;
use ava_types::TodoStatus;
use ratatui::layout::Rect;
use ratatui::style::{Modifier, Style};
use ratatui::text::{Line, Span};
use ratatui::widgets::{Block, Paragraph};
use ratatui::Frame;

fn is_hovered(state: &AppState, x: std::ops::Range<u16>, y: std::ops::Range<u16>) -> bool {
    state
        .mouse_position
        .map(|mouse| x.contains(&mouse.column) && y.contains(&mouse.row))
        .unwrap_or(false)
}

fn hover_bg(state: &AppState, hovered: bool) -> Style {
    if hovered {
        Style::default().bg(state.theme.bg_elevated)
    } else {
        Style::default()
    }
}

pub fn render_sidebar(frame: &mut Frame<'_>, area: Rect, state: &mut AppState) {
    state.sidebar_click_targets.clear();
    let inner_area = Rect {
        x: area.x + 2,
        y: area.y + 1,
        width: area.width.saturating_sub(4),
        height: area.height.saturating_sub(2),
    };
    if inner_area.width == 0 || inner_area.height == 0 {
        frame.render_widget(
            Block::default().style(Style::default().bg(state.theme.bg_surface)),
            area,
        );
        return;
    }

    let cwd = std::env::current_dir()
        .map(|path| format_workspace_path(&path, inner_area.width.saturating_sub(2) as usize))
        .unwrap_or_else(|_| "unknown".to_string());
    let session_label = state
        .session
        .current_session
        .as_ref()
        .map(|s| s.id.to_string()[..8].to_string())
        .unwrap_or_else(|| "none".to_string());

    let label_style = Style::default().fg(state.theme.text_dimmed);
    let value_style = Style::default().fg(state.theme.text);
    let dim_style = Style::default().fg(state.theme.text_dimmed);
    let accent_style = Style::default()
        .fg(state.theme.text)
        .add_modifier(Modifier::BOLD);

    // Maximum display width for value text (account for border + indent)
    let inner_w = inner_area.width.saturating_sub(1) as usize;

    let provider_display = truncate_display(&state.agent.provider_name, inner_w.saturating_sub(2));
    let model_display = truncate_display(&state.agent.model_name, inner_w.saturating_sub(2));
    let used_tokens = state.agent.tokens_used.input + state.agent.tokens_used.output;
    let context_summary = if let Some(ctx) = state.agent.context_window {
        format!("  {} / {} tokens", used_tokens, ctx)
    } else {
        format!("  {} tokens", used_tokens)
    };
    let context_pct = state.agent.context_window.map(|ctx| {
        let pct = if ctx > 0 {
            ((used_tokens as f64 / ctx as f64) * 100.0).round() as usize
        } else {
            0
        };
        format!("  {}% used", pct)
    });
    let cost_summary = format!("  ${:.2} spent", state.agent.cost);

    let mut lines = vec![
        Line::from(Span::styled("workspace", accent_style)),
        Line::from(Span::styled(format!("  {cwd}"), dim_style)),
        Line::from(""),
        Line::from(Span::styled("session", label_style)),
        Line::from(Span::styled(format!("  {session_label}"), value_style)),
        Line::from(""),
        Line::from(Span::styled("model", label_style)),
        Line::from(Span::styled(format!("  {provider_display}"), value_style)),
        Line::from(Span::styled(format!("  {model_display}"), value_style)),
        Line::from(""),
        Line::from(Span::styled("context", label_style)),
        Line::from(Span::styled(context_summary, value_style)),
    ];
    if let Some(context_pct) = context_pct {
        lines.push(Line::from(Span::styled(context_pct, dim_style)));
    }
    lines.extend([
        Line::from(Span::styled(cost_summary, dim_style)),
        Line::from(""),
        Line::from(Span::styled(
            format!("  session  {session_label}"),
            dim_style,
        )),
    ]);

    lines.push(Line::from(""));
    lines.push(Line::from(vec![
        Span::styled("mcp", label_style.add_modifier(Modifier::BOLD)),
        Span::styled(
            if state.feature_mcp_enabled {
                format!(
                    "  ({} active, {} tools)",
                    state.agent.mcp_server_count, state.agent.mcp_tool_count
                )
            } else {
                "  disabled".to_string()
            },
            dim_style,
        ),
    ]));
    if state.feature_mcp_enabled {
        for server in state.mcp_servers.iter().take(6) {
            let (marker_style, status_text) = match &server.status {
                McpServerStatus::Connected => (
                    Style::default().fg(state.theme.success),
                    "connected".to_string(),
                ),
                McpServerStatus::Connecting => (
                    Style::default().fg(state.theme.warning),
                    "connecting".to_string(),
                ),
                McpServerStatus::Disabled => (
                    Style::default().fg(state.theme.text_dimmed),
                    "disabled".to_string(),
                ),
                McpServerStatus::Failed(err) => (
                    Style::default().fg(state.theme.error),
                    format!(
                        "error: {}",
                        truncate_display(err, inner_w.saturating_sub(16))
                    ),
                ),
            };
            let click_row = inner_area.y + lines.len() as u16;
            let hover = is_hovered(
                state,
                inner_area.x..(inner_area.x + inner_area.width),
                click_row..(click_row + 2),
            );
            let action_glyph = if server.enabled { "−" } else { "+" };
            let action_style = if hover {
                Style::default()
                    .fg(state.theme.text)
                    .bg(state.theme.bg_elevated)
                    .add_modifier(Modifier::BOLD)
            } else {
                Style::default().fg(state.theme.text_muted)
            };
            let row_style = hover_bg(state, hover);

            lines.push(Line::from(vec![
                Span::styled(format!("  {action_glyph} "), action_style),
                Span::styled("• ", marker_style.patch(row_style)),
                Span::styled(
                    truncate_display(&server.name, inner_w.saturating_sub(8)),
                    if hover {
                        value_style.patch(row_style).add_modifier(Modifier::BOLD)
                    } else {
                        value_style.patch(row_style)
                    },
                ),
            ]));
            state.sidebar_click_targets.push(SidebarClickTarget {
                x: inner_area.x..(inner_area.x + inner_area.width),
                y: click_row..(click_row + 2),
                action: SidebarClickAction::ToggleMcpServer {
                    name: server.name.clone(),
                    enabled: server.enabled,
                },
            });
            if hover
                || matches!(
                    server.status,
                    McpServerStatus::Failed(_) | McpServerStatus::Connecting
                )
            {
                lines.push(Line::from(Span::styled(
                    format!(
                        "    {}  {} tools  {}",
                        status_text,
                        server.tool_count,
                        format!("{:?}", server.scope).to_lowercase()
                    ),
                    dim_style.patch(row_style),
                )));
            }
        }
        if state.mcp_servers.len() > 6 {
            lines.push(Line::from(Span::styled(
                format!("  +{} more", state.mcp_servers.len() - 6),
                dim_style,
            )));
        }
    }

    lines.push(Line::from(""));
    let lsp_header_row = inner_area.y + lines.len() as u16;
    lines.push(Line::from(vec![
        Span::styled("lsp", label_style.add_modifier(Modifier::BOLD)),
        Span::styled(
            if state.feature_lsp_enabled {
                if state.lsp_entries.is_empty() {
                    "  enabled".to_string()
                } else {
                    format!("  {} detected", state.lsp_entries.len())
                }
            } else {
                "  disabled".to_string()
            },
            dim_style,
        ),
    ]));
    state.sidebar_click_targets.push(SidebarClickTarget {
        x: inner_area.x..(inner_area.x + inner_area.width),
        y: lsp_header_row..(lsp_header_row + 1),
        action: SidebarClickAction::RefreshLsp,
    });
    if state.feature_lsp_enabled {
        if state.lsp_entries.is_empty() {
            lines.push(Line::from(Span::styled(
                "  no project tooling detected",
                dim_style,
            )));
        } else {
            for tool in state.lsp_entries.iter().take(6) {
                let click_row = inner_area.y + lines.len() as u16;
                let hover = is_hovered(
                    state,
                    inner_area.x..(inner_area.x + inner_area.width),
                    click_row..(click_row + 2),
                );
                let status_style = if tool.status == "connected" {
                    Style::default().fg(state.theme.success)
                } else if tool.status == "idle" {
                    Style::default().fg(state.theme.text_muted)
                } else if tool.status == "restarting" {
                    Style::default().fg(state.theme.warning)
                } else if tool.status == "failed" {
                    Style::default().fg(state.theme.error)
                } else {
                    Style::default().fg(state.theme.warning)
                };
                let row_style = hover_bg(state, hover);
                lines.push(Line::from(vec![
                    Span::styled(
                        if hover { "  ↻ " } else { "  • " },
                        status_style.patch(row_style),
                    ),
                    Span::styled(
                        tool.name.clone(),
                        if hover {
                            value_style.patch(row_style).add_modifier(Modifier::BOLD)
                        } else {
                            value_style.patch(row_style)
                        },
                    ),
                ]));
                state.sidebar_click_targets.push(SidebarClickTarget {
                    x: inner_area.x..(inner_area.x + inner_area.width),
                    y: click_row..(click_row + 2),
                    action: SidebarClickAction::RefreshLsp,
                });
                if hover || tool.status == "restarting" || tool.status == "failed" {
                    lines.push(Line::from(Span::styled(
                        format!("    {}  {}", tool.status, tool.detail),
                        dim_style.patch(row_style),
                    )));
                }
            }
            if state.lsp_entries.len() > 6 {
                lines.push(Line::from(Span::styled(
                    format!("  +{} more", state.lsp_entries.len() - 6),
                    dim_style,
                )));
            }
        }

        lines.push(Line::from(""));
        lines.push(Line::from(Span::styled(
            "  click to toggle or refresh",
            Style::default().fg(state.theme.text_dimmed),
        )));
    }

    // Sub-agents section — only show when there are any sub-agents
    if !state.agent.sub_agents.is_empty() {
        lines.push(Line::from(""));
        lines.push(Line::from(Span::styled("sub-agents", label_style)));

        let max_desc_len = inner_area.width.saturating_sub(4) as usize;
        let max_visible = 5;
        let total = state.agent.sub_agents.len();
        let skip = total.saturating_sub(max_visible);

        // Show "+N more" indicator when there are hidden sub-agents
        if skip > 0 {
            lines.push(Line::from(Span::styled(
                format!("  +{skip} more"),
                Style::default().fg(state.theme.text_dimmed),
            )));
        }

        for sa in state.agent.sub_agents.iter().skip(skip) {
            let desc = truncate_display(&sa.description, max_desc_len);

            // [CC] badge for claude-code-powered sub-agents
            let cc_badge: Vec<Span<'_>> = if sa.provider.as_deref() == Some("claude-code") {
                vec![Span::styled(
                    "[CC] ",
                    Style::default()
                        .fg(state.theme.secondary)
                        .add_modifier(Modifier::BOLD),
                )]
            } else {
                vec![]
            };

            if sa.is_running {
                // Spinning indicator for running sub-agents
                let elapsed = sa.started_at.elapsed().as_secs();
                let spinner = match elapsed % 4 {
                    0 => "|",
                    1 => "/",
                    2 => "-",
                    _ => "\\",
                };
                let stats = if sa.tool_count > 0 {
                    format!(" ({} tools, {}s)", sa.tool_count, elapsed)
                } else {
                    format!(" ({}s)", elapsed)
                };
                let mut spans = vec![Span::styled(
                    format!("  {spinner} "),
                    Style::default().fg(state.theme.warning),
                )];
                spans.extend(cc_badge);
                spans.push(Span::styled(desc, value_style));
                spans.push(Span::styled(
                    stats,
                    Style::default().fg(state.theme.text_dimmed),
                ));
                lines.push(Line::from(spans));
            } else {
                // Completed sub-agents: dimmed with stats
                let secs = sa.elapsed.map(|d| d.as_secs()).unwrap_or(0);
                let stats = if sa.tool_count > 0 {
                    format!(" ({} tools, {}s)", sa.tool_count, secs)
                } else {
                    format!(" ({}s)", secs)
                };
                let dim_modifier = Modifier::DIM;
                let mut spans = vec![Span::styled(
                    "  \u{2713} ",
                    Style::default()
                        .fg(state.theme.success)
                        .add_modifier(dim_modifier),
                )];
                if sa.provider.as_deref() == Some("claude-code") {
                    spans.push(Span::styled(
                        "[CC] ",
                        Style::default()
                            .fg(state.theme.secondary)
                            .add_modifier(dim_modifier),
                    ));
                }
                spans.push(Span::styled(
                    desc,
                    Style::default()
                        .fg(state.theme.text_dimmed)
                        .add_modifier(dim_modifier),
                ));
                spans.push(Span::styled(
                    stats,
                    Style::default()
                        .fg(state.theme.text_dimmed)
                        .add_modifier(dim_modifier),
                ));
                lines.push(Line::from(spans));
            }
        }
    }

    // Todo section — only show when there are incomplete items
    if todo_list::has_incomplete(&state.todo_items) {
        lines.push(Line::from(""));
        lines.push(Line::from(Span::styled("todos", label_style)));

        let max_content_len = inner_area.width.saturating_sub(4) as usize;

        for item in &state.todo_items {
            let (icon, style) = match item.status {
                TodoStatus::Completed => (
                    "\u{2713}",
                    Style::default()
                        .fg(state.theme.success)
                        .add_modifier(Modifier::DIM),
                ),
                TodoStatus::InProgress => ("\u{25CF}", Style::default().fg(state.theme.warning)),
                TodoStatus::Pending => ("\u{25CB}", Style::default().fg(state.theme.text)),
                TodoStatus::Cancelled => (
                    "\u{2717}",
                    Style::default()
                        .fg(state.theme.text_dimmed)
                        .add_modifier(Modifier::DIM),
                ),
            };

            let priority_prefix = match item.priority {
                ava_types::TodoPriority::High => "! ",
                _ => "",
            };

            let display = truncate_display(&item.content, max_content_len);

            if priority_prefix.is_empty() {
                lines.push(Line::from(vec![
                    Span::styled(format!("  {icon} "), style),
                    Span::styled(display, style),
                ]));
            } else {
                lines.push(Line::from(vec![
                    Span::styled(format!("  {icon} "), style),
                    Span::styled(
                        priority_prefix.to_string(),
                        Style::default().fg(state.theme.error),
                    ),
                    Span::styled(display, style),
                ]));
            }
        }
    }

    // Clamp every line to the inner width (area minus 1 for the left border)
    let clamp_width = inner_area.width as usize;
    let clamped_lines: Vec<Line<'static>> = to_static_lines(lines)
        .into_iter()
        .map(|line| clamp_line(line, clamp_width))
        .collect();
    let widget = Paragraph::new(clamped_lines)
        .block(Block::default().style(Style::default().bg(state.theme.bg_surface)));
    frame.render_widget(
        Block::default().style(Style::default().bg(state.theme.bg_surface)),
        area,
    );
    frame.render_widget(widget, inner_area);
}

fn format_workspace_path(path: &std::path::Path, max_width: usize) -> String {
    let parts: Vec<String> = path
        .components()
        .filter_map(|component| match component {
            std::path::Component::Normal(part) => Some(part.to_string_lossy().to_string()),
            _ => None,
        })
        .collect();

    let display = if let Some(idx) = parts.iter().position(|part| part == "Personal") {
        format!("/{}", parts[idx..].join("/"))
    } else if parts.len() >= 3 {
        format!(
            "/{}/{}/{}",
            parts[parts.len() - 3],
            parts[parts.len() - 2],
            parts[parts.len() - 1]
        )
    } else {
        path.display().to_string()
    };

    truncate_display(&display, max_width)
}
