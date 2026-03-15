use crate::state::theme::Theme;
use crate::text_utils::display_width;
use crate::widgets::autocomplete::AutocompleteState;
use ratatui::layout::Rect;
use ratatui::style::{Modifier, Style};
use ratatui::text::{Line, Span};
use ratatui::widgets::{Block, Borders, Clear, Paragraph};
use ratatui::Frame;

/// Maximum number of visible items in the slash menu.
const MAX_VISIBLE: usize = 10;

/// Render the inline slash command autocomplete menu above the composer area.
///
/// This is NOT a modal — no dimming, no overlay. It floats above the composer
/// anchored to the composer's top edge and overlaps the message area.
pub fn render_slash_menu(
    frame: &mut Frame<'_>,
    composer_rect: Rect,
    state: &AutocompleteState,
    theme: &Theme,
) {
    let item_count = state.items.len();
    if item_count == 0 {
        return;
    }

    let visible_count = item_count.min(MAX_VISIBLE);
    // Each item is 1 row, plus 2 for border (top + bottom)
    let menu_height = (visible_count as u16) + 2;

    // Position the menu above the composer
    let menu_y = composer_rect.y.saturating_sub(menu_height);
    let menu_width = composer_rect.width.min(60);

    let menu_rect = Rect::new(composer_rect.x + 1, menu_y, menu_width, menu_height);

    // Clear the area behind the menu
    frame.render_widget(Clear, menu_rect);

    let block = Block::default()
        .borders(Borders::ALL)
        .border_style(Style::default().fg(theme.border))
        .style(Style::default().bg(theme.bg_elevated));

    let inner = block.inner(menu_rect);
    frame.render_widget(block, menu_rect);

    // Compute scroll offset to keep selected item visible
    let scroll_offset = if state.selected >= MAX_VISIBLE {
        state.selected - MAX_VISIBLE + 1
    } else {
        0
    };

    let mut lines: Vec<Line<'_>> = Vec::with_capacity(visible_count);

    for (idx, item) in state
        .items
        .iter()
        .enumerate()
        .skip(scroll_offset)
        .take(MAX_VISIBLE)
    {
        let is_selected = idx == state.selected;

        let bg = if is_selected {
            theme.primary
        } else {
            theme.bg_elevated
        };
        let fg = if is_selected { theme.bg } else { theme.text };
        let detail_fg = if is_selected {
            theme.bg
        } else {
            theme.text_muted
        };

        let inner_width = inner.width as usize;
        let prefix = format!("/{}", item.value);
        let detail = &item.detail;

        let mut spans: Vec<Span<'_>> = Vec::new();

        // Slash prefix styled
        spans.push(Span::styled(
            "/",
            Style::default()
                .fg(if is_selected { fg } else { theme.accent })
                .bg(bg),
        ));
        spans.push(Span::styled(
            item.value.clone(),
            Style::default().fg(fg).bg(bg).add_modifier(Modifier::BOLD),
        ));

        // Right-align the description
        let left_len = display_width(&prefix);
        let detail_len = display_width(detail);
        if !detail.is_empty() && inner_width > left_len + detail_len + 2 {
            let padding = inner_width - left_len - detail_len;
            spans.push(Span::styled(" ".repeat(padding), Style::default().bg(bg)));
            spans.push(Span::styled(
                detail.clone(),
                Style::default().fg(detail_fg).bg(bg),
            ));
        } else {
            // Fill remaining space with background
            let current_len = left_len;
            if inner_width > current_len {
                spans.push(Span::styled(
                    " ".repeat(inner_width - current_len),
                    Style::default().bg(bg),
                ));
            }
        }

        lines.push(Line::from(spans));
    }

    let widget = Paragraph::new(lines).style(Style::default().bg(theme.bg_elevated));
    frame.render_widget(widget, inner);
}
