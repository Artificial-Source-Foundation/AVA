use ratatui::layout::{Constraint, Direction, Layout, Rect};
use ratatui::text::Span;
use unicode_width::UnicodeWidthChar;

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

pub const SIDEBAR_AUTO_SHOW_WIDTH: u16 = 150;
pub const SIDEBAR_MANUAL_SHOW_WIDTH: u16 = 116;

/// Calculate how many lines the composer needs based on buffer content.
///
/// Content: prompt line + metadata line = 2 lines minimum.
/// Keep only a small amount of vertical breathing room so the composer hugs the bottom.
/// Multi-line input adds more prompt lines (capped at 8).
/// Capped at 33% of terminal height.
pub fn composer_height(
    buffer: &str,
    available_width: u16,
    terminal_height: u16,
    attachment_count: usize,
    pending_image_count: usize,
    queued_item_count: usize,
) -> u16 {
    // Match the actual width passed to `wrap_line_spans` in the composer.
    // The prompt/padding spans are counted inside that width by the renderer,
    // so the estimator must use the same `area.width - 4` budget.
    let inner_width = available_width.saturating_sub(4).max(1) as usize;

    let prompt_lines = estimate_prompt_rows(buffer, inner_width);

    let prompt_h = prompt_lines.min(8) as u16;
    let extra_rows = attachment_count
        .saturating_add(pending_image_count)
        .saturating_add(queued_item_count)
        .min(8) as u16;
    // prompt lines + attachment/queue rows + spacer + metadata line, with a small top inset.
    let content = prompt_h + extra_rows + 2;
    let total = content + 1;
    let max_height = (terminal_height / 3).max(3);
    total.min(max_height)
}

fn estimate_prompt_rows(buffer: &str, max_width: usize) -> usize {
    if buffer.is_empty() {
        return 1;
    }

    let input_lines: Vec<&str> = buffer.split('\n').collect();
    let last_line_index = input_lines.len().saturating_sub(1);

    input_lines
        .iter()
        .enumerate()
        .map(|(i, line)| {
            let prompt = if i == 0 { "❯ " } else { "  " };
            let mut spans = vec![
                Span::raw(prompt.to_string()),
                Span::raw((*line).to_string()),
            ];

            if i == last_line_index {
                // Match the rendered end-of-line block cursor.
                spans.push(Span::raw("█".to_string()));
            }

            estimate_actual_wrapped_rows(spans, max_width).max(1)
        })
        .sum::<usize>()
        .max(1)
}

fn estimate_actual_wrapped_rows(spans: Vec<Span<'_>>, max_width: usize) -> usize {
    let total_width: usize = spans.iter().map(actual_span_width).sum();
    if total_width <= max_width {
        return 1;
    }

    let mut rows = 1usize;
    let mut current_width = 0usize;
    for span in spans {
        for ch in span.content.chars() {
            let ch_width = actual_char_width(ch);
            if current_width + ch_width > max_width && current_width > 0 {
                rows += 1;
                current_width = 0;
            }
            current_width += ch_width;
        }
    }
    rows.max(1)
}

fn actual_char_width(ch: char) -> usize {
    UnicodeWidthChar::width(ch).unwrap_or(0).max(1)
}

