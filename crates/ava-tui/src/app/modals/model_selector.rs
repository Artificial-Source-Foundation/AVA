use super::*;

impl App {
    pub(crate) fn handle_model_selector_key(
        &mut self,
        key: crossterm::event::KeyEvent,
        app_tx: mpsc::UnboundedSender<AppEvent>,
    ) -> bool {
        let Some(ref mut selector) = self.state.model_selector else {
            self.state.active_modal = None;
            return false;
        };

        let vh = list_viewport_height(modal_viewport_height());
        let action = handle_select_list_key(&mut selector.list, key, vh);
        match action {
            SelectListAction::Cancelled => {
                self.state.model_selector = None;
                self.state.active_modal = None;
            }
            SelectListAction::Selected => {
                let Some(selector) = self.state.model_selector.as_ref() else {
                    return false;
                };
                if let Some(mv) = selector.list.selected_value() {
                    let provider = mv.provider.clone();
                    let model = mv.model.clone();
                    let display = mv.display.clone();
                    self.spawn_model_switch(
                        provider,
                        model,
                        display,
                        crate::event::ModelSwitchContext::Selector,
                        app_tx,
                    );
                }
            }
            _ => {}
        }
        false
    }
}
