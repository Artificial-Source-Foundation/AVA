use crossterm::event::{KeyCode, KeyEvent};
use nucleo::pattern::{CaseMatching, Normalization, Pattern};
use nucleo::Matcher;
use ratatui::layout::Rect;
use ratatui::style::{Color, Modifier, Style};
use ratatui::text::{Line, Span};
use ratatui::widgets::{Block, Borders, Paragraph};
use ratatui::Frame;

use crate::state::theme::Theme;
use crate::text_utils::display_width;

/// Status indicator for a select list item.
#[derive(Debug, Clone)]
pub enum ItemStatus {
    /// Green checkmark + text (e.g., "connected").
    Connected(String),
    /// Filled bullet indicator (e.g., current model).
    Active,
    /// Dimmed info text (e.g., "free").
    Info(String),
}

/// A single item in a select list.
#[derive(Debug, Clone)]
pub struct SelectItem<T: Clone> {
    /// Display text (left side).
    pub title: String,
    /// Secondary text (right-aligned, dimmed).
    pub detail: String,
    /// Category/section header this item belongs to.
    pub section: Option<String>,
    /// Status indicator (right side, colored).
    pub status: Option<ItemStatus>,
    /// User data payload.
    pub value: T,
    /// Whether this item is selectable.
    pub enabled: bool,
}

/// Reusable state for scrollable, searchable, categorized list modals.
#[derive(Debug, Clone, Default)]
pub struct SelectListState<T: Clone> {
    /// All items (unfiltered).
    pub items: Vec<SelectItem<T>>,
    /// Current search query.
    pub query: String,
    /// Selected item index (within filtered results).
    pub selected: usize,
    /// Scroll offset in rendered lines (NOT item index).
    pub scroll_offset: usize,
    /// Cached: rendered line position for each filtered item.
    line_map: Vec<usize>,
    /// Cached filtered indices into `items`.
    filtered_cache: Vec<usize>,
    /// Index of the item currently hovered by the mouse (if any).
    pub hovered: Option<usize>,
    /// Cached content area from last render (for mouse hit testing).
    pub content_area: Option<Rect>,
}

impl<T: Clone> SelectListState<T> {
    pub fn new(items: Vec<SelectItem<T>>) -> Self {
        let mut state = Self {
            items,
            query: String::new(),
            selected: 0,
            scroll_offset: 0,
            line_map: Vec::new(),
            filtered_cache: Vec::new(),
            hovered: None,
            content_area: None,
        };
        state.rebuild_cache();
        state
    }

    /// Get filtered items (references).
    pub fn filtered(&self) -> Vec<&SelectItem<T>> {
        self.filtered_cache
            .iter()
            .map(|&idx| &self.items[idx])
            .collect()
    }

    /// Get the selected item's value, if any.
    pub fn selected_value(&self) -> Option<&T> {
        self.filtered_cache
            .get(self.selected)
            .map(|&idx| &self.items[idx].value)
    }

    /// Get the selected item, if any.
    pub fn selected_item(&self) -> Option<&SelectItem<T>> {
        self.filtered_cache
            .get(self.selected)
            .map(|&idx| &self.items[idx])
    }

    /// Move selection down by N. Wraps at boundaries.
    pub fn move_down(&mut self, n: usize) {
        let count = self.filtered_cache.len();
        if count == 0 {
            return;
        }
        self.selected = (self.selected + n) % count;
    }

    /// Move selection up by N. Wraps at boundaries.
    pub fn move_up(&mut self, n: usize) {
        let count = self.filtered_cache.len();
        if count == 0 {
            return;
        }
        if n > self.selected {
            // Wrap to end
            self.selected = count - (n - self.selected) % count;
            if self.selected >= count {
                self.selected = count - 1;
            }
        } else {
            self.selected -= n;
        }
    }

    /// Jump to first item.
    pub fn move_to_start(&mut self) {
        self.selected = 0;
    }

    /// Jump to last item.
    pub fn move_to_end(&mut self) {
        let count = self.filtered_cache.len();
        if count > 0 {
            self.selected = count - 1;
        }
    }

