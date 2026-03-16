use crate::app::{AppState, ModalType};
use crate::ui::layout::build_layout;
use crate::widgets::autocomplete::AutocompleteTrigger;
use crate::widgets::composer::render_composer;
use crate::widgets::mention_picker::render_mention_picker;
use crate::widgets::message_list::render_message_list;
use crate::widgets::select_list::{render_select_list, KeybindHint, SelectListConfig};
use crate::widgets::slash_menu::render_slash_menu;
use ratatui::layout::{Constraint, Direction, Layout, Rect};
use ratatui::style::Style;
use ratatui::text::{Line, Span};
use ratatui::widgets::{Block, Borders, Clear, Paragraph};
use ratatui::Frame;

pub mod layout;
pub mod sidebar;
pub mod status_bar;

pub fn render(frame: &mut Frame<'_>, state: &mut AppState) {
    // Advance spinner animation each frame
    state.messages.advance_spinner();

    // Nuclear clear: reset every cell in the terminal buffer, then fill with
    // the deepest background color.  This prevents stale character artifacts
    // (e.g. `ke ..`, `ac`, `n.`) that survive tab-switching or resize.
    let full_area = frame.area();
    frame.render_widget(Clear, full_area);
    let bg_block = Block::default().style(Style::default().bg(state.theme.bg_deep));
    frame.render_widget(bg_block, full_area);

    let area = frame.area();

    // When tool approval is pending, replace the composer with the approval dock
    let approval_active = matches!(state.active_modal, Some(ModalType::ToolApproval));
    let composer_h = if approval_active {
        crate::widgets::tool_approval::APPROVAL_DOCK_HEIGHT
    } else {
        layout::composer_height(&state.input.buffer, area.width, area.height)
    };
    let split = build_layout(area, state.show_sidebar, composer_h);

    status_bar::render_top(frame, split.top_bar, state);
    render_message_list(frame, split.messages, state);

    if approval_active {
        // Render the approval dock in place of the composer
        if let Some(request) = state.permission.queue.front() {
            let dock_bg = Block::default().style(Style::default().bg(state.theme.bg_elevated));
            frame.render_widget(dock_bg, split.composer);

            crate::widgets::tool_approval::render_tool_approval(
                frame,
                split.composer,
                request,
                &state.permission,
                &state.theme,
            );
        }
    } else {
        // Composer bg drawn AFTER the message list so it acts as a curtain
        // covering any streaming text that overflows into the composer area.
        let composer_bg = Block::default().style(Style::default().bg(state.theme.bg_elevated));
        frame.render_widget(composer_bg, split.composer);

        render_composer(frame, split.composer, state);
    }
    status_bar::render_context_bar(frame, split.context_bar, state);

    if let Some(sidebar_area) = split.sidebar {
        sidebar::render_sidebar(frame, sidebar_area, state);
    }

    // Render inline slash menu or mention picker above the composer (not a modal)
    if let Some(ref ac) = state.input.autocomplete {
        if ac.trigger == AutocompleteTrigger::Slash && !ac.items.is_empty() {
            render_slash_menu(frame, split.composer, ac, &state.theme);
        } else if ac.trigger == AutocompleteTrigger::AtMention && !ac.items.is_empty() {
            render_mention_picker(frame, split.composer, ac, &state.theme);
        }
    }

    // Render modals on top (ToolApproval is rendered inline as a dock, not as a modal overlay)
    if let Some(modal) = state.active_modal {
        if modal != ModalType::ToolApproval {
            render_modal(frame, state, modal);
        }
    }

    // Render toast notifications (top-right overlay, above everything)
    render_toasts(frame, area, state);
}

fn render_toasts(frame: &mut Frame<'_>, area: Rect, state: &mut AppState) {
    state.toast.cleanup();
    if state.toast.is_empty() {
        return;
    }
    let theme = &state.theme;
    for (i, toast) in state.toast.toasts.iter().rev().enumerate() {
        let text_width = toast.message.len() as u16 + 2; // 1 padding each side
        let width = text_width.clamp(10, 40);
        let y = 1 + (i as u16 * 2);
        if y >= area.height {
            break;
        }
        let x = area.width.saturating_sub(width + 1);
        let rect = Rect::new(x, y, width, 1);
        let block = Block::default()
            .borders(Borders::NONE)
            .style(Style::default().bg(theme.bg_elevated));
        let text = Paragraph::new(Line::from(Span::styled(
            format!(" {} ", toast.message),
            Style::default().fg(theme.text_muted).bg(theme.bg_elevated),
        )))
        .block(block);
        frame.render_widget(Clear, rect);
        frame.render_widget(text, rect);
    }
}

