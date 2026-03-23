use super::*;
use crate::widgets::command_palette::CommandExec;

impl App {
    pub(crate) fn handle_command_palette_key(
        &mut self,
        key: crossterm::event::KeyEvent,
        app_tx: mpsc::UnboundedSender<AppEvent>,
    ) -> bool {
        let vh = list_viewport_height(modal_viewport_height());
        let action = handle_select_list_key(&mut self.state.command_palette.list, key, vh);
        match action {
            SelectListAction::Cancelled => {
                self.state.command_palette.open = false;
                self.state.active_modal = None;
            }
            SelectListAction::Selected => {
                if let Some(item) = self.state.command_palette.list.selected_item() {
                    match &item.value {
                        CommandExec::Action(action) => {
                            let action = *action;
                            self.state.command_palette.open = false;
                            self.state.active_modal = None;
                            self.execute_command_action(action, Some(app_tx.clone()));
                        }
                        CommandExec::Slash(cmd) => {
                            let cmd = cmd.clone();
                            self.state.command_palette.open = false;
                            self.state.active_modal = None;
                            if let Some((kind, msg)) =
                                self.handle_slash_command(&cmd, Some(app_tx.clone()))
                            {
                                self.state.messages.push(UiMessage::new(kind, msg));
                            }
                        }
                    }
                } else {
                    self.state.command_palette.open = false;
                    self.state.active_modal = None;
                }
            }
            _ => {}
        }
        false
    }
}
