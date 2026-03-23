use super::*;

impl App {
    pub(crate) fn handle_question_key(&mut self, key: crossterm::event::KeyEvent) -> bool {
        let Some(ref mut q) = self.state.question else {
            self.state.active_modal = None;
            return false;
        };

        if q.options.is_empty() {
            // Free-text input mode
            match key.code {
                KeyCode::Enter => {
                    let answer = q.input.clone();
                    if let Some(reply) = q.reply.take() {
                        let _ = reply.send(answer);
                    }
                    self.state.question = None;
                    self.state.active_modal = None;
                }
                KeyCode::Esc => {
                    if let Some(reply) = q.reply.take() {
                        let _ = reply.send(String::new());
                    }
                    self.state.question = None;
                    self.state.active_modal = None;
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
                    if let Some(reply) = q.reply.take() {
                        let _ = reply.send(answer);
                    }
                    self.state.question = None;
                    self.state.active_modal = None;
                }
                KeyCode::Esc => {
                    if let Some(reply) = q.reply.take() {
                        let _ = reply.send(String::new());
                    }
                    self.state.question = None;
                    self.state.active_modal = None;
                }
                _ => {}
            }
        }
        false
    }
}