    /// Ensure selected item is visible within the viewport.
    /// Uses line_map to compute the actual rendered line position
    /// (accounting for section headers and blank lines between groups).
    pub fn ensure_visible(&mut self, viewport_height: usize) {
        if self.line_map.is_empty() || self.selected >= self.line_map.len() {
            return;
        }
        let line = self.line_map[self.selected];

        // When scrolling up, also reveal the section header above this item.
        // If this is the first item in a section, the section header line sits
        // at (line - 1) and possibly a blank separator at (line - 2). We detect
        // "first in section" by checking whether the gap to the previous item
        // is larger than 1 (meaning header/separator lines exist between them).
        let effective_top = if self.selected == 0 {
            // Very first item — always scroll to the absolute top so the
            // first section header is visible.
            0
        } else {
            let prev_line = self.line_map[self.selected - 1];
            if line.saturating_sub(prev_line) > 1 {
                // First item in a new section — show the section header line.
                line.saturating_sub(1)
            } else {
                line
            }
        };

        if effective_top < self.scroll_offset {
            self.scroll_offset = effective_top;
        } else if line >= self.scroll_offset + viewport_height {
            self.scroll_offset = line.saturating_sub(viewport_height) + 1;
        }
    }

    /// Type a character into the search query.
    pub fn type_char(&mut self, ch: char) {
        self.query.push(ch);
        self.invalidate();
        self.selected = 0;
        self.scroll_offset = 0;
    }

    /// Delete last character from search query.
    pub fn backspace(&mut self) {
        self.query.pop();
        self.invalidate();
        self.selected = 0;
        self.scroll_offset = 0;
    }

    /// Reset search, selection, and scroll.
    pub fn reset(&mut self) {
        self.query.clear();
        self.selected = 0;
        self.scroll_offset = 0;
        self.invalidate();
    }

    /// Replace the items list and rebuild cache.
    pub fn set_items(&mut self, items: Vec<SelectItem<T>>) {
        self.items = items;
        self.selected = 0;
        self.scroll_offset = 0;
        self.invalidate();
    }

    /// Rebuild caches after items or query changed.
    pub fn invalidate(&mut self) {
        self.rebuild_cache();
    }

    /// Total number of rendered lines (for scrollbar).
    pub fn total_lines(&self) -> usize {
        if self.line_map.is_empty() {
            0
        } else {
            self.line_map.last().map_or(0, |&l| l + 1)
        }
    }

    fn rebuild_cache(&mut self) {
        self.filtered_cache = self.compute_filtered_indices();
        self.rebuild_line_map();
    }

    fn compute_filtered_indices(&self) -> Vec<usize> {
        if self.query.trim().is_empty() {
            return (0..self.items.len()).collect();
        }
        let mut matcher = Matcher::new(nucleo::Config::DEFAULT);
        let needle = Pattern::parse(&self.query, CaseMatching::Ignore, Normalization::Smart);
        let mut scored: Vec<_> = self
            .items
            .iter()
            .enumerate()
            .filter_map(|(idx, item)| {
                // Build search haystack from section + title + detail
                let search_str = format!(
                    "{} {} {}",
                    item.section.as_deref().unwrap_or(""),
                    item.title,
                    item.detail
                );
                let mut buf = Vec::new();
                let haystack = nucleo::Utf32Str::new(&search_str, &mut buf);
                needle
                    .score(haystack, &mut matcher)
                    .map(|score| (score, idx))
            })
            .collect();
        scored.sort_by(|a, b| b.0.cmp(&a.0));
        scored.into_iter().map(|(_, idx)| idx).collect()
    }

    /// Rebuild line_map from current filtered items + sections.
    /// Precomputes the actual rendered line for each item,
    /// accounting for section headers and blank separator lines.
    fn rebuild_line_map(&mut self) {
        let show_sections = self.query.is_empty();
        let mut line = 0usize;
        let mut last_section: Option<&str> = None;
        let mut map = Vec::with_capacity(self.filtered_cache.len());

        for &idx in &self.filtered_cache {
            let item = &self.items[idx];
            if show_sections {
                if let Some(ref section) = item.section {
                    if last_section != Some(section.as_str()) {
                        if last_section.is_some() {
                            line += 1; // Blank line between sections
                        }
                        line += 1; // Section header line
                        last_section = Some(section);
                    }
                }
            }
            map.push(line);
            line += 1; // The item itself
        }
        self.line_map = map;
    }
}

