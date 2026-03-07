use crate::app::AppState;
use ratatui::layout::Rect;
use ratatui::widgets::{Block, Borders, Paragraph};
use ratatui::Frame;

pub fn render_composer(frame: &mut Frame<'_>, area: Rect, state: &AppState) {
    let widget = Paragraph::new(state.input.buffer.as_str())
        .block(Block::default().title("Composer").borders(Borders::ALL));
    frame.render_widget(widget, area);
}
