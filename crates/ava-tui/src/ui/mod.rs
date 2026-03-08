use crate::app::{AppState, ModalType};
use crate::ui::layout::build_layout;
use crate::widgets::composer::{render_composer, render_separator};
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
    // Advance spinner animation each frame
    state.messages.advance_spinner();

    // Fill background with theme bg color
    let bg_block = Block::default().style(Style::default().bg(state.theme.bg));
    frame.render_widget(bg_block, frame.area());

    let composer_h = layout::composer_height(&state.input.buffer, frame.area().width);
    let split = build_layout(frame.area(), state.show_sidebar, composer_h);

    status_bar::render_top(frame, split.top_bar, state);
    render_message_list(frame, split.messages, state);
    render_separator(frame, split.separator, state);
    render_composer(frame, split.composer, state);
    status_bar::render_context_bar(frame, split.context_bar, state);

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

    // Clear background and add subtle elevated bg
    frame.render_widget(Clear, popup_area);
    let bg = Block::default()
        .style(Style::default().bg(state.theme.bg_elevated))
        .borders(Borders::ALL)
        .border_style(Style::default().fg(state.theme.border));
    let inner = bg.inner(popup_area);
    frame.render_widget(bg, popup_area);

    match modal {
        ModalType::CommandPalette => render_command_palette(frame, inner, state),
        ModalType::SessionList => render_session_list(frame, inner, state),
        ModalType::ToolApproval => render_tool_approval(frame, inner, state),
        ModalType::ModelSelector => render_model_selector(frame, inner, state),
        ModalType::ToolList => {
            crate::widgets::tool_list::render_tool_list(
                frame,
                inner,
                &state.tool_list,
                &state.theme,
            );
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

fn render_command_palette(frame: &mut Frame<'_>, area: Rect, state: &AppState) {
    let items = state.command_palette.filtered();

    let mut lines = vec![
        Line::from(vec![
            Span::styled("> ", Style::default().fg(state.theme.primary)),
            Span::styled(
                &state.command_palette.query,
                Style::default().fg(state.theme.text),
            ),
            Span::styled("_", Style::default().fg(state.theme.text_dimmed)),
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
            Style::default().fg(state.theme.text_dimmed),
        ));

        lines.push(Line::from(spans));
    }

    let widget = Paragraph::new(lines);
    frame.render_widget(widget, area);
}

fn render_session_list(frame: &mut Frame<'_>, area: Rect, state: &AppState) {
    let items = crate::widgets::session_list::filter_sessions(
        &state.session.sessions,
        &state.session_list.query,
    );

    let mut lines = vec![
        Line::from(vec![
            Span::styled("> ", Style::default().fg(state.theme.primary)),
            Span::styled(
                &state.session_list.query,
                Style::default().fg(state.theme.text),
            ),
            Span::styled("_", Style::default().fg(state.theme.text_dimmed)),
        ]),
        Line::from(""),
    ];

    for (idx, session) in items.iter().enumerate() {
        let is_selected = idx == state.session_list.selected;
        let prefix = if is_selected { "> " } else { "  " };
        let name_style = if is_selected {
            Style::default()
                .fg(state.theme.primary)
                .add_modifier(Modifier::BOLD)
        } else {
            Style::default().fg(state.theme.text)
        };

        lines.push(Line::from(vec![
            Span::styled(prefix, Style::default().fg(state.theme.primary)),
            Span::styled(
                format!("Session {}", &session.id.to_string()[..8]),
                name_style,
            ),
            Span::styled(
                format!("  {}", session.updated_at.format("%Y-%m-%d %H:%M")),
                Style::default().fg(state.theme.text_muted),
            ),
        ]));
    }

    let widget = Paragraph::new(lines);
    frame.render_widget(widget, area);
}

fn render_tool_approval(frame: &mut Frame<'_>, area: Rect, state: &AppState) {
    use crate::widgets::tool_approval::render_tool_approval_lines;

    if let Some(request) = state.permission.queue.front() {
        let lines = render_tool_approval_lines(request, &state.permission, &state.theme);
        let widget = Paragraph::new(lines);
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
            Span::styled("_", Style::default().fg(state.theme.text_dimmed)),
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

    let widget = Paragraph::new(lines);
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