/// Result of handling a key event in the select list.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum SelectListAction {
    /// Selection moved (up/down/page/home/end).
    Moved,
    /// Enter pressed — item selected.
    Selected,
    /// Esc pressed — modal cancelled.
    Cancelled,
    /// Search query changed.
    Filtered,
    /// Key was not handled.
    Ignored,
}

/// Handle common list navigation keys. Returns the action taken.
pub fn handle_select_list_key<T: Clone>(
    state: &mut SelectListState<T>,
    key: KeyEvent,
    viewport_height: usize,
) -> SelectListAction {
    match key.code {
        KeyCode::Down => {
            state.move_down(1);
            state.ensure_visible(viewport_height);
            SelectListAction::Moved
        }
        KeyCode::Up => {
            state.move_up(1);
            state.ensure_visible(viewport_height);
            SelectListAction::Moved
        }
        KeyCode::PageDown => {
            state.move_down(10);
            state.ensure_visible(viewport_height);
            SelectListAction::Moved
        }
        KeyCode::PageUp => {
            state.move_up(10);
            state.ensure_visible(viewport_height);
            SelectListAction::Moved
        }
        KeyCode::Home => {
            state.move_to_start();
            state.ensure_visible(viewport_height);
            SelectListAction::Moved
        }
        KeyCode::End => {
            state.move_to_end();
            state.ensure_visible(viewport_height);
            SelectListAction::Moved
        }
        KeyCode::Enter => SelectListAction::Selected,
        KeyCode::Esc => SelectListAction::Cancelled,
        KeyCode::Char(ch) => {
            state.type_char(ch);
            SelectListAction::Filtered
        }
        KeyCode::Backspace => {
            state.backspace();
            SelectListAction::Filtered
        }
        _ => SelectListAction::Ignored,
    }
}

/// Result of handling a mouse event in the select list.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum SelectListMouseAction {
    /// Hover changed (visual update needed).
    Hovered,
    /// An item was clicked — caller should execute like Enter.
    Clicked,
    /// Scroll wheel moved selection.
    Scrolled,
    /// Mouse event was not handled.
    Ignored,
}

/// Handle mouse events for a select list: hover highlight, click to select, scroll wheel.
pub fn handle_select_list_mouse<T: Clone>(
    state: &mut SelectListState<T>,
    event: crossterm::event::MouseEvent,
    viewport_height: usize,
) -> SelectListMouseAction {
    use crossterm::event::MouseEventKind;

    let Some(area) = state.content_area else {
        return SelectListMouseAction::Ignored;
    };

    match event.kind {
        MouseEventKind::Moved | MouseEventKind::Drag(_) => {
            if event.column >= area.x
                && event.column < area.right()
                && event.row >= area.y
                && event.row < area.bottom()
            {
                let rendered_line = (event.row - area.y) as usize + state.scroll_offset;
                // Find which item index corresponds to this rendered line
                if let Some(idx) = state.line_map.iter().position(|&l| l == rendered_line) {
                    if state.hovered != Some(idx) {
                        state.hovered = Some(idx);
                        return SelectListMouseAction::Hovered;
                    }
                    return SelectListMouseAction::Ignored;
                }
            }
            if state.hovered.is_some() {
                state.hovered = None;
                return SelectListMouseAction::Hovered;
            }
            SelectListMouseAction::Ignored
        }
        MouseEventKind::Down(_) => {
            if let Some(hovered) = state.hovered {
                state.selected = hovered;
                return SelectListMouseAction::Clicked;
            }
            SelectListMouseAction::Ignored
        }
        MouseEventKind::ScrollUp => {
            state.move_up(3);
            state.ensure_visible(viewport_height);
            SelectListMouseAction::Scrolled
        }
        MouseEventKind::ScrollDown => {
            state.move_down(3);
            state.ensure_visible(viewport_height);
            SelectListMouseAction::Scrolled
        }
        _ => SelectListMouseAction::Ignored,
    }
}