fn render_modal(frame: &mut Frame<'_>, state: &mut AppState, modal: ModalType) {
    let area = frame.area();
    // Use a smaller popup for the copy picker, larger for diff preview
    let popup_area = if matches!(modal, ModalType::DiffPreview) {
        centered_rect(85, 90, area)
    } else if matches!(modal, ModalType::CopyPicker) {
        centered_rect(50, 40, area)
    } else if matches!(modal, ModalType::Rewind) {
        centered_rect(60, 60, area)
    } else if matches!(modal, ModalType::InfoPanel) {
        centered_rect(70, 80, area)
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
                    KeybindHint {
                        key: "enter".to_string(),
                        label: "run".to_string(),
                    },
                    KeybindHint {
                        key: "esc".to_string(),
                        label: "close".to_string(),
                    },
                ],
            };
            render_select_list(
                frame,
                inner,
                &mut state.command_palette.list,
                &config,
                &state.theme,
            );
        }
        ModalType::SessionList => {
            let config = SelectListConfig {
                title: "Switch Session".to_string(),
                search_placeholder: "Search sessions...".to_string(),
                keybinds: vec![
                    KeybindHint {
                        key: "enter".to_string(),
                        label: "load".to_string(),
                    },
                    KeybindHint {
                        key: "esc".to_string(),
                        label: "close".to_string(),
                    },
                ],
            };
            render_select_list(
                frame,
                inner,
                &mut state.session_list.list,
                &config,
                &state.theme,
            );
        }
        ModalType::ToolApproval => {
            // Handled inline as a bottom dock bar — should not reach here.
        }
        ModalType::ModelSelector => {
            if let Some(ref mut selector) = state.model_selector {
                let config = SelectListConfig {
                    title: "Switch Model".to_string(),
                    search_placeholder: "Search models...".to_string(),
                    keybinds: vec![
                        KeybindHint {
                            key: "\u{2191}\u{2193}".to_string(),
                            label: "navigate".to_string(),
                        },
                        KeybindHint {
                            key: "Enter".to_string(),
                            label: "select".to_string(),
                        },
                        KeybindHint {
                            key: "Esc".to_string(),
                            label: "close".to_string(),
                        },
                    ],
                };
                render_select_list(frame, inner, &mut selector.list, &config, &state.theme);
            }
        }
        ModalType::ToolList => {
            let config = SelectListConfig {
                title: "Tools".to_string(),
                search_placeholder: "Search tools...".to_string(),
                keybinds: vec![KeybindHint {
                    key: "esc".to_string(),
                    label: "close".to_string(),
                }],
            };
            render_select_list(
                frame,
                inner,
                &mut state.tool_list.list,
                &config,
                &state.theme,
            );
        }
        ModalType::ProviderConnect => {
            crate::widgets::provider_connect::render_provider_connect(frame, inner, state);
        }
        ModalType::AgentList => {
            if let Some(ref mut selector) = state.agent_list {
                let config = SelectListConfig {
                    title: "Sub-Agents".to_string(),
                    search_placeholder: "Search agents...".to_string(),
                    keybinds: vec![
                        KeybindHint {
                            key: "Esc".to_string(),
                            label: "close".to_string(),
                        },
                        KeybindHint {
                            key: "hint".to_string(),
                            label: "edit .ava/agents.toml to configure".to_string(),
                        },
                    ],
                };
                render_select_list(frame, inner, &mut *selector, &config, &state.theme);
            }
        }
        ModalType::ThemeSelector => {
            if let Some(ref mut selector) = state.theme_selector {
                let config = SelectListConfig {
                    title: "Switch Theme".to_string(),
                    search_placeholder: "Search themes...".to_string(),
                    keybinds: vec![
                        KeybindHint {
                            key: "\u{2191}\u{2193}".to_string(),
                            label: "navigate".to_string(),
                        },
                        KeybindHint {
                            key: "Enter".to_string(),
                            label: "select".to_string(),
                        },
                        KeybindHint {
                            key: "Esc".to_string(),
                            label: "close".to_string(),
                        },
                    ],
                };
                render_select_list(frame, inner, &mut *selector, &config, &state.theme);
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
                frame,
                inner,
                &state.rewind,
                &state.theme,
            );
        }
        ModalType::TaskList => {
            let bg = state.background.lock().unwrap();
            crate::widgets::task_list_modal::render_task_list(
                frame,
                inner,
                &bg,
                &state.theme,
                state.messages.spinner_tick,
            );
        }
        ModalType::DiffPreview => {
            if let Some(ref preview) = state.diff_preview {
                crate::widgets::diff_preview::render_diff_preview(
                    frame,
                    popup_area,
                    preview,
                    &state.theme,
                );
            }
        }
        ModalType::InfoPanel => {
            render_info_panel(frame, inner, state);
        }
    }
}

