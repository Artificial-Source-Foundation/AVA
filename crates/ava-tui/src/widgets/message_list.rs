use crate::app::AppState;
use crate::widgets::message::render_message;
use ratatui::layout::Rect;
use ratatui::widgets::{Block, Borders, Paragraph};
use ratatui::Frame;

pub fn render_message_list(frame: &mut Frame<'_>, area: Rect, state: &AppState) {
    let mut lines = Vec::new();
    for message in &state.messages.messages {
        lines.extend(render_message(message, &state.theme));
    }

    let widget = Paragraph::new(lines)
        .scroll((state.messages.scroll_offset, 0))
        .block(Block::default().title("Messages").borders(Borders::ALL));

    frame.render_widget(widget, area);
}