/// A keybind hint displayed at the bottom of the modal.
pub struct KeybindHint {
    pub key: String,
    pub label: String,
}

/// Configuration for rendering a select list.
pub struct SelectListConfig {
    pub title: String,
    pub search_placeholder: String,
    pub keybinds: Vec<KeybindHint>,
}

/// Render a select list modal with title, search, categorized items, and sticky keybind footer.
pub fn render_select_list<T: Clone>(
    frame: &mut Frame<'_>,
    area: Rect,
    state: &mut SelectListState<T>,
    config: &SelectListConfig,
    theme: &Theme,
) {
    use ratatui::layout::{Constraint, Direction, Layout};

    // Layout: header (3 lines) | search (3 lines) | items (flex) | footer (2 lines)
    let header_height = 3u16;
    let search_height = 3u16;
    let footer_height = if config.keybinds.is_empty() { 0u16 } else { 2 };
    let chunks = Layout::default()
        .direction(Direction::Vertical)
        .constraints([
            Constraint::Length(header_height),
            Constraint::Length(search_height),
            Constraint::Min(1),
            Constraint::Length(footer_height),
        ])
        .split(area);
    let header_area = chunks[0];
    let search_area = chunks[1];
    let content_area = chunks[2];
    let footer_area = chunks[3];

    // Cache content area for mouse hit testing
    state.content_area = Some(content_area);

    let inner_width = content_area.width as usize;
    let filtered = state.filtered();
    let show_sections = state.query.is_empty();

    // --- Header bar: bg_surface background, title left, [Esc] right ---
    {
        // Fill header background
        let header_bg = Block::default().style(Style::default().bg(theme.bg_surface));
        frame.render_widget(header_bg, header_area);

        let title_text = vec![Span::styled(
            format!(" {}", config.title),
            Style::default()
                .fg(theme.text)
                .bg(theme.bg_surface)
                .add_modifier(Modifier::BOLD),
        )];
        let esc_text = vec![Span::styled(
            "[Esc] ",
            Style::default().fg(theme.text_dimmed).bg(theme.bg_surface),
        )];

        // Title on line 1 (vertically centered in 3-line header)
        let title_line = Rect::new(header_area.x, header_area.y + 1, header_area.width / 2, 1);
        frame.render_widget(
            Paragraph::new(Line::from(title_text)).style(Style::default().bg(theme.bg_surface)),
            title_line,
        );
        let esc_line = Rect::new(
            header_area.x + header_area.width / 2,
            header_area.y + 1,
            header_area.width / 2,
            1,
        );
        frame.render_widget(
            Paragraph::new(Line::from(esc_text))
                .alignment(ratatui::layout::Alignment::Right)
                .style(Style::default().bg(theme.bg_surface)),
            esc_line,
        );
    }

    // --- Search input: bg_deep fill with border_subtle ---
    {
        let search_inner = Rect::new(
            search_area.x + 1,
            search_area.y + 1,
            search_area.width.saturating_sub(2),
            1,
        );

        // Draw a bordered search box with bg_deep
        let search_block = Block::default()
            .borders(Borders::ALL)
            .border_style(Style::default().fg(theme.border_subtle))
            .style(Style::default().bg(theme.bg_deep));
        frame.render_widget(search_block, search_area);

        let search_display = if state.query.is_empty() {
            config.search_placeholder.clone()
        } else {
            state.query.clone()
        };
        let search_fg = if state.query.is_empty() {
            theme.text_dimmed
        } else {
            theme.text
        };
        let cursor = if state.query.is_empty() {
            ""
        } else {
            "\u{2588}"
        };

        let search_line = Line::from(vec![
            Span::styled(
                " \u{1F50D} ",
                Style::default().fg(theme.text_dimmed).bg(theme.bg_deep),
            ),
            Span::styled(
                search_display,
                Style::default().fg(search_fg).bg(theme.bg_deep),
            ),
            Span::styled(cursor, Style::default().fg(theme.primary).bg(theme.bg_deep)),
        ]);
        frame.render_widget(
            Paragraph::new(search_line).style(Style::default().bg(theme.bg_deep)),
            search_inner,
        );
    }

    // --- Items with section headers ---
    let mut lines: Vec<Line<'_>> = Vec::new();
    let mut last_section: Option<&str> = None;

    for (idx, item) in filtered.iter().enumerate() {
        // Section header
        if show_sections {
            if let Some(ref section) = item.section {
                if last_section != Some(section.as_str()) {
                    if last_section.is_some() {
                        lines.push(Line::from(""));
                    }
                    // Section header: uppercase, bold, text_dimmed (#505A6B)
                    let section_upper = section.to_uppercase();
                    lines.push(Line::from(vec![Span::styled(
                        format!("  {section_upper}"),
                        Style::default()
                            .fg(theme.text_dimmed)
                            .add_modifier(Modifier::BOLD),
                    )]));
                    last_section = Some(section);
                }
            }
        }

        let is_selected = idx == state.selected;
        let is_hovered = state.hovered == Some(idx) && !is_selected;

        // Selected: accent-primary bg with dark text; hovered: subtle surface bg; unselected: transparent
        let bg = if is_selected {
            theme.primary
        } else if is_hovered {
            theme.bg_surface
        } else {
            theme.bg_elevated
        };
        let fg = if is_selected {
            Color::Rgb(11, 14, 20) // dark text on accent bg
        } else {
            theme.text
        };
        // Detail text on selected row: ~0.7 opacity dark on primary blue
        let fg_detail = if is_selected {
            Color::Rgb(31, 57, 88) // #1F3958 — 0.7 opacity #0B0E14 on #4D9EF6
        } else {
            theme.text_dimmed
        };

        let mut spans: Vec<Span<'_>> = Vec::new();

        // Left padding + status prefix
        match &item.status {
            Some(ItemStatus::Active) => {
                spans.push(Span::styled(
                    " \u{25CF} ",
                    Style::default()
                        .fg(if is_selected { fg } else { theme.accent })
                        .bg(bg),
                ));
            }
            Some(ItemStatus::Connected(_)) => {
                spans.push(Span::styled(
                    " \u{2713} ",
                    Style::default()
                        .fg(if is_selected { fg } else { theme.success })
                        .bg(bg),
                ));
            }
            _ => {
                spans.push(Span::styled("   ", Style::default().bg(bg)));
            }
        }

        // Title: bold
        let title_style = if is_selected {
            Style::default().fg(fg).bg(bg).add_modifier(Modifier::BOLD)
        } else if !item.enabled {
            Style::default().fg(theme.text_dimmed).bg(bg)
        } else {
            Style::default().fg(fg).bg(bg).add_modifier(Modifier::BOLD)
        };
        spans.push(Span::styled(item.title.clone(), title_style));

        // Detail (right-aligned) or status text
        let right_text = match &item.status {
            Some(ItemStatus::Connected(text)) => text.clone(),
            Some(ItemStatus::Info(text)) => text.clone(),
            _ => item.detail.clone(),
        };

        if !right_text.is_empty() {
            let prefix_len = 3; // status prefix
            let title_len = display_width(&item.title);
            let left_len = prefix_len + title_len;
            let right_len = display_width(&right_text) + 1; // +1 for trailing space
            let padding = if inner_width > left_len + right_len + 1 {
                inner_width - left_len - right_len
            } else {
                2
            };

            spans.push(Span::styled(" ".repeat(padding), Style::default().bg(bg)));

            let detail_style = match &item.status {
                Some(ItemStatus::Connected(_)) => Style::default()
                    .fg(if is_selected { fg_detail } else { theme.accent })
                    .bg(bg),
                Some(ItemStatus::Info(_)) => Style::default()
                    .fg(if is_selected {
                        fg_detail
                    } else {
                        theme.text_muted
                    })
                    .bg(bg),
                _ => Style::default()
                    .fg(if is_selected {
                        fg_detail
                    } else {
                        theme.text_dimmed
                    })
                    .bg(bg),
            };
            spans.push(Span::styled(right_text, detail_style));
            spans.push(Span::styled(" ", Style::default().bg(bg)));
        }

        // Apply full-width background highlight for selected or hovered items
        if is_selected || is_hovered {
            let current_len: usize = spans.iter().map(|s| s.content.len()).sum();
            if inner_width > current_len {
                spans.push(Span::styled(
                    " ".repeat(inner_width - current_len),
                    Style::default().bg(bg),
                ));
            }
        }

        lines.push(Line::from(spans));
    }

    // Render scrollable content
    let scroll = state.scroll_offset as u16;
    let widget = Paragraph::new(lines)
        .scroll((scroll, 0))
        .style(Style::default().bg(theme.bg_elevated));
    frame.render_widget(widget, content_area);

    // --- Sticky footer: bg_surface background, keybind hints ---
    if !config.keybinds.is_empty() {
        let footer_bg = Block::default().style(Style::default().bg(theme.bg_surface));
        frame.render_widget(footer_bg, footer_area);

        let mut footer_spans: Vec<Span<'_>> =
            vec![Span::styled(" ", Style::default().bg(theme.bg_surface))];
        for (i, hint) in config.keybinds.iter().enumerate() {
            if i > 0 {
                footer_spans.push(Span::styled("    ", Style::default().bg(theme.bg_surface)));
            }
            footer_spans.push(Span::styled(
                hint.key.clone(),
                Style::default()
                    .fg(theme.text_muted)
                    .bg(theme.bg_surface)
                    .add_modifier(Modifier::BOLD),
            ));
            footer_spans.push(Span::styled(
                format!(" {}", hint.label),
                Style::default().fg(theme.text_dimmed).bg(theme.bg_surface),
            ));
        }
        let footer =
            Paragraph::new(Line::from(footer_spans)).style(Style::default().bg(theme.bg_surface));
        let footer_text_area = Rect::new(footer_area.x, footer_area.y + 1, footer_area.width, 1);
        frame.render_widget(footer, footer_text_area);
    }
}

