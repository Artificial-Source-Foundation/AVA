use crate::app::AppState;
use crate::widgets::message::render_message;
use crate::widgets::welcome::render_welcome;
use ratatui::layout::Rect;
use ratatui::widgets::Paragraph;
use ratatui::Frame;

pub fn render_message_list(frame: &mut Frame<'_>, area: Rect, state: &mut AppState) {
    if state.messages.messages.is_empty() {
        render_welcome(frame, area, state);
        return;
    }

    let spinner_tick = state.messages.spinner_tick;
    let mut lines = Vec::new();
    for message in &state.messages.messages {
        lines.extend(render_message(message, &state.theme, spinner_tick));
    }

    let total = lines.len() as u16;
    let visible_height = area.height;

    state.messages.total_lines = total;
    state.messages.visible_height = visible_height;

    // Auto-scroll: keep bottom visible
    if state.messages.auto_scroll {
        state.messages.scroll_offset = total.saturating_sub(visible_height);
    }

    // Clamp offset to valid range
    let max_offset = total.saturating_sub(visible_height);
    if state.messages.scroll_offset > max_offset {
        state.messages.scroll_offset = max_offset;
    }

    let widget = Paragraph::new(lines).scroll((state.messages.scroll_offset, 0));
    frame.render_widget(widget, area);
}
