use super::*;

impl App {
    pub(crate) fn handle_agent_list_key(&mut self, key: crossterm::event::KeyEvent) -> bool {
        let Some(ref mut selector) = self.state.agent_list else {
            self.state.active_modal = None;
            return false;
        };

        let vh = list_viewport_height(modal_viewport_height());
        let action = handle_select_list_key(selector, key, vh);
        match action {
            SelectListAction::Cancelled | SelectListAction::Selected => {
                self.state.agent_list = None;
                self.state.active_modal = None;
            }
            _ => {}
        }
        false
    }
}
