use ratatui::layout::{Constraint, Direction, Layout, Rect};

pub struct MainLayout {
    pub top_bar: Rect,
    pub messages: Rect,
    pub separator: Rect,
    pub composer: Rect,
    pub context_bar: Rect,
    pub sidebar: Option<Rect>,
}

/// Calculate how many lines the composer needs based on buffer content.
pub fn composer_height(buffer: &str, available_width: u16) -> u16 {
    // Inner width = total width minus 2 ("❯ " prefix)
    let inner_width = available_width.saturating_sub(2).max(1) as usize;
    let text_len = buffer.len();
    let lines = if text_len == 0 {
        1
    } else {
        ((text_len + inner_width - 1) / inner_width).max(1)
    };
    // Clamp to max 8 lines
    lines.min(8) as u16
}

pub fn build_layout(area: Rect, show_sidebar: bool, composer_h: u16) -> MainLayout {
    let (main, sidebar) = if show_sidebar && area.width > 120 {
        let chunks = Layout::default()
            .direction(Direction::Horizontal)
            .constraints([Constraint::Min(60), Constraint::Length(36)])
            .split(area);
        (chunks[0], Some(chunks[1]))
    } else {
        (area, None)
    };

    let rows = Layout::default()
        .direction(Direction::Vertical)
        .constraints([
            Constraint::Length(1),          // top bar
            Constraint::Min(1),            // messages
            Constraint::Length(1),          // thin separator line
            Constraint::Length(composer_h), // composer (borderless, just prompt)
            Constraint::Length(1),          // context bar (activity/status)
        ])
        .split(main);

    MainLayout {
        top_bar: rows[0],
        messages: rows[1],
        separator: rows[2],
        composer: rows[3],
        context_bar: rows[4],
        sidebar,
    }
}