/// Compute the viewport height for the item list area within a modal.
/// Subtracts header (3 lines), search (3 lines), and footer (2 lines) from the modal inner height.
pub fn list_viewport_height(modal_inner_height: usize) -> usize {
    modal_inner_height.saturating_sub(8)
}

#[cfg(test)]
mod tests {
    use super::*;

    fn make_items(sections: &[(&str, &[&str])]) -> Vec<SelectItem<String>> {
        let mut items = Vec::new();
        for (section, names) in sections {
            for name in *names {
                items.push(SelectItem {
                    title: name.to_string(),
                    detail: String::new(),
                    section: Some(section.to_string()),
                    status: None,
                    value: name.to_string(),
                    enabled: true,
                });
            }
        }
        items
    }

    fn make_flat_items(names: &[&str]) -> Vec<SelectItem<String>> {
        names
            .iter()
            .map(|name| SelectItem {
                title: name.to_string(),
                detail: String::new(),
                section: None,
                status: None,
                value: name.to_string(),
                enabled: true,
            })
            .collect()
    }

    #[test]
    fn move_down_wraps_at_boundary() {
        let mut state = SelectListState::new(make_flat_items(&["a", "b", "c"]));
        assert_eq!(state.selected, 0);
        state.move_down(1);
        assert_eq!(state.selected, 1);
        state.move_down(1);
        assert_eq!(state.selected, 2);
        state.move_down(1);
        assert_eq!(state.selected, 0); // wraps
    }

