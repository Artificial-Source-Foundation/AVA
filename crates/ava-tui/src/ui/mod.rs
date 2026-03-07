use crate::app::AppState;
use crate::ui::layout::build_layout;
use crate::widgets::composer::render_composer;
use crate::widgets::message_list::render_message_list;
use ratatui::Frame;

pub mod layout;
pub mod sidebar;
pub mod status_bar;

pub fn render(frame: &mut Frame<'_>, state: &AppState) {
    let split = build_layout(frame.area(), state.show_sidebar);

    status_bar::render_top(frame, split.top_bar, state);
    render_message_list(frame, split.messages, state);
    render_composer(frame, split.composer, state);
    status_bar::render_bottom(frame, split.bottom_bar, state);

    if let Some(sidebar_area) = split.sidebar {
        sidebar::render_sidebar(frame, sidebar_area, state);
    }
}
