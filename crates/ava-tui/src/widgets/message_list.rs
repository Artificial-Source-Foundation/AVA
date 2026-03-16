use crate::app::{AppState, ViewMode};
use crate::state::messages::{MessageKind, UiMessage};
use crate::text_utils::{display_width, safe_char_width};
use crate::widgets::message::{render_action_group, render_message_with_options};
use crate::widgets::welcome::render_welcome;
use ratatui::layout::{Constraint, Direction, Layout, Rect};
use ratatui::style::{Modifier, Style};
use ratatui::text::{Line, Span};
use ratatui::widgets::{Block, Clear, Paragraph, Scrollbar, ScrollbarOrientation, ScrollbarState};
use ratatui::Frame;

/// Hard-clamp a Line to at most `max_width` display columns.
///
/// This is the **final safety net** against text bleed.  Every line that enters
/// the message-list Paragraph passes through this function.  If a line is
/// already within budget it is returned unchanged (zero allocation).  Otherwise
/// spans are truncated (and trailing spans dropped) until the total width fits.
fn clamp_line_width(line: Line<'static>, max_width: usize) -> Line<'static> {
    // Fast path: measure total width and bail early if it fits.
    let total: usize = line
        .spans
        .iter()
        .map(|s| display_width(s.content.as_ref()))
        .sum();
    if total <= max_width {
        return line;
    }

    let alignment = line.alignment;
    let mut remaining = max_width;
    let mut clamped_spans: Vec<Span<'static>> = Vec::with_capacity(line.spans.len());

    for span in line.spans {
        if remaining == 0 {
            break;
        }
        let span_w = display_width(span.content.as_ref());
        if span_w <= remaining {
            remaining -= span_w;
            clamped_spans.push(span);
        } else {
            // Truncate this span at `remaining` display columns.
            let mut col = 0usize;
            let mut byte_end = 0usize;
            for ch in span.content.chars() {
                let cw = safe_char_width(ch);
                if col + cw > remaining {
                    break;
                }
                col += cw;
                byte_end += ch.len_utf8();
            }
            if byte_end > 0 {
                clamped_spans.push(Span::styled(
                    span.content[..byte_end].to_owned(),
                    span.style,
                ));
            }
            break;
        }
    }

    let mut result = Line::from(clamped_spans);
    result.alignment = alignment;
    result
}

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
        frame.render_widget(Clear, area);
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

    // Reserve 1 column for the scrollbar + 1 column of right margin +
    // 1 column safety margin against ambiguous-width Unicode characters.
    // ALL text rendering must use content_width to prevent bleed.
    let content_width = area.width.saturating_sub(3);

    // Top padding: 1 blank line between status bar and first message.
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
        lines.push(Line::from(Span::styled(
            "\u{2500}".repeat(content_width as usize),
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
                format!("Praxis #{task_id}: {truncated_goal}"),
                Style::default()
                    .fg(state.theme.accent)
                    .add_modifier(Modifier::BOLD),
            ),
            Span::styled(status_str, Style::default().fg(state.theme.text_muted)),
        ]));
        lines.push(Line::from(Span::styled(
            "\u{2500}".repeat(content_width as usize),
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
            "\u{2500}".repeat(content_width as usize),
            Style::default().fg(state.theme.border),
        )));
        lines.push(Line::raw(""));
    }

    let blocks = derive_blocks(messages_source);
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
    let start = (state.messages.scroll_offset as usize).min(lines.len());
    let end = (start + visible_height as usize).min(lines.len());
    let visible_lines: Vec<Line<'static>> = lines.drain(start..end).collect();

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
        .map(|line| clamp_line_width(line, clamped_width))
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
        let mut scrollbar_state = ScrollbarState::new(max_offset as usize)
            .position(state.messages.scroll_offset as usize);
        let scrollbar = Scrollbar::new(ScrollbarOrientation::VerticalRight)
            .thumb_symbol("\u{2593}")
            .track_symbol(Some("\u{2591}"))
            .begin_symbol(None)
            .end_symbol(None)
            .style(Style::default().fg(state.theme.text_dimmed));
        frame.render_stateful_widget(scrollbar, scrollbar_area_layout, &mut scrollbar_state);
    }
}
