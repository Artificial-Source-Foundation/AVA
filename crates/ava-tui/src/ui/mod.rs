use crate::app::{AppState, ModalType};
use crate::ui::layout::build_layout;
use crate::widgets::composer::render_composer;
use crate::widgets::message_list::render_message_list;
use ratatui::layout::{Constraint, Direction, Layout, Rect};
use ratatui::style::{Modifier, Style};
use ratatui::text::{Line, Span};
use ratatui::widgets::{Block, Borders, Clear, Paragraph};
use ratatui::Frame;

pub mod layout;
pub mod sidebar;
pub mod status_bar;

pub fn render(frame: &mut Frame<'_>, state: &mut AppState) {
    let split = build_layout(frame.area(), state.show_sidebar);

    status_bar::render_top(frame, split.top_bar, state);
    render_message_list(frame, split.messages, state);
    render_composer(frame, split.composer, state);
    status_bar::render_bottom(frame, split.bottom_bar, state);

    if let Some(sidebar_area) = split.sidebar {
        sidebar::render_sidebar(frame, sidebar_area, state);
    }

    // Render modals on top
    if let Some(modal) = state.active_modal {
        render_modal(frame, state, modal);
    }
}

fn render_modal(frame: &mut Frame<'_>, state: &AppState, modal: ModalType) {
    let area = frame.area();
    let popup_area = centered_rect(60, 70, area);

    // Clear background
    frame.render_widget(Clear, popup_area);

    match modal {
        ModalType::CommandPalette => render_command_palette(frame, popup_area, state),
        ModalType::SessionList => render_session_list(frame, popup_area, state),
        ModalType::ToolApproval => render_tool_approval(frame, popup_area, state),
        ModalType::ModelSelector => render_model_selector(frame, popup_area, state),
        ModalType::ToolList => {
            crate::widgets::tool_list::render_tool_list(
                frame,
                popup_area,
                &state.tool_list,
                &state.theme,
            );
        }
    }
}

fn render_command_palette(frame: &mut Frame<'_>, area: Rect, state: &AppState) {
    let items = state.command_palette.filtered();

    let mut lines = vec![
        Line::from(vec![
            Span::styled("> ", Style::default().fg(state.theme.primary)),
            Span::styled(
                &state.command_palette.query,
                Style::default().fg(state.theme.text),
            ),
            Span::styled("_", Style::default().fg(state.theme.text_muted)),
        ]),
        Line::from(""),
    ];

    for (idx, item) in items.iter().enumerate() {
        let is_selected = idx == state.command_palette.selected;
        let name_style = if is_selected {
            Style::default()
                .fg(state.theme.primary)
                .add_modifier(Modifier::BOLD)
        } else {
            Style::default().fg(state.theme.text)
        };
        let prefix = if is_selected { "> " } else { "  " };

        let mut spans = vec![
            Span::styled(prefix, Style::default().fg(state.theme.primary)),
            Span::styled(&item.name, name_style),
        ];

        if !item.hint.is_empty() {
            spans.push(Span::styled(
                format!("  ({})", item.hint),
                Style::default().fg(state.theme.text_muted),
            ));
        }

        spans.push(Span::styled(
            format!("  {}", item.category),
            Style::default().fg(state.theme.secondary),
        ));

        lines.push(Line::from(spans));
    }

    let widget = Paragraph::new(lines).block(
        Block::default()
            .title("Command Palette")
            .borders(Borders::ALL)
            .border_style(Style::default().fg(state.theme.border)),
    );
    frame.render_widget(widget, area);
}

fn render_session_list(frame: &mut Frame<'_>, area: Rect, state: &AppState) {
    let items = crate::widgets::session_list::filter_sessions(
        &state.session.sessions,
        &state.session_list.query,
    );
    let mut text = format!("> {}\n\n", state.session_list.query);

    for (idx, session) in items.iter().enumerate() {
        let prefix = if idx == state.session_list.selected {
            "> "
        } else {
            "  "
        };
        text.push_str(&format!(
            "{}Session {} ({})\n",
            prefix,
            &session.id.to_string()[..8],
            session.updated_at.format("%Y-%m-%d %H:%M")
        ));
    }

    let widget =
        Paragraph::new(text).block(Block::default().title("Sessions").borders(Borders::ALL));
    frame.render_widget(widget, area);
}

fn render_tool_approval(frame: &mut Frame<'_>, area: Rect, state: &AppState) {
    use crate::widgets::tool_approval::render_tool_approval_lines;

    if let Some(request) = state.permission.queue.front() {
        let lines = render_tool_approval_lines(request, &state.permission, &state.theme);
        let widget = Paragraph::new(lines).block(
            Block::default()
                .title("Tool Approval")
                .borders(Borders::ALL),
        );
        frame.render_widget(widget, area);
    }
}

fn render_model_selector(frame: &mut Frame<'_>, area: Rect, state: &AppState) {
    let selector = match state.model_selector {
        Some(ref s) => s,
        None => return,
    };

    let items = selector.filtered();
    let current_model = &state.agent.model_name;

    let mut lines = vec![
        Line::from(vec![
            Span::styled("> ", Style::default().fg(state.theme.primary)),
            Span::styled(&selector.query, Style::default().fg(state.theme.text)),
            Span::styled("_", Style::default().fg(state.theme.text_muted)),
        ]),
        Line::from(""),
    ];

    for (idx, item) in items.iter().enumerate() {
        let is_selected = idx == selector.selected;
        let is_current = item.model == *current_model
            || format!("{}/{}", item.provider, item.model)
                == format!("{}/{}", state.agent.provider_name, state.agent.model_name);

        let name_style = if is_selected {
            Style::default()
                .fg(state.theme.primary)
                .add_modifier(Modifier::BOLD)
        } else {
            Style::default().fg(state.theme.text)
        };

        let prefix = if is_selected { "> " } else { "  " };
        let marker = if is_current { " *" } else { "" };

        let spans = vec![
            Span::styled(prefix, Style::default().fg(state.theme.primary)),
            Span::styled(&item.display, name_style),
            Span::styled(
                format!("  ({})", item.provider),
                Style::default().fg(state.theme.text_muted),
            ),
            Span::styled(marker, Style::default().fg(state.theme.accent)),
        ];

        lines.push(Line::from(spans));
    }

    let widget = Paragraph::new(lines).block(
        Block::default()
            .title("Select Model")
            .borders(Borders::ALL)
            .border_style(Style::default().fg(state.theme.border)),
    );
    frame.render_widget(widget, area);
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