fn render_info_panel(frame: &mut Frame<'_>, area: Rect, state: &AppState) {
    use ratatui::style::Modifier;
    use ratatui::widgets::Wrap;

    let Some(ref panel) = state.info_panel else {
        return;
    };

    // Title line
    let title_line = Line::from(Span::styled(
        &panel.title,
        Style::default()
            .fg(state.theme.text)
            .add_modifier(Modifier::BOLD),
    ));

    // Build content lines, highlighting command names (lines starting with /)
    let content_lines: Vec<Line<'_>> = panel
        .content
        .lines()
        .map(|line| {
            let trimmed = line.trim_start();
            if let Some(rest) = trimmed.strip_prefix('/') {
                // Split at first whitespace after the command name
                let cmd_end = rest.find(|c: char| c.is_whitespace()).unwrap_or(rest.len());
                let cmd = &trimmed[..cmd_end + 1]; // includes the leading /
                let desc = &trimmed[cmd_end + 1..];
                Line::from(vec![
                    Span::styled(
                        cmd.to_string(),
                        Style::default()
                            .fg(state.theme.accent)
                            .add_modifier(Modifier::BOLD),
                    ),
                    Span::styled(
                        desc.to_string(),
                        Style::default().fg(state.theme.text_muted),
                    ),
                ])
            } else {
                Line::from(Span::styled(
                    line.to_string(),
                    Style::default().fg(state.theme.text),
                ))
            }
        })
        .collect();

    let total_lines = content_lines.len() as u16;

    // Layout: title (1 line) + separator (1 line) + content (dynamic) + footer (1 line)
    let chunks = Layout::default()
        .direction(Direction::Vertical)
        .constraints([
            Constraint::Length(1), // title
            Constraint::Length(1), // separator
            Constraint::Min(1),    // content
            Constraint::Length(1), // footer
        ])
        .split(area);

    // Render title
    frame.render_widget(Paragraph::new(title_line), chunks[0]);

    // Render separator
    let sep = Line::from(Span::styled(
        "\u{2500}".repeat(chunks[1].width as usize),
        Style::default().fg(state.theme.border),
    ));
    frame.render_widget(Paragraph::new(sep), chunks[1]);

    // Render scrollable content
    let content_height = chunks[2].height;
    let content_paragraph = Paragraph::new(content_lines)
        .wrap(Wrap { trim: false })
        .scroll((panel.scroll, 0));
    frame.render_widget(content_paragraph, chunks[2]);

    // Footer with scroll indicator
    let can_scroll = total_lines > content_height;
    let footer_text = if can_scroll {
        format!(
            " Esc close | \u{2191}/\u{2193} scroll | {}/{}",
            panel.scroll + 1,
            total_lines.saturating_sub(content_height) + 1
        )
    } else {
        " Esc close".to_string()
    };
    let footer = Line::from(Span::styled(
        footer_text,
        Style::default().fg(state.theme.text_muted),
    ));
    frame.render_widget(Paragraph::new(footer), chunks[3]);
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
        Style::default()
            .fg(state.theme.text)
            .add_modifier(Modifier::BOLD),
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
                (
                    "> ",
                    Style::default()
                        .fg(state.theme.accent)
                        .add_modifier(Modifier::BOLD),
                )
            } else {
                ("  ", Style::default().fg(state.theme.text))
            };
            lines.push(Line::from(Span::styled(format!("{prefix}{opt}"), style)));
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

    let Some(ref picker) = state.copy_picker else {
        return;
    };

    let mut lines: Vec<Line<'_>> = Vec::new();

    // Title
    lines.push(Line::from(Span::styled(
        "Copy Code Block",
        Style::default()
            .fg(state.theme.text)
            .add_modifier(Modifier::BOLD),
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