    #[test]
    fn move_up_wraps_at_boundary() {
        let mut state = SelectListState::new(make_flat_items(&["a", "b", "c"]));
        assert_eq!(state.selected, 0);
        state.move_up(1);
        assert_eq!(state.selected, 2); // wraps to end
        state.move_up(1);
        assert_eq!(state.selected, 1);
    }

    #[test]
    fn move_down_page_navigation() {
        let items: Vec<&str> = (0..25).map(|_| "item").collect();
        let mut state = SelectListState::new(make_flat_items(&items));
        state.move_down(10);
        assert_eq!(state.selected, 10);
        state.move_down(10);
        assert_eq!(state.selected, 20);
        state.move_down(10);
        assert_eq!(state.selected, 5); // wraps: (20+10) % 25 = 5
    }

    #[test]
    fn empty_list_handling() {
        let mut state: SelectListState<String> = SelectListState::new(Vec::new());
        state.move_down(1);
        assert_eq!(state.selected, 0);
        state.move_up(1);
        assert_eq!(state.selected, 0);
        assert!(state.selected_value().is_none());
    }

    #[test]
    fn type_char_and_backspace_filter() {
        let mut state = SelectListState::new(make_flat_items(&["apple", "banana", "avocado"]));
        assert_eq!(state.filtered().len(), 3);

        state.type_char('a');
        let filtered = state.filtered();
        // "a" should match "apple" and "avocado" (fuzzy on title)
        assert!(filtered.len() >= 2);
        assert_eq!(state.selected, 0);

        state.backspace();
        assert_eq!(state.filtered().len(), 3);
    }

