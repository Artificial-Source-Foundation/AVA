use crate::app::AppState;
use crate::widgets::message::render_message;
use crate::widgets::welcome::render_welcome;
use ratatui::layout::Rect;
use ratatui::widgets::{Block, Borders, Paragraph};
use ratatui::Frame;

pub fn render_message_list(frame: &mut Frame<'_>, area: Rect, state: &mut AppState) {
    if state.messages.messages.is_empty() {
        let block = Block::default().borders(Borders::ALL);
        let inner = block.inner(area);
        frame.render_widget(block, area);
        render_welcome(frame, inner, state);
        return;
    }

    let mut lines = Vec::new();
    for message in &state.messages.messages {
        lines.extend(render_message(message, &state.theme));
    }

    let total = lines.len() as u16;
    let visible_height = area.height.saturating_sub(2); // borders

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

    let widget = Paragraph::new(lines)
        .scroll((state.messages.scroll_offset, 0))
        .block(Block::default().title("Messages").borders(Borders::ALL));

    frame.render_widget(widget, area);
}
