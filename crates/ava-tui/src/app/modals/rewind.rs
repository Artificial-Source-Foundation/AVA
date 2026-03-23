use super::*;

impl App {
    pub(crate) fn handle_rewind_key(&mut self, key: crossterm::event::KeyEvent) -> bool {
        use crate::state::rewind::RewindOption;

        match key.code {
            KeyCode::Esc => {
                self.state.rewind.close();
                self.state.active_modal = None;
            }
            KeyCode::Up => {
                self.state.rewind.select_prev();
            }
            KeyCode::Down => {
                self.state.rewind.select_next();
            }
            KeyCode::Enter => {
                let option = self.state.rewind.selected();
                self.execute_rewind(option);
            }
            KeyCode::Char('1') => self.execute_rewind(RewindOption::RestoreCodeAndConversation),
            KeyCode::Char('2') => self.execute_rewind(RewindOption::RestoreConversation),
            KeyCode::Char('3') => self.execute_rewind(RewindOption::RestoreCode),
            KeyCode::Char('4') => self.execute_rewind(RewindOption::SummarizeFromHere),
            KeyCode::Char('5') => {
                self.state.rewind.close();
                self.state.active_modal = None;
            }
            _ => {}
        }
        false
    }
}