fn actual_span_width(span: &Span<'_>) -> usize {
    span.content.chars().map(actual_char_width).sum()
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

pub fn sidebar_visible(area_width: u16, show_sidebar: bool) -> bool {
    area_width >= SIDEBAR_MANUAL_SHOW_WIDTH && show_sidebar
}

pub fn build_layout(
    area: Rect,
    show_sidebar: bool,
    composer_h: u16,
    show_context_bar: bool,
) -> MainLayout {
    // Sidebar split (only Layout usage — horizontal is reliable)
    let (main, sidebar) = if sidebar_visible(area.width, show_sidebar) {
        let chunks = Layout::default()
            .direction(Direction::Horizontal)
            .constraints([Constraint::Min(72), Constraint::Length(38)])
            .split(area);
        (chunks[0], Some(chunks[1]))
    } else {
        (area, None)
    };

    // Manual vertical layout — pinned to edges, no solver ambiguity.
    // The shell runs without a persistent header bar now.
    let top_bar_h: u16 = 0;

    // Top bar: disabled, kept as a zero-height rect for layout consistency.
    let top_bar = Rect {
        x: main.x,
        y: main.y,
        width: main.width,
        height: top_bar_h,
    };

    let context_h: u16 = if show_context_bar { 1 } else { 0 };

    // Context bar: optional bottom row
    let context_bar = Rect {
        x: main.x,
        y: main.y + main.height.saturating_sub(context_h),
        width: main.width,
        height: context_h,
    };

    // Leave a small breathing gap between messages and composer.
    let composer_gap: u16 = 1;
    let clamped_composer_h = composer_h.min(main.height.saturating_sub(top_bar_h + context_h + 1));
    let composer = Rect {
        x: main.x,
        y: context_bar.y.saturating_sub(clamped_composer_h),
        width: main.width,
        height: clamped_composer_h,
    };

    // Messages: fills between top bar and composer
    let messages_y = main.y + top_bar_h;
    let messages_h = composer.y.saturating_sub(messages_y + composer_gap);
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

    // Safety invariant: sections must tile vertically with no gaps or overlaps.
    // top_bar.bottom() == messages_full.y
    // messages_full.bottom() + composer_gap == composer.y
    // composer.bottom() == context_bar.y
    // context_bar.bottom() == main.y + main.height
    debug_assert_eq!(
        top_bar.y + top_bar.height,
        messages_full.y,
        "gap/overlap between top_bar and messages: top_bar.bottom()={} messages.y={}",
        top_bar.y + top_bar.height,
        messages_full.y,
    );
    debug_assert_eq!(
        messages_full.y + messages_full.height + composer_gap,
        composer.y,
        "gap/overlap between messages and composer: messages.bottom()+gap={} composer.y={}",
        messages_full.y + messages_full.height + composer_gap,
        composer.y,
    );
    debug_assert_eq!(
        composer.y + composer.height,
        context_bar.y,
        "gap/overlap between composer and context_bar: composer.bottom()={} context_bar.y={}",
        composer.y + composer.height,
        context_bar.y,
    );
    debug_assert_eq!(
        context_bar.y + context_bar.height,
        main.y + main.height,
        "gap/overlap between context_bar and bottom: context_bar.bottom()={} main.bottom()={}",
        context_bar.y + context_bar.height,
        main.y + main.height,
    );

    MainLayout {
        top_bar,
        messages,
        messages_full,
        composer,
        context_bar,
        sidebar,
    }
}

#[cfg(test)]
mod tests {
    use super::{
        composer_height, sidebar_visible, SIDEBAR_AUTO_SHOW_WIDTH, SIDEBAR_MANUAL_SHOW_WIDTH,
    };

    #[test]
    fn sidebar_respects_toggle_on_wide_terminals() {
        assert!(!sidebar_visible(SIDEBAR_AUTO_SHOW_WIDTH, false));
        assert!(sidebar_visible(SIDEBAR_AUTO_SHOW_WIDTH + 10, true));
    }

    #[test]
    fn sidebar_respects_manual_toggle_in_middle_range() {
        assert!(!sidebar_visible(SIDEBAR_MANUAL_SHOW_WIDTH, false));
        assert!(sidebar_visible(SIDEBAR_MANUAL_SHOW_WIDTH, true));
    }

    #[test]
    fn sidebar_hides_on_small_terminals() {
        assert!(!sidebar_visible(SIDEBAR_MANUAL_SHOW_WIDTH - 1, false));
        assert!(!sidebar_visible(SIDEBAR_MANUAL_SHOW_WIDTH - 1, true));
    }

    #[test]
    fn composer_height_grows_for_wrapped_input() {
        let short = composer_height("hello", 80, 40, 0, 0, 0);
        let wrapped = composer_height(
            "this is a deliberately long line that should wrap inside the composer input area once it gets wide enough",
            80,
            40,
            0,
            0,
            0,
        );

        assert!(wrapped > short);
    }

    #[test]
    fn composer_height_accounts_for_attachment_and_queue_rows() {
        let base = composer_height("hello", 80, 40, 0, 0, 0);
        let expanded = composer_height("hello", 80, 40, 2, 1, 3);

        assert!(expanded > base);
    }

    #[test]
    fn composer_height_is_capped() {
        let huge = composer_height(&"word ".repeat(500), 80, 24, 20, 20, 20);

        assert!(huge <= 24 / 3);
    }
}
