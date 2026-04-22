use crate::app::{AppState, ViewMode};
use crate::state::messages::{MessageKind, UiMessage};
use crate::widgets::message::{render_action_group, render_message_with_options};
use crate::widgets::safe_render::hard_clamp_line;
use crate::widgets::welcome::render_welcome;
use ratatui::layout::{Constraint, Direction, Layout, Rect};
use ratatui::style::{Modifier, Style};
use ratatui::text::{Line, Span};
use ratatui::widgets::{Block, Clear, Paragraph, Scrollbar, ScrollbarOrientation, ScrollbarState};
use ratatui::Frame;

enum RenderBlock<'a> {
    Message(&'a UiMessage),
    ActionGroup {
        messages: Vec<&'a UiMessage>,
        active: bool,
    },
}

fn block_spacing(previous: Option<&RenderBlock<'_>>, current: &RenderBlock<'_>) -> usize {
    match (previous, current) {
        (None, _) => 0,
        (Some(RenderBlock::ActionGroup { .. }), RenderBlock::Message(message))
            if matches!(message.kind, MessageKind::Assistant) =>
        {
            2
        }
        (Some(RenderBlock::Message(prev)), RenderBlock::Message(message))
            if matches!(prev.kind, MessageKind::User)
                && matches!(message.kind, MessageKind::Assistant) =>
        {
            2
        }
        (Some(RenderBlock::Message(prev)), RenderBlock::ActionGroup { .. })
            if matches!(prev.kind, MessageKind::Assistant) =>
        {
            1
        }
        _ => 1,
    }
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
            let bg = state.background.lock().unwrap_or_else(|e| e.into_inner());
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
    };

    // Empty state: show the welcome screen across the full area (main view only).
    if messages_source.is_empty() {
        state.messages.messages_area = area;
        state.messages.message_line_ranges.clear();
        state.messages.total_lines = 0;
        state.messages.visible_height = usize::from(area.height);

        // Clear the area first to prevent ghost characters from prior renders.
        frame.render_widget(Clear, area);
        let bg_fill = Block::default().style(Style::default().bg(state.theme.bg_deep));
        frame.render_widget(bg_fill, area);

        if matches!(state.view_mode, ViewMode::Main) {
            render_welcome(frame, area, state);
        } else {
            // Sub-agent or background task with no messages — show a hint
            let hint_text = if matches!(state.view_mode, ViewMode::BackgroundTask { .. }) {
                "No output from this background task yet."
            } else if let ViewMode::SubAgent { agent_index, .. } = &state.view_mode {
                if state
                    .agent
                    .sub_agents
                    .get(*agent_index)
                    .map(|sa| sa.is_running)
                    .unwrap_or(false)
                {
                    "Sub-agent is running. Transcript details will appear here when available."
                } else {
                    "No messages in this sub-agent conversation."
                }
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

    // Reserve 1 column for the scrollbar + 1 column of right margin +
    // 1 column safety margin against ambiguous-width Unicode characters.
    // ALL text rendering must use content_width to prevent bleed.
    let content_width = area.width.saturating_sub(3);

    // Top padding only; keep the conversation surface visually quiet.
    lines.push(Line::raw(""));

    // Add breadcrumb header when viewing a background task or sub-agent conversation.
    if let ViewMode::BackgroundTask { task_id, goal } = &state.view_mode {
        let truncated_goal = crate::text_utils::truncate_display(goal, 55);
        let bg = state.background.lock().unwrap_or_else(|e| e.into_inner());
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
        lines.push(Line::raw(""));
    }

    if let ViewMode::SubAgent {
        agent_index,
        description,
    } = &state.view_mode
    {
        if let Some(subagent) = state.agent.sub_agents.get(*agent_index) {
            let truncated_desc = crate::text_utils::truncate_display(description, 60);
            let total = state.agent.sub_agents.len();
            let index_label = format!("#{}/{}", *agent_index + 1, total);
            let (status_label, status_style) = if subagent.is_running {
                let activity = subagent
                    .current_tool
                    .as_deref()
                    .map(|tool| format!("running {tool}"))
                    .unwrap_or_else(|| "running".to_string());
                (
                    activity,
                    Style::default()
                        .fg(state.theme.warning)
                        .add_modifier(Modifier::BOLD),
                )
            } else {
                (
                    "done".to_string(),
                    Style::default().fg(state.theme.text_muted),
                )
            };
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
                    format!("Sub-agent {index_label}: {truncated_desc}"),
                    Style::default()
                        .fg(state.theme.accent)
                        .add_modifier(Modifier::BOLD),
                ),
                Span::styled(" · ", Style::default().fg(state.theme.text_dimmed)),
                Span::styled(status_label, status_style),
            ]));
            lines.push(Line::raw(""));
        }
    }

    let blocks = derive_blocks(messages_source);
    let show_thinking = state.agent.show_thinking;

    // Track per-source-message line ranges for mouse click hit-testing.
    let mut line_ranges: Vec<(usize, usize)> = Vec::with_capacity(messages_source.len());
    // Index into messages_source — advances as we iterate render blocks.
    let mut src_idx = 0usize;

    for (i, block) in blocks.iter().enumerate() {
        if i > 0 {
            let gap = block_spacing(blocks.get(i.saturating_sub(1)), block);
            for _ in 0..gap {
                lines.push(Line::raw(""));
            }
        }
        let start = lines.len();
        match block {
            RenderBlock::Message(message) => {
                lines.extend(render_message_with_options(
                    message,
                    &state.theme,
                    spinner_tick,
                    content_width,
                    show_thinking,
                ));
                let end = lines.len();
                // Record range for this source message.
                if src_idx < messages_source.len() {
                    line_ranges.push((start, end));
                    src_idx += 1;
                }
            }
            RenderBlock::ActionGroup { messages, active } => {
                // Per-group expanded state: check if any message in the group
                // has tool_group_expanded set by user click.
                let group_expanded = messages.iter().any(|m| m.tool_group_expanded);
                lines.extend(render_action_group(
                    messages,
                    &state.theme,
                    spinner_tick,
                    content_width,
                    *active || state.messages.show_tools_expanded || group_expanded,
                ));
                let end = lines.len();
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

    if !state.messages.auto_scroll && state.messages.unseen_count > 0 {
        lines.push(Line::raw(""));
        lines.push(Line::from(vec![
            Span::styled(
                format!("{} new", state.messages.unseen_count),
                Style::default().fg(state.theme.text),
            ),
            Span::styled(
                "  End to return to live",
                Style::default().fg(state.theme.text_dimmed),
            ),
        ]));
    }

    // Bottom padding: 1 blank line between last message and composer.
    lines.push(Line::raw(""));

    // Lines are pre-wrapped, so each Line = 1 visual row.
    let total = lines.len();
    let visible_height = usize::from(area.height);

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

    // STEP 1: Clear the ENTIRE area to prevent ghost characters from prior renders.
    frame.render_widget(Clear, area);
    let bg_fill = Block::default().style(Style::default().bg(state.theme.bg_deep));
    frame.render_widget(bg_fill, area);

    // STEP 2: Split the area into content + margin + scrollbar using Layout.
    // This guarantees pixel-perfect tiling with NO gaps between regions.
    // Previous manual Rect arithmetic left a 2-column gap that was never
    // explicitly rendered into, causing ghost character bleed.
    let horizontal = Layout::default()
        .direction(Direction::Horizontal)
        .constraints([
            Constraint::Min(0),    // content
            Constraint::Length(2), // right margin — absorbs Unicode bleed
            Constraint::Length(1), // scrollbar
        ])
        .split(area);
    let content_area = horizontal[0];
    let margin_area = horizontal[1];
    let scrollbar_area_layout = horizontal[2];

    // Explicitly fill the margin gap so no stale characters survive.
    frame.render_widget(Clear, margin_area);
    let margin_bg = Block::default().style(Style::default().bg(state.theme.bg_deep));
    frame.render_widget(margin_bg, margin_area);

    // Also explicitly clear the scrollbar column.
    frame.render_widget(Clear, scrollbar_area_layout);
    let scrollbar_bg = Block::default().style(Style::default().bg(state.theme.bg_deep));
    frame.render_widget(scrollbar_bg, scrollbar_area_layout);

    // STEP 3: Manually slice lines to exactly the visible window instead of
    // relying on Paragraph::scroll(). This guarantees that the Paragraph widget
    // receives only as many lines as fit in the area, preventing any possibility
    // of text bleeding past area.bottom().
    let start = state.messages.scroll_offset.min(lines.len());
    // Hard-cap to content_area.height — the Paragraph must NEVER receive more
    // lines than the area can display, otherwise text bleeds into the composer.
    let max_visible = usize::from(content_area.height);
    let visible_lines: Vec<Line<'static>> =
        lines.into_iter().skip(start).take(max_visible).collect();

    // STEP 4: Hard-clamp every line to content_area.width.
    // This is the final safety net — even if wrapping or markdown rendering
    // produced a line wider than expected, we truncate it here so the
    // Paragraph widget can never emit characters beyond the area boundary.
    //
    // Conservative width measurement now treats non-ASCII as at least 2 columns,
    // so clamping to the full content width is safe.
    let clamped_width = content_area.width as usize;
    let clamped_lines: Vec<Line<'static>> = visible_lines
        .into_iter()
        .map(|line| hard_clamp_line(line, clamped_width))
        .collect();

    // STEP 5: Render the paragraph into the clipped content_area.
    // Explicit bg ensures every cell the Paragraph touches carries the
    // correct background, even for spans that omit a bg color.
    let widget = Paragraph::new(clamped_lines).style(Style::default().bg(state.theme.bg_deep));
    frame.render_widget(widget, content_area);

    // STEP 6: Render scrollbar into the layout-derived scrollbar column.
    // Using the layout-split area (scrollbar_area_layout) guarantees the
    // scrollbar is exactly 1 column wide with no gap between it and the margin.
    if total > visible_height && !state.messages.auto_scroll {
        let mut scrollbar_state =
            ScrollbarState::new(max_offset).position(state.messages.scroll_offset);
        let scrollbar = Scrollbar::new(ScrollbarOrientation::VerticalRight)
            .thumb_symbol("\u{2593}")
            .track_symbol(Some("\u{2591}"))
            .begin_symbol(None)
            .end_symbol(None)
            .style(Style::default().fg(state.theme.text_dimmed));
        frame.render_stateful_widget(scrollbar, scrollbar_area_layout, &mut scrollbar_state);
    }
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
        let state = MessageState {
            show_tools_expanded: true,
            ..MessageState::default()
        };

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
