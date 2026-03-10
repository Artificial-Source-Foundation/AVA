use crate::app::AppState;
use crate::widgets::message::render_message;
use crate::widgets::welcome::render_welcome;
use ratatui::layout::Rect;
use ratatui::style::Style;
use ratatui::text::Line;
use ratatui::widgets::{Paragraph, Scrollbar, ScrollbarOrientation, ScrollbarState};
use ratatui::Frame;

pub fn render_message_list(frame: &mut Frame<'_>, area: Rect, state: &mut AppState) {
    // Empty state: show the welcome screen across the full area.
    if state.messages.messages.is_empty() {
        render_welcome(frame, area, state);
        return;
    }

    // Build all visual lines, inserting 1 blank line between every message.
    let spinner_tick = state.messages.spinner_tick;
    let mut lines: Vec<Line<'static>> = Vec::new();
    for (i, message) in state.messages.messages.iter().enumerate() {
        if i > 0 {
            lines.push(Line::raw(""));
        }
        lines.extend(render_message(message, &state.theme, spinner_tick, area.width));
    }

    // Lines are pre-wrapped, so each Line = 1 visual row.
    let total = lines.len() as u16;
    let visible_height = area.height;

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

    let widget = Paragraph::new(lines)
        .scroll((state.messages.scroll_offset, 0));
    frame.render_widget(widget, area);

    // Render scroll indicator when not at bottom (content overflows).
    if total > visible_height && !state.messages.auto_scroll {
        let mut scrollbar_state = ScrollbarState::new(max_offset as usize)
            .position(state.messages.scroll_offset as usize);
        let scrollbar = Scrollbar::new(ScrollbarOrientation::VerticalRight)
            .thumb_symbol("\u{2593}")
            .track_symbol(Some("\u{2591}"))
            .style(Style::default().fg(state.theme.text_dimmed));
        frame.render_stateful_widget(scrollbar, area, &mut scrollbar_state);
    }
}
