use crate::app::{AppState, ModalType};
use crate::ui::layout::build_layout;
use crate::widgets::composer::render_composer;
use crate::widgets::message_list::render_message_list;
use ratatui::layout::{Constraint, Direction, Layout, Rect};
use ratatui::widgets::{Block, Borders, Clear, Paragraph};
use ratatui::Frame;

pub mod layout;
pub mod sidebar;
pub mod status_bar;

pub fn render(frame: &mut Frame<'_>, state: &AppState) {
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
    }
}

fn render_command_palette(frame: &mut Frame<'_>, area: Rect, state: &AppState) {
    let items = state.command_palette.filtered();
    let mut text = format!("> {}\n\n", state.command_palette.query);

    for (idx, item) in items.iter().enumerate() {
        let prefix = if idx == state.command_palette.selected {
            "> "
        } else {
            "  "
        };
        text.push_str(&format!(
            "{}{} ({}) - {}\n",
            prefix, item.name, item.hint, item.category
        ));
    }

    let widget = Paragraph::new(text).block(
        Block::default()
            .title("Command Palette")
            .borders(Borders::ALL),
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
        text.push_str(&format!("{}Session {}\n", prefix, session.id));
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
