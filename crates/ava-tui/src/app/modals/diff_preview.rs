use super::*;

impl App {
    pub(crate) fn handle_diff_preview_key(&mut self, key: crossterm::event::KeyEvent) -> bool {
        let Some(ref mut preview) = self.state.diff_preview else {
            self.state.active_modal = None;
            return false;
        };

        match key.code {
            KeyCode::Char('y') => {
                preview.accept_selected();
            }
            KeyCode::Char('n') => {
                preview.reject_selected();
            }
            KeyCode::Char('a') => {
                preview.accept_all();
            }
            KeyCode::Char('d') => {
                preview.reject_all();
            }
            KeyCode::Char('j') | KeyCode::Down => {
                preview.select_next();
            }
            KeyCode::Char('k') | KeyCode::Up => {
                preview.select_prev();
            }
            KeyCode::Tab => {
                preview.next_file();
            }
            KeyCode::Enter => {
                // Apply accepted hunks
                if let Some(preview) = self.state.diff_preview.take() {
                    let results = preview.apply_accepted();
                    let count = results.len();
                    for (path, content) in results {
                        if let Err(e) = std::fs::write(&path, &content) {
                            self.set_status(
                                format!("Failed to write {}: {e}", path.display()),
                                StatusLevel::Error,
                            );
                        }
                    }
                    if count > 0 {
                        self.set_status(
                            format!("Applied accepted hunks to {count} file(s)"),
                            StatusLevel::Info,
                        );
                    } else {
                        self.set_status("No hunks accepted", StatusLevel::Info);
                    }
                }
                self.state.active_modal = None;
            }
            KeyCode::Esc => {
                // Reject all and close
                self.state.diff_preview = None;
                self.state.active_modal = None;
                self.set_status("Diff preview cancelled", StatusLevel::Info);
            }
            _ => {}
        }
        false
    }
}
