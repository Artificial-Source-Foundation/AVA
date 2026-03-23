use super::*;

impl App {
    pub(crate) fn handle_theme_selector_key(&mut self, key: crossterm::event::KeyEvent) -> bool {
        let Some(ref mut selector) = self.state.theme_selector else {
            self.state.active_modal = None;
            return false;
        };

        let vh = list_viewport_height(modal_viewport_height());
        let action = handle_select_list_key(selector, key, vh);
        match action {
            SelectListAction::Cancelled => {
                // Revert to the original theme saved before preview
                if let Some(original) = self.state.theme_before_preview.take() {
                    self.state.theme = original;
                }
                self.state.theme_selector = None;
                self.state.active_modal = None;
            }
            SelectListAction::Selected => {
                // Confirm the previewed theme
                if let Some(name) = self
                    .state
                    .theme_selector
                    .as_ref()
                    .and_then(|s| s.selected_value().cloned())
                {
                    self.state.theme = Theme::from_name(&name);
                    self.set_status(format!("Theme: {name}"), StatusLevel::Info);
                }
                self.state.theme_before_preview = None;
                self.state.theme_selector = None;
                self.state.active_modal = None;
            }
            SelectListAction::Moved => {
                // Live preview: apply the highlighted theme immediately
                if let Some(name) = self
                    .state
                    .theme_selector
                    .as_ref()
                    .and_then(|s| s.selected_value().cloned())
                {
                    self.state.theme = Theme::from_name(&name);
                }
            }
            _ => {}
        }
        false
    }
}
