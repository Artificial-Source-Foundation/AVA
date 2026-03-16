use crate::state::background::{BackgroundState, TaskStatus};
use crate::state::messages::spinner_frame;
use crate::state::theme::Theme;
use crate::widgets::safe_render::{clamp_line, truncate_str};
use ratatui::layout::Rect;
use ratatui::style::{Modifier, Style};
use ratatui::text::{Line, Span};
use ratatui::widgets::Paragraph;
use ratatui::Frame;

/// Render the background tasks modal within the given area.
pub fn render_task_list(
    frame: &mut Frame<'_>,
    area: Rect,
    bg_state: &BackgroundState,
    theme: &Theme,
    spinner_tick: usize,
) {
    let w = area.width as usize;
    let mut lines: Vec<Line<'_>> = Vec::new();

    // Title
    lines.push(Line::from(Span::styled(
        truncate_str("Background Tasks", w),
        Style::default().fg(theme.text).add_modifier(Modifier::BOLD),
    )));
    lines.push(Line::from(""));

    if bg_state.tasks.is_empty() {
        lines.push(Line::from(Span::styled(
            "  No background tasks",
            Style::default().fg(theme.text_muted),
        )));
    } else {
        for (i, task) in bg_state.tasks.iter().enumerate() {
            let is_selected = i == bg_state.selected_index;

            let (icon, icon_style) = match task.status {
                TaskStatus::Running => {
                    let frame_char = spinner_frame(spinner_tick);
                    (frame_char.to_string(), Style::default().fg(theme.accent))
                }
                TaskStatus::Completed => (
                    "\u{2713}".to_string(),
                    Style::default()
                        .fg(theme.success)
                        .add_modifier(Modifier::BOLD),
                ),
                TaskStatus::Failed => (
                    "\u{2717}".to_string(),
                    Style::default()
                        .fg(theme.error)
                        .add_modifier(Modifier::BOLD),
                ),
            };

            let status_str = match task.status {
                TaskStatus::Running => "Running ",
                TaskStatus::Completed => "Done    ",
                TaskStatus::Failed => "Failed  ",
            };

            let status_style = match task.status {
                TaskStatus::Running => Style::default().fg(theme.accent),
                TaskStatus::Completed => Style::default().fg(theme.success),
                TaskStatus::Failed => Style::default().fg(theme.error),
            };

            let prefix = if is_selected { "> " } else { "  " };
            let prefix_style = if is_selected {
                Style::default()
                    .fg(theme.accent)
                    .add_modifier(Modifier::BOLD)
            } else {
                Style::default().fg(theme.text_dimmed)
            };

            // Truncate goal to fit — derive budget from area width
            let goal_budget = w.saturating_sub(22); // prefix(2) + id(4) + icon(2) + status(8) + quotes(2) + elapsed(~4)
            let goal = task.goal_display(goal_budget);
            let elapsed = task.elapsed_display();

            let goal_style = if is_selected {
                Style::default().fg(theme.text)
            } else {
                Style::default().fg(theme.text_muted)
            };

            lines.push(clamp_line(
                Line::from(vec![
                    Span::styled(prefix.to_string(), prefix_style),
                    Span::styled(
                        format!("#{:<3}", task.id),
                        Style::default().fg(theme.text_dimmed),
                    ),
                    Span::styled(format!("{icon} "), icon_style),
                    Span::styled(status_str.to_string(), status_style),
                    Span::styled(format!("\"{goal}\""), goal_style),
                    Span::styled(
                        format!("  {elapsed}"),
                        Style::default().fg(theme.text_dimmed),
                    ),
                ]),
                w,
            ));
        }
    }

    // Summary line
    lines.push(Line::from(""));
    let running = bg_state
        .tasks
        .iter()
        .filter(|t| t.status == TaskStatus::Running)
        .count();
    let completed = bg_state
        .tasks
        .iter()
        .filter(|t| t.status == TaskStatus::Completed)
        .count();
    let failed = bg_state
        .tasks
        .iter()
        .filter(|t| t.status == TaskStatus::Failed)
        .count();
    let total_tokens = bg_state.total_tokens();
    let total_cost = bg_state.total_cost();

    let mut summary_spans: Vec<Span<'_>> = vec![Span::styled("  ", Style::default())];
    summary_spans.push(Span::styled(
        format!("Running: {running}"),
        Style::default().fg(theme.accent),
    ));
    summary_spans.push(Span::styled("  ", Style::default()));
    summary_spans.push(Span::styled(
        format!("Completed: {completed}"),
        Style::default().fg(theme.success),
    ));
    summary_spans.push(Span::styled("  ", Style::default()));
    summary_spans.push(Span::styled(
        format!("Failed: {failed}"),
        Style::default().fg(theme.error),
    ));
    lines.push(clamp_line(Line::from(summary_spans), w));

    if total_tokens > 0 || total_cost > 0.0 {
        let tokens_str = if total_tokens >= 1_000_000 {
            format!("{:.1}M", total_tokens as f64 / 1_000_000.0)
        } else if total_tokens >= 1_000 {
            format!("{:.1}K", total_tokens as f64 / 1_000.0)
        } else {
            total_tokens.to_string()
        };
        lines.push(clamp_line(
            Line::from(vec![
                Span::styled("  ".to_string(), Style::default()),
                Span::styled(
                    format!("Total tokens: {tokens_str}  Cost: ${total_cost:.2}"),
                    Style::default().fg(theme.text_muted),
                ),
            ]),
            w,
        ));
    }

    // Keybind hints
    lines.push(Line::from(""));
    lines.push(clamp_line(
        Line::from(vec![
            Span::styled("  ".to_string(), Style::default()),
            Span::styled(
                "Enter".to_string(),
                Style::default()
                    .fg(theme.text_muted)
                    .add_modifier(Modifier::BOLD),
            ),
            Span::styled(
                ": view task  ".to_string(),
                Style::default().fg(theme.text_dimmed),
            ),
            Span::styled(
                "Esc".to_string(),
                Style::default()
                    .fg(theme.text_muted)
                    .add_modifier(Modifier::BOLD),
            ),
            Span::styled(
                ": close".to_string(),
                Style::default().fg(theme.text_dimmed),
            ),
        ]),
        w,
    ));

    let paragraph = Paragraph::new(lines);
    frame.render_widget(paragraph, area);
}
