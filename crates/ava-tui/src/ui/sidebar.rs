use crate::app::{AppState, ViewMode};
use crate::text_utils::truncate_display;
use crate::widgets::safe_render::clamp_line;
use crate::widgets::todo_list;
use ava_types::TodoStatus;
use ratatui::layout::Rect;
use ratatui::style::{Color, Modifier, Style};
use ratatui::text::{Line, Span};
use ratatui::widgets::{Block, Borders, Paragraph};
use ratatui::Frame;

pub fn render_sidebar(frame: &mut Frame<'_>, area: Rect, state: &AppState) {
    let session_label = state
        .session
        .current_session
        .as_ref()
        .map(|s| s.id.to_string()[..8].to_string())
        .unwrap_or_else(|| "none".to_string());

    let label_style = Style::default()
        .fg(state.theme.text_muted)
        .add_modifier(Modifier::BOLD);
    let value_style = Style::default().fg(state.theme.text);
    let dim_style = Style::default().fg(state.theme.text_dimmed);

    // Maximum display width for value text (account for border + indent)
    let inner_w = area.width.saturating_sub(1) as usize;

    let provider_display = truncate_display(&state.agent.provider_name, inner_w.saturating_sub(2));
    let model_display = truncate_display(&state.agent.model_name, inner_w.saturating_sub(2));
    let activity_str = state.agent.activity.to_string();
    let activity_display = truncate_display(&activity_str, inner_w.saturating_sub(2));

    let mut lines = vec![
        Line::from(""),
        Line::from(Span::styled("Session", label_style)),
        Line::from(Span::styled(format!("  {session_label}"), value_style)),
        Line::from(""),
        Line::from(Span::styled("Provider", label_style)),
        Line::from(Span::styled(format!("  {provider_display}"), value_style)),
        Line::from(Span::styled(format!("  {model_display}"), value_style)),
        Line::from(""),
        Line::from(Span::styled("Context (latest turn)", label_style)),
        Line::from(Span::styled(
            format!("  in:  {}", state.agent.tokens_used.input),
            value_style,
        )),
        Line::from(Span::styled(
            format!("  out: {}", state.agent.tokens_used.output),
            value_style,
        )),
        Line::from(Span::styled("Session Total", label_style)),
        Line::from(Span::styled(
            format!("  in:  {}", state.agent.tokens_used.cumulative_input),
            value_style,
        )),
        Line::from(Span::styled(
            format!("  out: {}", state.agent.tokens_used.cumulative_output),
            value_style,
        )),
        Line::from(""),
        Line::from(Span::styled("Agent", label_style)),
        Line::from(Span::styled(
            if state.agent.max_turns == 0 {
                format!("  Turn {}", state.agent.current_turn)
            } else {
                format!(
                    "  Turn {}/{}",
                    state.agent.current_turn, state.agent.max_turns
                )
            },
            value_style,
        )),
        Line::from(Span::styled(format!("  {activity_display}"), value_style)),
    ];

    // Sub-agents section — only show when there are any sub-agents
    if !state.agent.sub_agents.is_empty() {
        lines.push(Line::from(""));
        lines.push(Line::from(Span::styled("Sub-agents", label_style)));

        let max_desc_len = area.width.saturating_sub(8) as usize;
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
                        .fg(Color::Magenta)
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
                    Style::default().fg(Color::Yellow),
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
                    Style::default().fg(Color::Green).add_modifier(dim_modifier),
                )];
                if sa.provider.as_deref() == Some("claude-code") {
                    spans.push(Span::styled(
                        "[CC] ",
                        Style::default()
                            .fg(Color::Magenta)
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

    if !state.praxis.tasks.is_empty() {
        lines.push(Line::from(""));
        lines.push(Line::from(Span::styled("Praxis", label_style)));

        for task in state.praxis.tasks.iter().rev().take(3) {
            let goal = truncate_display(&task.goal, area.width.saturating_sub(10) as usize);
            lines.push(Line::from(vec![
                Span::styled(format!("  #{} ", task.id), dim_style),
                Span::styled(goal, value_style),
            ]));
            lines.push(Line::from(Span::styled(
                format!("    {} · {} workers", task.status, task.workers.len()),
                dim_style,
            )));
            for worker in task.workers.iter().take(2) {
                let worker_desc = truncate_display(
                    &format!("{} {}/{}", worker.lead, worker.turn, worker.max_turns),
                    area.width.saturating_sub(8) as usize,
                );
                lines.push(Line::from(Span::styled(
                    format!("      {worker_desc}"),
                    dim_style,
                )));
            }
        }
    }

    // Todo section — only show when there are incomplete items
    if todo_list::has_incomplete(&state.todo_items) {
        lines.push(Line::from(""));
        lines.push(Line::from(Span::styled("Todos", label_style)));

        let max_content_len = area.width.saturating_sub(6) as usize;

        for item in &state.todo_items {
            let (icon, style) = match item.status {
                TodoStatus::Completed => (
                    "\u{2713}",
                    Style::default()
                        .fg(Color::Green)
                        .add_modifier(Modifier::DIM),
                ),
                TodoStatus::InProgress => ("\u{25CF}", Style::default().fg(Color::Yellow)),
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
                    Span::styled(priority_prefix.to_string(), Style::default().fg(Color::Red)),
                    Span::styled(display, style),
                ]));
            }
        }
    }

    lines.push(Line::from(""));
    lines.push(Line::from(Span::styled("Keys", label_style)));
    lines.push(Line::from(Span::styled("  Ctrl+K  palette", dim_style)));
    lines.push(Line::from(Span::styled("  Ctrl+M  model", dim_style)));
    lines.push(Line::from(Span::styled("  Ctrl+E  tools", dim_style)));
    lines.push(Line::from(Span::styled("  Ctrl+N  new session", dim_style)));
    lines.push(Line::from(Span::styled("  Ctrl+L  sessions", dim_style)));
    lines.push(Line::from(Span::styled("  Ctrl+S  sidebar", dim_style)));
    lines.push(Line::from(Span::styled("  Ctrl+C  cancel/quit", dim_style)));
    if matches!(state.view_mode, ViewMode::SubAgent { .. }) {
        lines.push(Line::from(Span::styled(
            "  Esc     back (sub-agent)",
            dim_style,
        )));
    } else if matches!(state.view_mode, ViewMode::PraxisTask { .. }) {
        lines.push(Line::from(Span::styled(
            "  Esc     back (praxis)",
            dim_style,
        )));
    }

    // Clamp every line to the inner width (area minus 1 for the left border)
    let clamp_width = area.width.saturating_sub(1) as usize;
    let clamped_lines: Vec<Line<'static>> = lines
        .into_iter()
        .map(|line| {
            let static_spans: Vec<Span<'static>> = line
                .spans
                .into_iter()
                .map(|s| Span::styled(s.content.to_string(), s.style))
                .collect();
            clamp_line(Line::from(static_spans), clamp_width)
        })
        .collect();
    let widget = Paragraph::new(clamped_lines).block(
        Block::default()
            .borders(Borders::LEFT)
            .border_style(Style::default().fg(state.theme.border)),
    );
    frame.render_widget(widget, area);
}
