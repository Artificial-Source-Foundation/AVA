use super::*;

impl App {
    pub(crate) fn handle_question_key(
        &mut self,
        key: crossterm::event::KeyEvent,
        app_tx: mpsc::UnboundedSender<AppEvent>,
    ) -> bool {
        let Some(ref mut q) = self.state.question else {
            self.state.active_modal = None;
            return false;
        };

        let mut resolution: Option<(String, Option<String>, String)> = None;

        if q.options.is_empty() {
            // Free-text input mode
            match key.code {
                KeyCode::Enter => {
                    resolution = Some((q.request_id.clone(), q.run_id.clone(), q.input.clone()));
                }
                KeyCode::Esc => {
                    resolution = Some((q.request_id.clone(), q.run_id.clone(), String::new()));
                }
                KeyCode::Char(ch) => {
                    q.input.push(ch);
                }
                KeyCode::Backspace => {
                    q.input.pop();
                }
                _ => {}
            }
        } else {
            // Options selection mode
            match key.code {
                KeyCode::Up => {
                    q.selected = q.selected.saturating_sub(1);
                }
                KeyCode::Down => {
                    if q.selected + 1 < q.options.len() {
                        q.selected += 1;
                    }
                }
                KeyCode::Enter => {
                    let answer = q.options.get(q.selected).cloned().unwrap_or_default();
                    resolution = Some((q.request_id.clone(), q.run_id.clone(), answer));
                }
                KeyCode::Esc => {
                    resolution = Some((q.request_id.clone(), q.run_id.clone(), String::new()));
                }
                _ => {}
            }
        }

        if let Some((request_id, run_id, answer)) = resolution {
            self.state.question = None;
            self.state.active_modal = None;
            self.promote_next_queued_interactive_modal(app_tx.clone());
            self.resolve_question_request(request_id, run_id, answer, app_tx);
        }

        false
    }
}
