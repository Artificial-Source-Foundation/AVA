use crate::app::{AppState, ViewMode};
use crate::state::messages::{MessageKind, UiMessage};
use crate::widgets::message::{render_action_group, render_message_with_options};
use crate::widgets::welcome::render_welcome;
use ratatui::layout::Rect;
use ratatui::style::{Modifier, Style};
use ratatui::text::{Line, Span};
use ratatui::widgets::{Block, Paragraph, Scrollbar, ScrollbarOrientation, ScrollbarState};
use ratatui::Frame;

enum RenderBlock<'a> {
    Message(&'a UiMessage),
    ActionGroup {
        messages: Vec<&'a UiMessage>,
        active: bool,
    },
}

fn derive_blocks(messages: &[UiMessage]) -> Vec<RenderBlock<'_>> {
    let mut blocks = Vec::new();
    let mut current_group: Vec<&UiMessage> = Vec::new();

    for message in messages {
        if matches!(
            message.kind,
            MessageKind::ToolCall | MessageKind::ToolResult
        ) {
            current_group.push(message);
            continue;
        }

        if !current_group.is_empty() {
            blocks.push(RenderBlock::ActionGroup {
                messages: std::mem::take(&mut current_group),
                active: false,
            });
        }

        blocks.push(RenderBlock::Message(message));
    }

    if !current_group.is_empty() {
        blocks.push(RenderBlock::ActionGroup {
            messages: current_group,
            active: true,
        });
    }

    blocks
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::state::messages::MessageState;
    use crate::state::theme::Theme;

    #[test]
    fn derive_blocks_groups_consecutive_tool_messages() {
        let messages = vec![
            UiMessage::new(MessageKind::User, "search this"),
            UiMessage::new(MessageKind::ToolCall, "grep foo"),
            UiMessage::new(MessageKind::ToolResult, "matched"),
            UiMessage::new(MessageKind::Assistant, "done"),
        ];

        let blocks = derive_blocks(&messages);
        assert_eq!(blocks.len(), 3);
        assert!(matches!(blocks[0], RenderBlock::Message(_)));
        assert!(matches!(blocks[1], RenderBlock::ActionGroup { .. }));
        assert!(matches!(blocks[2], RenderBlock::Message(_)));
    }

    #[test]
    fn expanded_tool_history_forces_finished_groups_open() {
        let messages = vec![
            UiMessage::new(MessageKind::ToolCall, "grep foo"),
            UiMessage::new(MessageKind::ToolResult, "matched"),
        ];
        let blocks = derive_blocks(&messages);
        let mut state = MessageState::default();
        state.show_tools_expanded = true;

        let expanded = match &blocks[0] {
            RenderBlock::ActionGroup { messages, active } => render_action_group(
                messages,
                &Theme::default_theme(),
                0,
                80,
                *active || state.show_tools_expanded,
            ),
            RenderBlock::Message(_) => panic!("expected action group"),
        };

        assert!(expanded.len() > 1, "expanded group should render details");
    }
}

