use super::*;

impl App {
    pub(crate) fn handle_task_list_key(&mut self, key: crossterm::event::KeyEvent) -> bool {
        match key.code {
            KeyCode::Esc => {
                self.state.active_modal = None;
            }
            KeyCode::Up => {
                let mut bg = self
                    .state
                    .background
                    .lock()
                    .unwrap_or_else(|e| e.into_inner());
                bg.select_prev();
            }
            KeyCode::Down => {
                let mut bg = self
                    .state
                    .background
                    .lock()
                    .unwrap_or_else(|e| e.into_inner());
                bg.select_next();
            }
            KeyCode::Enter => {
                let task_id = {
                    let bg = self
                        .state
                        .background
                        .lock()
                        .unwrap_or_else(|e| e.into_inner());
                    bg.selected_task_id()
                };
                if let Some(id) = task_id {
                    self.state.active_modal = None;
                    self.enter_background_task_view(id);
                }
            }
            _ => {}
        }
        false
    }
}
