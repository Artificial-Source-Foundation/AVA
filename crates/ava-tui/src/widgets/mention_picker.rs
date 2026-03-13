use crate::state::theme::Theme;
use crate::text_utils::display_width;
use crate::widgets::autocomplete::AutocompleteState;
use ratatui::layout::Rect;
use ratatui::style::{Modifier, Style};
use ratatui::text::{Line, Span};
use ratatui::widgets::{Block, Borders, Clear, Paragraph};
use ratatui::Frame;

/// Maximum number of visible items in the mention picker.
const MAX_VISIBLE: usize = 10;

/// Render the @-mention autocomplete picker above the composer area.
///
/// Similar to the slash menu but styled for file/folder/codebase mentions.
pub fn render_mention_picker(
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
    let menu_height = (visible_count as u16) + 2; // +2 for borders

    let menu_y = composer_rect.y.saturating_sub(menu_height);
    let menu_width = composer_rect.width.min(70);

    let menu_rect = Rect::new(composer_rect.x + 1, menu_y, menu_width, menu_height);

    frame.render_widget(Clear, menu_rect);

    let block = Block::default()
        .borders(Borders::ALL)
        .border_style(Style::default().fg(theme.border))
        .style(Style::default().bg(theme.bg_elevated));

    let inner = block.inner(menu_rect);
    frame.render_widget(block, menu_rect);

    // Scroll offset
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
        let is_folder = item.value.ends_with('/');

        // Icon for file vs folder
        let icon = if is_folder {
            "\u{1F4C1} "
        } else {
            "\u{1F4C4} "
        };
        let icon_style = Style::default().fg(fg).bg(bg);

        let mut spans: Vec<Span<'_>> = Vec::new();

        spans.push(Span::styled(icon.to_string(), icon_style));
        spans.push(Span::styled(
            item.value.clone(),
            Style::default().fg(fg).bg(bg).add_modifier(Modifier::BOLD),
        ));

        // Right-align the detail using display width.
        let left_len = display_width(icon) + display_width(&item.value);
        let detail = &item.detail;
        let detail_len = display_width(detail);
        if !detail.is_empty() && inner_width > left_len + detail_len + 2 {
            let padding = inner_width - left_len - detail_len;
            spans.push(Span::styled(" ".repeat(padding), Style::default().bg(bg)));
            spans.push(Span::styled(
                detail.clone(),
                Style::default().fg(detail_fg).bg(bg),
            ));
        } else {
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

    let widget = Paragraph::new(lines);
    frame.render_widget(widget, inner);
}
