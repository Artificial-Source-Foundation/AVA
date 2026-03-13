use ratatui::layout::{Constraint, Direction, Layout, Rect};

use crate::text_utils::display_width;

pub struct MainLayout {
    pub top_bar: Rect,
    /// Messages area inset by content_margin on both sides.
    pub messages: Rect,
    /// Full-width row for the messages section (for background fills).
    pub messages_full: Rect,
    pub composer: Rect,
    pub context_bar: Rect,
    pub sidebar: Option<Rect>,
}

/// Calculate how many lines the composer needs based on buffer content.
///
/// Content: prompt line + model info line = 2 lines minimum.
/// Plus 1 line top/bottom padding for breathing room (design: padding=[12,16]).
/// Multi-line input adds more prompt lines (capped at 8).
/// Capped at 33% of terminal height.
pub fn composer_height(buffer: &str, available_width: u16, terminal_height: u16) -> u16 {
    // Inner width = total minus bar (1) + pad (2) + "❯ " (2)
    let inner_width = available_width.saturating_sub(5).max(1) as usize;

    let prompt_lines = if buffer.is_empty() {
        1
    } else {
        buffer
            .split('\n')
            .map(|line| {
                let len = display_width(line);
                if len == 0 {
                    1usize
                } else {
                    len.div_ceil(inner_width).max(1)
                }
            })
            .sum::<usize>()
            .max(1)
    };

    let prompt_h = prompt_lines.min(8) as u16;
    // prompt lines + model info line + 1 top pad + 1 bottom pad
    let content = prompt_h + 1;
    let total = content + 2;
    let max_height = (terminal_height / 3).max(4);
    total.min(max_height)
}

/// Horizontal margin for the messages area.
/// Design: padding=24px on 1440px frame ≈ 1.67%.
pub fn content_margin(area_width: u16) -> u16 {
    if area_width >= 120 {
        3
    } else if area_width >= 60 {
        2
    } else {
        1
    }
}

pub fn build_layout(area: Rect, show_sidebar: bool, composer_h: u16) -> MainLayout {
    // Sidebar split (only Layout usage — horizontal is reliable)
    let (main, sidebar) = if show_sidebar && area.width > 120 {
        let chunks = Layout::default()
            .direction(Direction::Horizontal)
            .constraints([Constraint::Min(60), Constraint::Length(36)])
            .split(area);
        (chunks[0], Some(chunks[1]))
    } else {
        (area, None)
    };

    // Manual vertical layout — pinned to edges, no solver ambiguity.
    // Design: Top Bar height=36px ≈ 2 rows, Context Bar height=28px ≈ 2 rows
    let bar_h: u16 = 2;

    // Top bar: rows 0..1
    let top_bar = Rect {
        x: main.x,
        y: main.y,
        width: main.width,
        height: bar_h,
    };

    // Context bar: last 2 rows
    let context_bar = Rect {
        x: main.x,
        y: main.y + main.height.saturating_sub(bar_h),
        width: main.width,
        height: bar_h,
    };

    // Composer: pinned above context bar
    let clamped_composer_h = composer_h.min(main.height.saturating_sub(bar_h * 2 + 1)); // leave room for top + context + 1 msg row
    let composer = Rect {
        x: main.x,
        y: context_bar.y.saturating_sub(clamped_composer_h),
        width: main.width,
        height: clamped_composer_h,
    };

    // Messages: fills between top bar and composer
    let messages_y = main.y + bar_h;
    let messages_h = composer.y.saturating_sub(messages_y);
    let messages_full = Rect {
        x: main.x,
        y: messages_y,
        width: main.width,
        height: messages_h,
    };

    let margin = content_margin(main.width);
    let messages = Rect {
        x: messages_full.x.saturating_add(margin),
        y: messages_full.y,
        width: messages_full.width.saturating_sub(margin * 2),
        height: messages_full.height,
    };

    MainLayout {
        top_bar,
        messages,
        messages_full,
        composer,
        context_bar,
        sidebar,
    }
}
