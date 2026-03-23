mod agent_list;
mod command_palette;
mod copy_picker;
mod diff_preview;
mod info_panel;
mod model_selector;
mod plan_approval;
mod provider_connect;
mod question;
mod rewind;
mod session_list;
mod task_list;
mod theme_selector;
mod tool_approval;
mod tool_list;

use super::*;
use crate::widgets::provider_connect::{ConnectField, ConnectScreen};
use crate::widgets::select_list::{
    handle_select_list_key, handle_select_list_mouse, list_viewport_height, SelectListAction,
    SelectListMouseAction,
};

/// Estimate the model selector viewport height (70% of terminal, minus border).
fn modal_viewport_height() -> usize {
    let (_, rows) = crossterm::terminal::size().unwrap_or((80, 40));
    // Modal is 70% of terminal height, minus 2 for border
    ((rows as usize) * 70 / 100).saturating_sub(2)
}

impl App {
    /// Route paste events to the active modal (e.g., API key input field).
    pub(crate) fn handle_modal_paste(&mut self, value: &str) {
        // Only the ProviderConnect Configure screen has text input fields
        if let Some(ref mut pc) = self.state.provider_connect {
            if let ConnectScreen::Configure(_) = pc.screen {
                match pc.active_field {
                    ConnectField::ApiKey => pc.key_input.push_str(value),
                    ConnectField::BaseUrl => pc.base_url_input.push_str(value),
                }
                return;
            }
        }
        // For search-based modals, route paste to the search query
        match self.state.active_modal {
            Some(ModalType::ModelSelector) => {
                if let Some(ref mut sel) = self.state.model_selector {
                    sel.list.query.push_str(value);
                }
            }
            Some(ModalType::CommandPalette) => {
                self.state.command_palette.list.query.push_str(value);
            }
            Some(ModalType::ToolList) => {
                self.state.tool_list.list.query.push_str(value);
            }
            Some(ModalType::SessionList) => {
                self.state.session_list.list.query.push_str(value);
            }
            Some(ModalType::ProviderConnect) => {
                if let Some(ref mut pc) = self.state.provider_connect {
                    if let ConnectScreen::List = pc.screen {
                        pc.list.query.push_str(value);
                    }
                }
            }
            Some(ModalType::ThemeSelector) => {
                if let Some(ref mut ts) = self.state.theme_selector {
                    ts.query.push_str(value);
                }
            }
            Some(ModalType::AgentList) => {
                if let Some(ref mut al) = self.state.agent_list {
                    al.query.push_str(value);
                }
            }
            Some(ModalType::Question) => {
                if let Some(ref mut q) = self.state.question {
                    if q.options.is_empty() {
                        q.input.push_str(value);
                    }
                }
            }
            Some(ModalType::Rewind | ModalType::DiffPreview | ModalType::InfoPanel) => {
                // These modals have no text input — ignore paste
            }
            _ => {}
        }
    }

    pub(crate) fn handle_modal_key(
        &mut self,
        modal: ModalType,
        key: crossterm::event::KeyEvent,
        app_tx: mpsc::UnboundedSender<AppEvent>,
    ) -> bool {
        if key.kind != KeyEventKind::Press {
            return false;
        }

        match modal {
            ModalType::CommandPalette => self.handle_command_palette_key(key, app_tx.clone()),
            ModalType::SessionList => self.handle_session_list_key(key, app_tx),
            ModalType::ToolApproval => self.handle_tool_approval_key(key),
            ModalType::ModelSelector => self.handle_model_selector_key(key, app_tx),
            ModalType::ToolList => self.handle_tool_list_key(key),
            ModalType::ProviderConnect => self.handle_provider_connect_key(key, app_tx),
            ModalType::ThemeSelector => self.handle_theme_selector_key(key),
            ModalType::AgentList => self.handle_agent_list_key(key),
            ModalType::Question => self.handle_question_key(key),
            ModalType::CopyPicker => self.handle_copy_picker_key(key),
            ModalType::Rewind => self.handle_rewind_key(key),
            ModalType::TaskList => self.handle_task_list_key(key),
            ModalType::DiffPreview => self.handle_diff_preview_key(key),
            ModalType::InfoPanel => self.handle_info_panel_key(key),
            ModalType::PlanApproval => self.handle_plan_approval_key(key, app_tx.clone()),
        }
    }