    #[test]
    fn rebuild_line_map_accounts_for_sections() {
        // Two sections: "Fruit" with 2 items, "Veggie" with 1 item
        let state = SelectListState::new(make_items(&[
            ("Fruit", &["apple", "banana"]),
            ("Veggie", &["carrot"]),
        ]));

        // Expected layout:
        // line 0: "Fruit" header
        // line 1: apple
        // line 2: banana
        // line 3: blank line between sections
        // line 4: "Veggie" header
        // line 5: carrot
        assert_eq!(state.line_map, vec![1, 2, 5]);
    }

    #[test]
    fn ensure_visible_with_section_headers() {
        let mut state = SelectListState::new(make_items(&[
            ("Section A", &["a1", "a2", "a3"]),
            ("Section B", &["b1", "b2", "b3"]),
        ]));

        // Layout:
        // line 0: "Section A"
        // line 1: a1
        // line 2: a2
        // line 3: a3
        // line 4: blank
        // line 5: "Section B"
        // line 6: b1
        // line 7: b2
        // line 8: b3
        assert_eq!(state.line_map, vec![1, 2, 3, 6, 7, 8]);

        // Small viewport of 3 lines
        state.selected = 0;
        state.ensure_visible(3);
        assert_eq!(state.scroll_offset, 0);

        // Select b1 (index 3, line 6)
        state.selected = 3;
        state.ensure_visible(3);
        // line 6 should be visible: scroll_offset should be >= 4
        assert!(state.scroll_offset + 3 > 6);
        assert!(state.scroll_offset <= 6);
    }

    #[test]
    fn scroll_up_to_first_item_shows_section_header() {
        // Regression test: scrolling back up to the first item must
        // reset scroll_offset to 0 so the section header is visible.
        let mut state = SelectListState::new(make_items(&[
            ("Section A", &["a1", "a2", "a3", "a4", "a5"]),
            ("Section B", &["b1", "b2"]),
        ]));

        // line_map[0] == 1 (because line 0 is the "Section A" header)
        assert_eq!(state.line_map[0], 1);

        // Scroll down past the viewport, then back to the first item
        let viewport = 3;
        state.selected = 4; // a5
        state.ensure_visible(viewport);
        assert!(state.scroll_offset > 0);

        // Now scroll back up to the very first item
        state.selected = 0;
        state.ensure_visible(viewport);
        // scroll_offset must be 0, not 1 — the section header must be visible
        assert_eq!(state.scroll_offset, 0);
    }

