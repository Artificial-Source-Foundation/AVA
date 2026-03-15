use crate::state::messages::{spinner_frame, MessageKind, UiMessage};
use crate::state::theme::Theme;
use ratatui::style::{Modifier, Style};
use ratatui::text::{Line, Span};

pub fn render_message(
    message: &UiMessage,
    theme: &Theme,
    spinner_tick: usize,
    width: u16,
) -> Vec<Line<'static>> {
    message.to_lines(theme, spinner_tick, width)
}

pub fn render_message_with_options(
    message: &UiMessage,
    theme: &Theme,
    spinner_tick: usize,
    width: u16,
    show_thinking: bool,
) -> Vec<Line<'static>> {
    message.to_lines_with_options(theme, spinner_tick, width, show_thinking)
}

pub fn render_action_group(
    messages: &[&UiMessage],
    theme: &Theme,
    spinner_tick: usize,
    width: u16,
    expanded: bool,
) -> Vec<Line<'static>> {
    let tool_calls: Vec<&UiMessage> = messages
        .iter()
        .copied()
        .filter(|msg| matches!(msg.kind, MessageKind::ToolCall))
        .collect();
    let tool_results: Vec<&UiMessage> = messages
        .iter()
        .copied()
        .filter(|msg| matches!(msg.kind, MessageKind::ToolResult))
        .collect();
    let active = messages
        .last()
        .is_some_and(|msg| matches!(msg.kind, MessageKind::ToolCall));

    let tool_names: Vec<String> = tool_calls
        .iter()
        .map(|msg| {
            msg.content
                .split_whitespace()
                .next()
                .unwrap_or("tool")
                .to_string()
        })
        .collect();

    let mut unique_names = Vec::new();
    for name in tool_names {
        if !unique_names.contains(&name) {
            unique_names.push(name);
        }
    }

    let title = if unique_names.is_empty() {
        format!("{} tool activity", tool_results.len().max(1))
    } else {
        crate::text_utils::truncate_display(
            &unique_names.join(", "),
            width.saturating_sub(12) as usize,
        )
    };

    let icon = if active {
        format!("{} ", spinner_frame(spinner_tick))
    } else {
        "▸ ".to_string()
    };
    let mut lines = vec![Line::from(vec![
        Span::styled(icon, Style::default().fg(theme.accent)),
        Span::styled(
            format!(
                "{} tools · {} result{}",
                tool_calls.len().max(1),
                tool_results.len(),
                if tool_results.len() == 1 { "" } else { "s" }
            ),
            Style::default()
                .fg(theme.text_muted)
                .add_modifier(Modifier::BOLD),
        ),
        Span::styled(" · ", Style::default().fg(theme.text_dimmed)),
        Span::styled(title, Style::default().fg(theme.text_dimmed)),
    ])];

    if expanded {
        for call in tool_calls.iter().take(3) {
            let preview = crate::text_utils::truncate_display(
                &call.content,
                width.saturating_sub(6) as usize,
            );
            lines.push(Line::from(vec![
                Span::raw("  "),
                Span::styled("• ", Style::default().fg(theme.success)),
                Span::styled(preview, Style::default().fg(theme.text_dimmed)),
            ]));
        }
        if tool_calls.len() > 3 {
            lines.push(Line::from(Span::styled(
                format!("  … {} more tool calls", tool_calls.len() - 3),
                Style::default()
                    .fg(theme.text_dimmed)
                    .add_modifier(Modifier::DIM),
            )));
        }

        if let Some(last_result) = tool_results.last() {
            let preview = crate::text_utils::truncate_display(
                &last_result.content.replace('\n', " "),
                width.saturating_sub(6) as usize,
            );
            lines.push(Line::from(vec![
                Span::raw("  "),
                Span::styled("↳ ", Style::default().fg(theme.primary)),
                Span::styled(preview, Style::default().fg(theme.text_muted)),
            ]));
        }
    }

    lines
}
