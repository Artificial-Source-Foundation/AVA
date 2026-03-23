use super::*;

impl App {
    pub(crate) fn handle_copy_picker_key(&mut self, key: crossterm::event::KeyEvent) -> bool {
        let Some(picker) = self.state.copy_picker.take() else {
            self.state.active_modal = None;
            return false;
        };

        match key.code {
            KeyCode::Esc => {
                self.state.active_modal = None;
            }
            KeyCode::Char('a') => {
                self.state.active_modal = None;
                let line_count = picker.full_content.lines().count();
                self.copy_to_clipboard(
                    &picker.full_content,
                    Some(format!(
                        "Copied entire response ({line_count} lines) to clipboard"
                    )),
                );
            }
            KeyCode::Char(ch) if ch.is_ascii_digit() && ch != '0' => {
                let idx = (ch as usize) - ('1' as usize);
                if let Some(block) = picker.blocks.get(idx) {
                    self.state.active_modal = None;
                    let lang = if block.language.is_empty() {
                        "code"
                    } else {
                        &block.language
                    };
                    let line_count = block.content.lines().count();
                    let label = format!("Copied {lang} block ({line_count} lines) to clipboard");
                    self.copy_to_clipboard(&block.content, Some(label));
                } else {
                    // Index out of range — put picker back
                    self.state.copy_picker = Some(picker);
                }
            }
            _ => {
                // Unrecognized key — put picker back
                self.state.copy_picker = Some(picker);
            }
        }
        false
    }
}
