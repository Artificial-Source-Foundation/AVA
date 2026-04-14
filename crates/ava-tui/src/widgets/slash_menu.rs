use crate::state::theme::Theme;
use crate::text_utils::display_width;
use crate::widgets::autocomplete::AutocompleteState;
use crate::widgets::safe_render::{anchored_popup, clamp_line};
use ratatui::layout::Rect;
use ratatui::style::{Modifier, Style};
use ratatui::text::{Line, Span};
use ratatui::widgets::{Block, Borders, Clear, Paragraph};
use ratatui::Frame;

/// Maximum number of visible items in the slash menu.
const MAX_VISIBLE: usize = 10;
const MENU_WIDTH_MAX: u16 = 62;
const MENU_WIDTH_MIN: u16 = 32;
const MENU_SIDE_PAD: usize = 4;

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
    let menu_height = visible_count as u16 + 2; // +2 for border

    // Slightly content-fit + centered: keep menu compact and balanced over composer.
    let max_row = state
        .items
        .iter()
        .map(|item| {
            let left = display_width(&format!("/{value}", value = item.value));
            let right = if item.detail.is_empty() {
                0
            } else {
                2 + display_width(&item.detail)
            };
            left + right
        })
        .max()
        .unwrap_or(0);
    let composer_room = composer_rect.width.max(1);
    let content_max = composer_room.saturating_sub(4).max(1); // leave a little breathing room
    let desired_content_width = (max_row + MENU_SIDE_PAD).min(content_max as usize);
    let menu_cap = std::cmp::min(MENU_WIDTH_MAX as usize, content_max as usize);
    let menu_min = std::cmp::min(MENU_WIDTH_MIN as usize, menu_cap);
    let menu_content_width = desired_content_width.clamp(menu_min, menu_cap) as u16;
    let menu_width = menu_content_width.saturating_add(2); // account for border glyphs

    let composer_center = composer_rect.x.saturating_add(composer_rect.width / 2);
    let menu_x = composer_center.saturating_sub(menu_width / 2);

    // Use anchored_popup to guarantee the menu stays within the viewport
    let viewport = frame.area();
    let menu_rect = anchored_popup(viewport, menu_x, composer_rect.y, menu_width, menu_height);

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

    let inner_width = inner.width as usize;
    let mut lines: Vec<Line<'static>> = Vec::with_capacity(visible_count);

    for (idx, item) in state
        .items
        .iter()
        .enumerate()
        .skip(scroll_offset)
        .take(MAX_VISIBLE)
    {
        let is_selected = idx == state.selected;

        let bg = if is_selected {
            theme.bg_surface
        } else {
            theme.bg_elevated
        };
        let fg = theme.text;
        let detail_fg = if is_selected {
            theme.text_muted
        } else {
            theme.text_dimmed
        };

        let prefix = format!("/{}", item.value);
        let detail = &item.detail;

        let mut spans: Vec<Span<'static>> = Vec::new();

        // Slash prefix styled
        spans.push(Span::styled(
            "/",
            Style::default().fg(theme.text_muted).bg(bg),
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

        lines.push(clamp_line(Line::from(spans), inner_width));
    }

    let widget = Paragraph::new(lines).style(Style::default().bg(theme.bg_elevated));
    frame.render_widget(widget, inner);
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::widgets::autocomplete::{AutocompleteItem, AutocompleteTrigger};
    use ratatui::backend::TestBackend;
    use ratatui::Terminal;

    #[test]
    fn slash_menu_keeps_inner_width_match_when_bordered() {
        let theme = Theme::default_theme();
        let state = AutocompleteState::new(
            AutocompleteTrigger::Slash,
            String::new(),
            vec![
                AutocompleteItem::new("help", "List available commands"),
                AutocompleteItem::new("theme", "Switch theme"),
            ],
        );

        let viewport = Rect::new(0, 0, 80, 30);
        let composer = Rect::new(16, 18, 38, 2);

        let mut terminal =
            Terminal::new(TestBackend::new(viewport.width, viewport.height)).unwrap();
        terminal
            .draw(|frame| {
                frame.render_widget(Clear, viewport);
                render_slash_menu(frame, composer, &state, &theme);
            })
            .unwrap();

        let buffer = terminal.backend().buffer();
        let visible_count = state.items.len().min(MAX_VISIBLE);
        let menu_height = visible_count as u16 + 2;
        let menu_y = composer.y.saturating_sub(menu_height + 1);

        let mut menu_left = None;
        let mut menu_right = None;
        for x in viewport.x..viewport.width {
            let symbol = buffer[(x, menu_y)].symbol();
            if symbol != " " {
                if menu_left.is_none() {
                    menu_left = Some(x);
                }
                menu_right = Some(x);
            }
        }

        let menu_left = menu_left.expect("menu left border should be rendered");
        let menu_right = menu_right.expect("menu right border should be rendered");
        assert_ne!(menu_left, menu_right, "menu must have non-zero width");
        assert_eq!(buffer[(menu_left, menu_y)].symbol(), "┌");
        assert_eq!(buffer[(menu_right, menu_y)].symbol(), "┐");

        let menu_width = menu_right.saturating_sub(menu_left).saturating_add(1);
        let inner_width = menu_width.saturating_sub(2);

        let max_row = state
            .items
            .iter()
            .map(|item| {
                let left = display_width(&format!("/{value}", value = item.value));
                let right = if item.detail.is_empty() {
                    0
                } else {
                    2 + display_width(&item.detail)
                };
                left + right
            })
            .max()
            .unwrap_or(0);
        let composer_room = composer.width.max(1);
        let content_max = composer_room.saturating_sub(4).max(1);
        let desired_content_width = (max_row + MENU_SIDE_PAD).min(content_max as usize);
        let menu_cap = std::cmp::min(MENU_WIDTH_MAX as usize, content_max as usize);
        let menu_min = std::cmp::min(MENU_WIDTH_MIN as usize, menu_cap);
        let expected_inner_width = desired_content_width.clamp(menu_min, menu_cap) as u16;

        assert_eq!(
            inner_width, expected_inner_width,
            "inner width should keep intended content width"
        );

        let composer_center = composer.x.saturating_add(composer.width / 2);
        let expected_menu_x = composer_center.saturating_sub((expected_inner_width + 2) / 2);
        assert_eq!(menu_left, expected_menu_x);
        assert_eq!(menu_y, composer.y.saturating_sub(menu_height + 1));
    }

    #[test]
    fn slash_menu_renders_centered_without_viewport_overflow_near_top() {
        let theme = Theme::default_theme();
        let state = AutocompleteState::new(
            AutocompleteTrigger::Slash,
            String::new(),
            vec![AutocompleteItem::new(
                "very_long",
                "detailed right-aligned slash command",
            )],
        );

        let viewport = Rect::new(0, 0, 54, 16);
        let composer = Rect::new(34, 0, 20, 2);

        let mut terminal =
            Terminal::new(TestBackend::new(viewport.width, viewport.height)).unwrap();
        terminal
            .draw(|frame| {
                frame.render_widget(Clear, viewport);
                render_slash_menu(frame, composer, &state, &theme);
            })
            .unwrap();

        let buffer = terminal.backend().buffer();
        let visible_count = state.items.len().min(MAX_VISIBLE);
        let menu_height = visible_count as u16 + 2;

        let max_row = state
            .items
            .iter()
            .map(|item| {
                let left = display_width(&format!("/{value}", value = item.value));
                let right = if item.detail.is_empty() {
                    0
                } else {
                    2 + display_width(&item.detail)
                };
                left + right
            })
            .max()
            .unwrap_or(0);
        let composer_room = composer.width.max(1);
        let content_max = composer_room.saturating_sub(4).max(1);
        let desired_content_width = (max_row + MENU_SIDE_PAD).min(content_max as usize);
        let menu_cap = std::cmp::min(MENU_WIDTH_MAX as usize, content_max as usize);
        let menu_min = std::cmp::min(MENU_WIDTH_MIN as usize, menu_cap);
        let expected_inner_width = desired_content_width.clamp(menu_min, menu_cap) as u16;
        let expected_menu_width = expected_inner_width.saturating_add(2);
        let composer_center = composer.x.saturating_add(composer.width / 2);
        let expected_menu_x = composer_center.saturating_sub(expected_menu_width / 2);
        let expected_menu = anchored_popup(
            viewport,
            expected_menu_x,
            composer.y,
            expected_menu_width,
            menu_height,
        );

        // The popup should stay fully inside the viewport.
        assert!(expected_menu.left() >= viewport.left());
        assert!(expected_menu.right() <= viewport.right());
        assert!(expected_menu.top() >= viewport.top());
        assert!(expected_menu.bottom() <= viewport.bottom());

        // Border should still render completely.
        assert_eq!(
            buffer[(expected_menu.left(), expected_menu.top())].symbol(),
            "┌"
        );
        assert_eq!(
            buffer[(expected_menu.right().saturating_sub(1), expected_menu.top())].symbol(),
            "┐"
        );
        assert_eq!(
            buffer[(
                expected_menu.left(),
                expected_menu.bottom().saturating_sub(1)
            )]
                .symbol(),
            "└"
        );
        assert_eq!(
            buffer[(
                expected_menu.right().saturating_sub(1),
                expected_menu.bottom().saturating_sub(1)
            )]
                .symbol(),
            "┘"
        );

        // Content should render and be visible on the first visible row.
        assert_eq!(
            buffer[(expected_menu.left() + 1, expected_menu.top() + 1)].symbol(),
            "/"
        );
        assert_eq!(
            buffer[(expected_menu.left() + 1, expected_menu.top() + 1)]
                .style()
                .bg,
            Some(theme.bg_surface)
        );
    }
}
