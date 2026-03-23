use super::*;

impl App {
    pub(crate) fn handle_session_list_key(
        &mut self,
        key: crossterm::event::KeyEvent,
        app_tx: mpsc::UnboundedSender<AppEvent>,
    ) -> bool {
        let vh = list_viewport_height(modal_viewport_height());
        let action = handle_select_list_key(&mut self.state.session_list.list, key, vh);
        match action {
            SelectListAction::Cancelled => {
                self.state.session_list.open = false;
                self.state.active_modal = None;
            }
            SelectListAction::Selected => {
                if let Some(session_id) = self.state.session_list.list.selected_value().copied() {
                    if session_id.is_nil() {
                        // "New Session" selected
                        let _ = self.state.session.create_session();
                        self.state.messages.messages.clear();
                        self.state.messages.reset_scroll();
                        self.set_status("New session created", StatusLevel::Info);
                    } else {
                        self.spawn_session_load(session_id, app_tx.clone());
                    }
                }
                self.state.session_list.open = false;
                self.state.active_modal = None;
            }
            _ => {}
        }
        false
    }
}