pub fn render_message_list(frame: &mut Frame<'_>, area: Rect, state: &mut AppState) {
    // Determine which messages to render based on view mode.
    // For BackgroundTask views, we need to copy from the shared state.
    let bg_messages_owned: Vec<crate::state::messages::UiMessage>;
    let messages_source: &[crate::state::messages::UiMessage] = match &state.view_mode {
        ViewMode::Main => &state.messages.messages,
        ViewMode::SubAgent { agent_index, .. } => {
            if let Some(sa) = state.agent.sub_agents.get(*agent_index) {
                &sa.session_messages
            } else {
                &state.messages.messages
            }
        }
        ViewMode::BackgroundTask { task_id, .. } => {
            let bg = state.background.lock().unwrap();
            if let Some(task) = bg.tasks.iter().find(|t| t.id == *task_id) {
                bg_messages_owned = task.messages.clone();
                drop(bg);
                &bg_messages_owned
            } else {
                bg_messages_owned = Vec::new();
                drop(bg);
                &bg_messages_owned
            }
        }
        ViewMode::PraxisTask { task_id, .. } => {
            if let Some(task) = state.praxis.task(*task_id) {
                &task.messages
            } else {
                &state.messages.messages
            }
        }
    };

    // Empty state: show the welcome screen across the full area (main view only).
    if messages_source.is_empty() {
        // Clear the area first to prevent ghost characters from prior renders.
        let bg_fill = Block::default().style(Style::default().bg(state.theme.bg_deep));
        frame.render_widget(bg_fill, area);

        if matches!(state.view_mode, ViewMode::Main) {
            render_welcome(frame, area, state);
        } else {
            // Sub-agent or background task with no messages — show a hint
            let hint_text = if matches!(state.view_mode, ViewMode::BackgroundTask { .. }) {
                "No output from this background task yet."
            } else if matches!(state.view_mode, ViewMode::PraxisTask { .. }) {
                "No output from this Praxis task yet."
            } else {
                "No messages in this sub-agent conversation."
            };
            let hint = Line::from(Span::styled(
                hint_text,
                Style::default()
                    .fg(state.theme.text_dimmed)
                    .add_modifier(Modifier::ITALIC),
            ));
            let widget = Paragraph::new(vec![hint]);
            frame.render_widget(widget, area);
        }
        return;
    }

    // Build all visual lines, inserting 1 blank line between every message.
    let spinner_tick = state.messages.spinner_tick;
    let mut lines: Vec<Line<'static>> = Vec::new();

    // Top padding: 1 blank line between status bar and first message.
    lines.push(Line::raw(""));

    // Add breadcrumb header when viewing a background task or sub-agent conversation.
    if let ViewMode::BackgroundTask { task_id, goal } = &state.view_mode {
        let truncated_goal = crate::text_utils::truncate_display(goal, 55);
        let bg = state.background.lock().unwrap();
        let status_str = if let Some(task) = bg.tasks.iter().find(|t| t.id == *task_id) {
            format!(" ({})", task.status)
        } else {
            String::new()
        };
        drop(bg);
        lines.push(Line::from(vec![
            Span::styled(
                "\u{2190} ",
                Style::default()
                    .fg(state.theme.accent)
                    .add_modifier(Modifier::BOLD),
            ),
            Span::styled(
                "Main",
                Style::default()
                    .fg(state.theme.primary)
                    .add_modifier(Modifier::BOLD),
            ),
            Span::styled(" > ", Style::default().fg(state.theme.text_dimmed)),
            Span::styled(
                format!("Task #{task_id}: {truncated_goal}"),
                Style::default()
                    .fg(state.theme.accent)
                    .add_modifier(Modifier::BOLD),
            ),
            Span::styled(status_str, Style::default().fg(state.theme.text_muted)),
        ]));
        lines.push(Line::from(Span::styled(
            "\u{2500}".repeat(area.width as usize),
            Style::default().fg(state.theme.border),
        )));
        lines.push(Line::raw(""));
    }

    if let ViewMode::PraxisTask { task_id, goal } = &state.view_mode {
        let truncated_goal = crate::text_utils::truncate_display(goal, 55);
        let status_str = state
            .praxis
            .task(*task_id)
            .map(|task| format!(" ({})", task.status))
            .unwrap_or_default();
        lines.push(Line::from(vec![
            Span::styled(
                "← ",
                Style::default()
                    .fg(state.theme.accent)
                    .add_modifier(Modifier::BOLD),
            ),
            Span::styled(
                "Main",
                Style::default()
                    .fg(state.theme.primary)
                    .add_modifier(Modifier::BOLD),
            ),
            Span::styled(" > ", Style::default().fg(state.theme.text_dimmed)),
            Span::styled(
                format!("Praxis #{task_id}: {truncated_goal}"),
                Style::default()
                    .fg(state.theme.accent)
                    .add_modifier(Modifier::BOLD),
            ),
            Span::styled(status_str, Style::default().fg(state.theme.text_muted)),
        ]));
        lines.push(Line::from(Span::styled(
            "─".repeat(area.width as usize),
            Style::default().fg(state.theme.border),
        )));
        lines.push(Line::raw(""));
    }

    if let ViewMode::SubAgent { description, .. } = &state.view_mode {
        let truncated_desc = crate::text_utils::truncate_display(description, 60);
        lines.push(Line::from(vec![
            Span::styled(
                "\u{2190} ",
                Style::default()
                    .fg(state.theme.accent)
                    .add_modifier(Modifier::BOLD),
            ),
            Span::styled(
                "Main",
                Style::default()
                    .fg(state.theme.primary)
                    .add_modifier(Modifier::BOLD),
            ),
            Span::styled(" > ", Style::default().fg(state.theme.text_dimmed)),
            Span::styled(
                format!("Sub-agent: {truncated_desc}"),
                Style::default()
                    .fg(state.theme.accent)
                    .add_modifier(Modifier::BOLD),
            ),
        ]));
        lines.push(Line::from(Span::styled(
            "\u{2500}".repeat(area.width as usize),
            Style::default().fg(state.theme.border),
        )));
        lines.push(Line::raw(""));
    }

    let blocks = derive_blocks(messages_source);
    // Reserve 1 column for the scrollbar so text doesn't overlap it.
    let content_width = area.width.saturating_sub(1);
    let show_thinking = state.agent.show_thinking;

    // Track per-source-message line ranges for mouse click hit-testing.
    let mut line_ranges: Vec<(u16, u16)> = Vec::with_capacity(messages_source.len());
    // Index into messages_source — advances as we iterate render blocks.
    let mut src_idx = 0usize;

    for (i, block) in blocks.iter().enumerate() {
        if i > 0 {
            lines.push(Line::raw(""));
        }
        let start = lines.len() as u16;
        match block {
            RenderBlock::Message(message) => {
                lines.extend(render_message_with_options(
                    message,
                    &state.theme,
                    spinner_tick,
                    content_width,
                    show_thinking,
                ));
                let end = lines.len() as u16;
                // Record range for this source message.
                if src_idx < messages_source.len() {
                    line_ranges.push((start, end));
                    src_idx += 1;
                }
            }
            RenderBlock::ActionGroup { messages, active } => {
                lines.extend(render_action_group(
                    messages,
                    &state.theme,
                    spinner_tick,
                    content_width,
                    *active || state.messages.show_tools_expanded,
                ));
                let end = lines.len() as u16;
                // Each message in the group gets the same range (the whole group).
                for _ in 0..messages.len() {
                    if src_idx < messages_source.len() {
                        line_ranges.push((start, end));
                        src_idx += 1;
                    }
                }
            }
        }
    }

    // Bottom padding: 1 blank line between last message and composer.
    lines.push(Line::raw(""));

    // Lines are pre-wrapped, so each Line = 1 visual row.
    let total = lines.len() as u16;
    let visible_height = area.height;

    state.messages.messages_area = area;
    state.messages.message_line_ranges = line_ranges;
    state.messages.total_lines = total;
    state.messages.visible_height = visible_height;

    // Auto-scroll: keep bottom visible.
    if state.messages.auto_scroll {
        state.messages.scroll_offset = total.saturating_sub(visible_height);
    }

    // Clamp offset to valid range.
    let max_offset = total.saturating_sub(visible_height);
    if state.messages.scroll_offset > max_offset {
        state.messages.scroll_offset = max_offset;
    }

    // Clear the entire message area with a background fill so no ghost characters
    // from previous frames or overlapping widgets persist between renders.
    let bg_fill = Block::default().style(Style::default().bg(state.theme.bg_deep));
    frame.render_widget(bg_fill, area);

    // CRITICAL: Manually slice lines to exactly the visible window instead of
    // relying on Paragraph::scroll(). This guarantees that the Paragraph widget
    // receives only as many lines as fit in the area, preventing any possibility
    // of text bleeding past area.bottom().
    let start = (state.messages.scroll_offset as usize).min(lines.len());
    let end = (start + visible_height as usize).min(lines.len());
    let visible_lines: Vec<Line<'static>> = lines.drain(start..end).collect();

    // Render the paragraph in a narrower area so text doesn't overlap the scrollbar column.
    let content_area = Rect {
        width: area.width.saturating_sub(1),
        ..area
    };
    let widget = Paragraph::new(visible_lines);
    frame.render_widget(widget, content_area);

    // Render scroll indicator when not at bottom (content overflows).
    // Use the full `area` so the scrollbar sits flush at the rightmost column.
    if total > visible_height && !state.messages.auto_scroll {
        let mut scrollbar_state = ScrollbarState::new(max_offset as usize)
            .position(state.messages.scroll_offset as usize);
        let scrollbar = Scrollbar::new(ScrollbarOrientation::VerticalRight)
            .thumb_symbol("\u{2593}")
            .track_symbol(Some("\u{2591}"))
            .begin_symbol(None)
            .end_symbol(None)
            .style(Style::default().fg(state.theme.text_dimmed));
        frame.render_stateful_widget(scrollbar, area, &mut scrollbar_state);
    }
}
