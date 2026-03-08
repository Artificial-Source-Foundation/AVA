use crate::state::messages::UiMessage;
use crate::state::theme::Theme;
use ratatui::text::Line;

pub fn render_message(message: &UiMessage, theme: &Theme, spinner_tick: usize) -> Vec<Line<'static>> {
    message.to_lines(theme, spinner_tick)
}
