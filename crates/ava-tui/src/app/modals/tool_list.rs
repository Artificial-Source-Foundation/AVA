use super::*;

impl App {
    pub(crate) fn handle_tool_list_key(&mut self, key: crossterm::event::KeyEvent) -> bool {
        let vh = list_viewport_height(modal_viewport_height());
        let action = handle_select_list_key(&mut self.state.tool_list.list, key, vh);
        if action == SelectListAction::Cancelled {
            self.state.active_modal = None;
        }
        false
    }
}
