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

    // Render /btw overlay on top of everything (including modals)
    if state.btw.pending || state.btw.response.is_some() {
        crate::widgets::btw_overlay::render_btw_overlay(frame, &state.btw, &state.theme);
    }
}

fn render_modal(frame: &mut Frame<'_>, state: &AppState, modal: ModalType) {
    let area = frame.area();
    // Use a smaller popup for the copy picker
    let popup_area = if matches!(modal, ModalType::CopyPicker) {
        centered_rect(50, 40, area)
    } else if matches!(modal, ModalType::Rewind) {
        centered_rect(60, 60, area)
    } else {
        centered_rect(60, 70, area)
    };

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
        ModalType::AgentList => {
            if let Some(ref selector) = state.agent_list {
                let config = SelectListConfig {
                    title: "Sub-Agents".to_string(),
                    search_placeholder: "Search agents...".to_string(),
                    keybinds: vec![
                        KeybindHint { key: "Esc".to_string(), label: "close".to_string() },
                        KeybindHint { key: "hint".to_string(), label: "edit .ava/agents.toml to configure".to_string() },
                    ],
                };
                render_select_list(frame, inner, selector, &config, &state.theme);
            }
        }
        ModalType::ThemeSelector => {
            if let Some(ref selector) = state.theme_selector {
                let config = SelectListConfig {
                    title: "Switch Theme".to_string(),
                    search_placeholder: "Search themes...".to_string(),
                    keybinds: vec![
                        KeybindHint { key: "\u{2191}\u{2193}".to_string(), label: "navigate".to_string() },
                        KeybindHint { key: "Enter".to_string(), label: "select".to_string() },
                        KeybindHint { key: "Esc".to_string(), label: "close".to_string() },
                    ],
                };
                render_select_list(frame, inner, selector, &config, &state.theme);
            }
        }
        ModalType::Question => {
            render_question_modal(frame, inner, state);
        }
        ModalType::CopyPicker => {
            render_copy_picker(frame, inner, state);
        }
        ModalType::Rewind => {
            crate::widgets::rewind_modal::render_rewind_modal(
                frame, inner, &state.rewind, &state.theme,
            );
        }
    }
}

fn render_question_modal(frame: &mut Frame<'_>, area: Rect, state: &AppState) {
    use ratatui::style::Modifier;
    use ratatui::text::{Line, Span};
    use ratatui::widgets::Paragraph;

    let Some(ref q) = state.question else { return };

    let mut lines: Vec<Line<'_>> = Vec::new();

    // Question text
    lines.push(Line::from(Span::styled(
        &q.question,
        Style::default().fg(state.theme.text).add_modifier(Modifier::BOLD),
    )));
    lines.push(Line::from(""));

    if q.options.is_empty() {
        // Free-text input
        lines.push(Line::from(Span::styled(
            "Type your answer:",
            Style::default().fg(state.theme.text_muted),
        )));
        lines.push(Line::from(""));

        let cursor_line = format!("> {}_", q.input);
        lines.push(Line::from(Span::styled(
            cursor_line,
            Style::default().fg(state.theme.accent),
        )));
    } else {
        // Options list
        for (i, opt) in q.options.iter().enumerate() {
            let (prefix, style) = if i == q.selected {
                ("> ", Style::default().fg(state.theme.accent).add_modifier(Modifier::BOLD))
            } else {
                ("  ", Style::default().fg(state.theme.text))
            };
            lines.push(Line::from(Span::styled(
                format!("{prefix}{opt}"),
                style,
            )));
        }
    }

    lines.push(Line::from(""));
    lines.push(Line::from(Span::styled(
        if q.options.is_empty() {
            "[Enter] submit  [Esc] decline"
        } else {
            "[↑↓] navigate  [Enter] select  [Esc] decline"
        },
        Style::default().fg(state.theme.text_muted),
    )));

    let title = "Agent Question";
    let block = Block::default()
        .title(title)
        .borders(Borders::ALL)
        .border_style(Style::default().fg(state.theme.accent));
    let paragraph = Paragraph::new(lines).block(block);
    frame.render_widget(paragraph, area);
}


fn render_copy_picker(frame: &mut Frame<'_>, area: Rect, state: &AppState) {
    use ratatui::style::Modifier;
    use ratatui::text::{Line, Span};
    use ratatui::widgets::Paragraph;

    let Some(ref picker) = state.copy_picker else { return };

    let mut lines: Vec<Line<'_>> = Vec::new();

    // Title
    lines.push(Line::from(Span::styled(
        "Copy Code Block",
        Style::default().fg(state.theme.text).add_modifier(Modifier::BOLD),
    )));
    lines.push(Line::from(""));

    // List code blocks (up to 9)
    for (i, block) in picker.blocks.iter().enumerate().take(9) {
        let lang = if block.language.is_empty() {
            "code".to_string()
        } else {
            block.language.clone()
        };
        let line_count = block.content.lines().count();
        let label = format!(
            "  {}. {} (lines {}-{}, {} lines)",
            i + 1,
            lang,
            block.start_line,
            block.end_line,
            line_count,
        );
        lines.push(Line::from(Span::styled(
            label,
            Style::default().fg(state.theme.text),
        )));
    }

    lines.push(Line::from(""));

    // "Copy entire response" option
    lines.push(Line::from(Span::styled(
        "  a. Copy entire response",
        Style::default().fg(state.theme.accent),
    )));

    lines.push(Line::from(""));

    // Hint
    lines.push(Line::from(Span::styled(
        "Press 1-9 to copy, Esc to cancel",
        Style::default().fg(state.theme.text_muted),
    )));

    let block = Block::default()
        .title(" Copy Code Block ")
        .borders(Borders::ALL)
        .border_style(Style::default().fg(state.theme.border));
    let paragraph = Paragraph::new(lines).block(block);
    frame.render_widget(paragraph, area);
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