    /// Route mouse events to the active modal's SelectListState for hover/click/scroll.
    pub(crate) fn handle_modal_mouse(
        &mut self,
        modal: ModalType,
        mouse: crossterm::event::MouseEvent,
        app_tx: mpsc::UnboundedSender<AppEvent>,
    ) {
        let vh = list_viewport_height(modal_viewport_height());

        match modal {
            ModalType::CommandPalette => {
                let action =
                    handle_select_list_mouse(&mut self.state.command_palette.list, mouse, vh);
                if action == SelectListMouseAction::Clicked {
                    // Simulate Enter — execute the selected command
                    let enter = crossterm::event::KeyEvent::from(KeyCode::Enter);
                    self.handle_command_palette_key(enter, app_tx);
                }
            }
            ModalType::SessionList => {
                let action = handle_select_list_mouse(&mut self.state.session_list.list, mouse, vh);
                if action == SelectListMouseAction::Clicked {
                    let enter = crossterm::event::KeyEvent::from(KeyCode::Enter);
                    self.handle_session_list_key(enter, app_tx);
                }
            }
            ModalType::ModelSelector => {
                if let Some(ref mut selector) = self.state.model_selector {
                    let action = handle_select_list_mouse(&mut selector.list, mouse, vh);
                    if action == SelectListMouseAction::Clicked {
                        let enter = crossterm::event::KeyEvent::from(KeyCode::Enter);
                        self.handle_model_selector_key(enter, app_tx);
                    }
                }
            }
            ModalType::ThemeSelector => {
                if let Some(ref mut selector) = self.state.theme_selector {
                    let action = handle_select_list_mouse(selector, mouse, vh);
                    if action == SelectListMouseAction::Clicked {
                        let enter = crossterm::event::KeyEvent::from(KeyCode::Enter);
                        self.handle_theme_selector_key(enter);
                    } else if action == SelectListMouseAction::Scrolled {
                        // Live preview on scroll (same as Moved in key handler)
                        if let Some(name) = self
                            .state
                            .theme_selector
                            .as_ref()
                            .and_then(|s| s.selected_value().cloned())
                        {
                            self.state.theme = Theme::from_name(&name);
                        }
                    }
                }
            }
            ModalType::ToolList => {
                handle_select_list_mouse(&mut self.state.tool_list.list, mouse, vh);
            }
            ModalType::ProviderConnect => {
                // Provider list is short — disable scroll, keep click/hover
                if let Some(ref mut pc) = self.state.provider_connect {
                    if matches!(pc.screen, ConnectScreen::List)
                        && !matches!(
                            mouse.kind,
                            crossterm::event::MouseEventKind::ScrollUp
                                | crossterm::event::MouseEventKind::ScrollDown
                        )
                    {
                        let action = handle_select_list_mouse(&mut pc.list, mouse, vh);
                        if action == SelectListMouseAction::Clicked {
                            let enter = crossterm::event::KeyEvent::from(KeyCode::Enter);
                            self.handle_provider_connect_key(enter, app_tx);
                        }
                    }
                }
            }
            ModalType::AgentList => {
                if let Some(ref mut selector) = self.state.agent_list {
                    let action = handle_select_list_mouse(selector, mouse, vh);
                    if action == SelectListMouseAction::Clicked {
                        let enter = crossterm::event::KeyEvent::from(KeyCode::Enter);
                        self.handle_agent_list_key(enter);
                    }
                }
            }
            ModalType::InfoPanel => {
                // Handle mouse scroll for the info panel
                if let crossterm::event::MouseEventKind::ScrollDown = mouse.kind {
                    if let Some(ref mut panel) = self.state.info_panel {
                        let content_lines = panel.content.lines().count() as u16;
                        let vh = modal_viewport_height() as u16;
                        let max_scroll = content_lines.saturating_sub(vh.saturating_sub(2));
                        panel.scroll = (panel.scroll + 3).min(max_scroll);
                    }
                } else if let crossterm::event::MouseEventKind::ScrollUp = mouse.kind {
                    if let Some(ref mut panel) = self.state.info_panel {
                        panel.scroll = panel.scroll.saturating_sub(3);
                    }
                }
            }
            // Non-SelectList modals — no mouse handling
            ModalType::ToolApproval
            | ModalType::PlanApproval
            | ModalType::Question
            | ModalType::CopyPicker
            | ModalType::Rewind
            | ModalType::TaskList
            | ModalType::DiffPreview => {}
        }
    }
}
