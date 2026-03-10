use crate::app::{AppState, ModalType};
use crate::ui::layout::build_layout;
use crate::widgets::autocomplete::AutocompleteTrigger;
use crate::widgets::composer::render_composer;
use crate::widgets::message_list::render_message_list;
use crate::widgets::select_list::{render_select_list, KeybindHint, SelectListConfig};
use crate::widgets::slash_menu::render_slash_menu;
use ratatui::layout::{Constraint, Direction, Layout, Rect};
use ratatui::style::Style;
use ratatui::widgets::{Block, Borders, Clear};
use ratatui::Frame;

pub mod layout;
pub mod sidebar;
pub mod status_bar;

pub fn render(frame: &mut Frame<'_>, state: &mut AppState) {
    // Advance spinner animation each frame
    state.messages.advance_spinner();

    // Fill background with deepest bg color — sections layer on top
    let bg_block = Block::default().style(Style::default().bg(state.theme.bg_deep));
    frame.render_widget(bg_block, frame.area());

    let area = frame.area();
    let composer_h = layout::composer_height(&state.input.buffer, area.width, area.height);
    let split = build_layout(area, state.show_sidebar, composer_h);

    // Composer bg (bars handle their own bg internally)
    let composer_bg = Block::default().style(Style::default().bg(state.theme.bg_elevated));
    frame.render_widget(composer_bg, split.composer);

    status_bar::render_top(frame, split.top_bar, state);
    render_message_list(frame, split.messages, state);
    render_composer(frame, split.composer, state);
    status_bar::render_context_bar(frame, split.context_bar, state);

    if let Some(sidebar_area) = split.sidebar {
        sidebar::render_sidebar(frame, sidebar_area, state);
    }

    // Render inline slash menu above the composer (not a modal)
    if let Some(ref ac) = state.input.autocomplete {
        if ac.trigger == AutocompleteTrigger::Slash && !ac.items.is_empty() {
            render_slash_menu(frame, split.composer, ac, &state.theme);
        }
    }

    // Render modals on top
    if let Some(modal) = state.active_modal {
        render_modal(frame, state, modal);
    }
}

fn render_modal(frame: &mut Frame<'_>, state: &AppState, modal: ModalType) {
    let area = frame.area();
    let popup_area = centered_rect(60, 70, area);

    // Dimmed backdrop
    let backdrop = Block::default().style(Style::default().bg(state.theme.bg));
    frame.render_widget(backdrop, area);

    // Clear background and add elevated bg with border
    frame.render_widget(Clear, popup_area);
    let bg = Block::default()
        .style(Style::default().bg(state.theme.bg_elevated))
        .borders(Borders::ALL)
        .border_style(Style::default().fg(state.theme.border));
    let inner = bg.inner(popup_area);
    frame.render_widget(bg, popup_area);

    match modal {
        ModalType::CommandPalette => {
            let config = SelectListConfig {
                title: "Command Palette".to_string(),
                search_placeholder: "Type a command...".to_string(),
                keybinds: vec![
                    KeybindHint { key: "enter".to_string(), label: "run".to_string() },
                    KeybindHint { key: "esc".to_string(), label: "close".to_string() },
                ],
            };
            render_select_list(frame, inner, &state.command_palette.list, &config, &state.theme);
        }
        ModalType::SessionList => {
            let config = SelectListConfig {
                title: "Switch Session".to_string(),
                search_placeholder: "Search sessions...".to_string(),
                keybinds: vec![
                    KeybindHint { key: "enter".to_string(), label: "load".to_string() },
                    KeybindHint { key: "esc".to_string(), label: "close".to_string() },
                ],
            };
            render_select_list(frame, inner, &state.session_list.list, &config, &state.theme);
        }
        ModalType::ToolApproval => {
            if let Some(request) = state.permission.queue.front() {
                crate::widgets::tool_approval::render_tool_approval(
                    frame, popup_area, request, &state.permission, &state.theme,
                );
            }
        }
        ModalType::ModelSelector => {
            if let Some(ref selector) = state.model_selector {
                let config = SelectListConfig {
                    title: "Switch Model".to_string(),
                    search_placeholder: "Search models...".to_string(),
                    keybinds: vec![
                        KeybindHint { key: "\u{2191}\u{2193}".to_string(), label: "navigate".to_string() },
                        KeybindHint { key: "Enter".to_string(), label: "select".to_string() },
                        KeybindHint { key: "Esc".to_string(), label: "close".to_string() },
                    ],
                };
                render_select_list(frame, inner, &selector.list, &config, &state.theme);
            }
        }
        ModalType::ToolList => {
            let config = SelectListConfig {
                title: "Tools".to_string(),
                search_placeholder: "Search tools...".to_string(),
                keybinds: vec![
                    KeybindHint { key: "esc".to_string(), label: "close".to_string() },
                ],
            };
            render_select_list(frame, inner, &state.tool_list.list, &config, &state.theme);
        }
        ModalType::ProviderConnect => {
            crate::widgets::provider_connect::render_provider_connect(
                frame,
                inner,
                state,
            );
        }
    }
}


fn centered_rect(percent_x: u16, percent_y: u16, r: Rect) -> Rect {
    let popup_layout = Layout::default()
        .direction(Direction::Vertical)
        .constraints([
            Constraint::Percentage((100 - percent_y) / 2),
            Constraint::Percentage(percent_y),
            Constraint::Percentage((100 - percent_y) / 2),
        ])
        .split(r);

    Layout::default()
        .direction(Direction::Horizontal)
        .constraints([
            Constraint::Percentage((100 - percent_x) / 2),
            Constraint::Percentage(percent_x),
            Constraint::Percentage((100 - percent_x) / 2),
        ])
        .split(popup_layout[1])[1]
}
