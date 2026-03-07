use ratatui::layout::{Constraint, Direction, Layout, Rect};

pub struct MainLayout {
    pub top_bar: Rect,
    pub messages: Rect,
    pub composer: Rect,
    pub bottom_bar: Rect,
    pub sidebar: Option<Rect>,
}

pub fn build_layout(area: Rect, show_sidebar: bool) -> MainLayout {
    let (main, sidebar) = if show_sidebar && area.width > 120 {
        let chunks = Layout::default()
            .direction(Direction::Horizontal)
            .constraints([Constraint::Min(60), Constraint::Length(42)])
            .split(area);
        (chunks[0], Some(chunks[1]))
    } else {
        (area, None)
    };

    let rows = Layout::default()
        .direction(Direction::Vertical)
        .constraints([
            Constraint::Length(1),
            Constraint::Min(1),
            Constraint::Length(3),
            Constraint::Length(1),
        ])
        .split(main);

    MainLayout {
        top_bar: rows[0],
        messages: rows[1],
        composer: rows[2],
        bottom_bar: rows[3],
        sidebar,
    }
}
