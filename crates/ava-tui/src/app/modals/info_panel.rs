use super::*;

impl App {
    pub(crate) fn handle_info_panel_key(&mut self, key: crossterm::event::KeyEvent) -> bool {
        let Some(ref mut panel) = self.state.info_panel else {
            self.state.active_modal = None;
            return false;
        };

        let content_lines = panel.content.lines().count() as u16;
        let vh = modal_viewport_height() as u16;
        // Reserve 2 lines for title + footer
        let max_scroll = content_lines.saturating_sub(vh.saturating_sub(2));

        match key.code {
            KeyCode::Esc | KeyCode::Char('q') => {
                self.state.info_panel = None;
                self.state.active_modal = None;
            }
            KeyCode::Down | KeyCode::Char('j') => {
                panel.scroll = (panel.scroll + 1).min(max_scroll);
            }
            KeyCode::Up | KeyCode::Char('k') => {
                panel.scroll = panel.scroll.saturating_sub(1);
            }
            KeyCode::PageDown => {
                panel.scroll = (panel.scroll + vh.saturating_sub(4)).min(max_scroll);
            }
            KeyCode::PageUp => {
                panel.scroll = panel.scroll.saturating_sub(vh.saturating_sub(4));
            }
            KeyCode::Home | KeyCode::Char('g') => {
                panel.scroll = 0;
            }
            KeyCode::End | KeyCode::Char('G') => {
                panel.scroll = max_scroll;
            }
            _ => {}
        }
        false
    }
}