    #[test]
    fn scroll_up_to_first_item_of_second_section_shows_header() {
        let mut state = SelectListState::new(make_items(&[
            ("Section A", &["a1", "a2"]),
            ("Section B", &["b1", "b2"]),
        ]));

        // Layout:
        // 0: Section A header
        // 1: a1, 2: a2
        // 3: blank
        // 4: Section B header
        // 5: b1, 6: b2
        assert_eq!(state.line_map, vec![1, 2, 5, 6]);

        let viewport = 3;
        state.selected = 3; // b2 at line 6
        state.ensure_visible(viewport);

        // Now go to b1 (first in Section B, at line 5)
        state.selected = 2; // b1
        state.ensure_visible(viewport);
        // Section B header is at line 4, so scroll_offset should show it
        assert!(
            state.scroll_offset <= 4,
            "scroll_offset {} should be <= 4 to show Section B header",
            state.scroll_offset
        );
    }

    #[test]
    fn line_map_no_sections_when_searching() {
        let mut state = SelectListState::new(make_items(&[
            ("Fruit", &["apple", "banana"]),
            ("Veggie", &["carrot"]),
        ]));

        // When searching, sections are hidden
        state.type_char('a'); // matches "apple" (and possibly others)
                              // Line map should have no section header offsets
        for (i, &line) in state.line_map.iter().enumerate() {
            assert_eq!(line, i, "Without sections, line should equal index");
        }
    }

    #[test]
    fn selected_value_returns_correct_item() {
        let state = SelectListState::new(make_flat_items(&["first", "second", "third"]));
        assert_eq!(state.selected_value(), Some(&"first".to_string()));
    }

    #[test]
    fn move_to_start_and_end() {
        let mut state = SelectListState::new(make_flat_items(&["a", "b", "c", "d"]));
        state.move_to_end();
        assert_eq!(state.selected, 3);
        state.move_to_start();
        assert_eq!(state.selected, 0);
    }

    #[test]
    fn handle_key_navigation() {
        let mut state = SelectListState::new(make_flat_items(&["a", "b", "c"]));

        let down = KeyEvent::from(KeyCode::Down);
        let action = handle_select_list_key(&mut state, down, 10);
        assert_eq!(action, SelectListAction::Moved);
        assert_eq!(state.selected, 1);

        let up = KeyEvent::from(KeyCode::Up);
        let action = handle_select_list_key(&mut state, up, 10);
        assert_eq!(action, SelectListAction::Moved);
        assert_eq!(state.selected, 0);

        let esc = KeyEvent::from(KeyCode::Esc);
        let action = handle_select_list_key(&mut state, esc, 10);
        assert_eq!(action, SelectListAction::Cancelled);

        let enter = KeyEvent::from(KeyCode::Enter);
        let action = handle_select_list_key(&mut state, enter, 10);
        assert_eq!(action, SelectListAction::Selected);

        let char_a = KeyEvent::from(KeyCode::Char('x'));
        let action = handle_select_list_key(&mut state, char_a, 10);
        assert_eq!(action, SelectListAction::Filtered);
        assert_eq!(state.query, "x");

        let bs = KeyEvent::from(KeyCode::Backspace);
        let action = handle_select_list_key(&mut state, bs, 10);
        assert_eq!(action, SelectListAction::Filtered);
        assert_eq!(state.query, "");
    }

    #[test]
    fn handle_key_page_and_home_end() {
        let items: Vec<&str> = (0..20).map(|_| "item").collect();
        let mut state = SelectListState::new(make_flat_items(&items));

        let pgdn = KeyEvent::from(KeyCode::PageDown);
        let action = handle_select_list_key(&mut state, pgdn, 10);
        assert_eq!(action, SelectListAction::Moved);
        assert_eq!(state.selected, 10);

        let pgup = KeyEvent::from(KeyCode::PageUp);
        let action = handle_select_list_key(&mut state, pgup, 10);
        assert_eq!(action, SelectListAction::Moved);
        assert_eq!(state.selected, 0);

        let end = KeyEvent::from(KeyCode::End);
        let action = handle_select_list_key(&mut state, end, 10);
        assert_eq!(action, SelectListAction::Moved);
        assert_eq!(state.selected, 19);

        let home = KeyEvent::from(KeyCode::Home);
        let action = handle_select_list_key(&mut state, home, 10);
        assert_eq!(action, SelectListAction::Moved);
        assert_eq!(state.selected, 0);
    }
}
