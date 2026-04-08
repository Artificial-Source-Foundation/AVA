//! Safe rendering primitives that make text bleed IMPOSSIBLE by design.
//!
//! Every widget that renders text should pass its lines through [`clamp_line`]
//! before the final `Paragraph::new()`.  Popup widgets should use
//! [`anchored_popup`] to guarantee viewport containment.

use crate::text_utils::{display_width, safe_char_width};
use ratatui::layout::Rect;
use ratatui::style::Style;
use ratatui::text::{Line, Span};
use ratatui::widgets::{Block, Clear, Paragraph};
use ratatui::Frame;

/// Convert a `Line` with borrowed content into a `Line<'static>` by cloning
/// all span content.
pub fn to_static_line(line: Line<'_>) -> Line<'static> {
    let spans: Vec<Span<'static>> = line
        .spans
        .into_iter()
        .map(|s| Span::styled(s.content.to_string(), s.style))
        .collect();
    let mut static_line = Line::from(spans);
    static_line.alignment = line.alignment;
    static_line
}

/// Convert a list of borrowed lines into owned lines while preserving alignment.
pub fn to_static_lines(lines: Vec<Line<'_>>) -> Vec<Line<'static>> {
    lines.into_iter().map(to_static_line).collect()
}

/// Clamp a `Line` to fit within `max_width` display columns.
///
/// Truncates spans from the right, appending `"..."` if any content was cut.
/// If the line already fits it is returned unchanged.
pub fn clamp_line(line: Line<'static>, max_width: usize) -> Line<'static> {
    let total: usize = line
        .spans
        .iter()
        .map(|s| display_width(s.content.as_ref()))
        .sum();
    if total <= max_width {
        return line;
    }

    let mut clamped = Vec::new();
    let mut used = 0;
    let budget = max_width.saturating_sub(3); // reserve room for "..."

    for span in line.spans {
        let w = display_width(span.content.as_ref());
        if used + w <= budget {
            clamped.push(span);
            used += w;
        } else {
            // Truncate this span to fill the remaining budget
            let remaining = budget - used;
            if remaining > 0 {
                let truncated = truncate_str(&span.content, remaining);
                clamped.push(Span::styled(truncated, span.style));
            }
            clamped.push(Span::raw("..."));
            break;
        }
    }

    let mut result = Line::from(clamped);
    result.alignment = line.alignment;
    result
}

/// Hard-clamp a line to fit within `max_width` display columns with no ellipsis.
///
/// This is useful in scroll regions where preserving strict geometry matters more
/// than indicating truncation.
pub fn hard_clamp_line(line: Line<'static>, max_width: usize) -> Line<'static> {
    let total: usize = line
        .spans
        .iter()
        .map(|s| display_width(s.content.as_ref()))
        .sum();
    if total <= max_width {
        return line;
    }

    let alignment = line.alignment;
    let mut remaining = max_width;
    let mut clamped_spans: Vec<Span<'static>> = Vec::with_capacity(line.spans.len());

    for span in line.spans {
        if remaining == 0 {
            break;
        }
        let span_w = display_width(span.content.as_ref());
        if span_w <= remaining {
            remaining -= span_w;
            clamped_spans.push(span);
        } else {
            let truncated = truncate_str(&span.content, remaining);
            if !truncated.is_empty() {
                clamped_spans.push(Span::styled(truncated, span.style));
            }
            break;
        }
    }

    let mut result = Line::from(clamped_spans);
    result.alignment = alignment;
    result
}

/// Truncate a string to fit within `max_width` display columns.
pub fn truncate_str(s: &str, max_width: usize) -> String {
    let mut result = String::new();
    let mut width = 0;
    for ch in s.chars() {
        let w = safe_char_width(ch);
        if width + w > max_width {
            break;
        }
        result.push(ch);
        width += w;
    }
    result
}

/// Render a [`Paragraph`] that is GUARANTEED to fit within the area.
///
/// Clears the area first, fills with `bg`, clamps every line to the inner
/// width, and caps the number of lines to the area height.  No wrapping.
pub fn safe_paragraph(frame: &mut Frame<'_>, area: Rect, lines: Vec<Line<'static>>, bg: Style) {
    frame.render_widget(Clear, area);
    frame.render_widget(Block::default().style(bg), area);

    let width = area.width as usize;
    let clamped: Vec<Line<'static>> = lines
        .into_iter()
        .take(area.height as usize)
        .map(|line| clamp_line(line, width))
        .collect();

    let paragraph = Paragraph::new(clamped);
    frame.render_widget(paragraph, area);
}

/// Build a key-value row that fits within `width`.
///
/// The left label gets priority; the right value truncates first.
pub fn fit_key_value(
    left: &str,
    right: &str,
    width: usize,
    left_style: Style,
    right_style: Style,
) -> Line<'static> {
    let left_w = display_width(left);
    let gap = 2; // "  " between key and value
    let available_right = width.saturating_sub(left_w + gap);
    let right_truncated = if display_width(right) > available_right {
        format!(
            "{}...",
            &truncate_str(right, available_right.saturating_sub(3))
        )
    } else {
        right.to_string()
    };
    let padding = width.saturating_sub(left_w + display_width(right_truncated.as_str()));

    Line::from(vec![
        Span::styled(left.to_string(), left_style),
        Span::raw(" ".repeat(padding)),
        Span::styled(right_truncated, right_style),
    ])
}

/// Calculate a safe popup `Rect` anchored above or below a reference point.
///
/// The returned rectangle is guaranteed to stay within the viewport.
pub fn anchored_popup(
    viewport: Rect,
    anchor_x: u16,
    anchor_y: u16,
    max_width: u16,
    max_height: u16,
) -> Rect {
    let width = max_width.min(viewport.width.saturating_sub(1));
    let height = max_height.min(viewport.height.saturating_sub(2));
    let x = anchor_x.min(viewport.right().saturating_sub(width));
    let y = if anchor_y > height {
        anchor_y - height - 1 // above anchor
    } else {
        (anchor_y + 1).min(viewport.bottom().saturating_sub(height)) // below anchor
    };
    Rect::new(x.max(viewport.x), y.max(viewport.y), width, height)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn clamp_line_fits() {
        let line = Line::from(vec![Span::raw("hello")]);
        let result = clamp_line(line, 10);
        assert_eq!(result.spans.len(), 1);
        assert_eq!(result.spans[0].content.as_ref(), "hello");
    }

    #[test]
    fn clamp_line_truncates() {
        let line = Line::from(vec![Span::raw("hello world, this is long")]);
        let result = clamp_line(line, 10);
        // Should end with "..."
        let last = result.spans.last().unwrap();
        assert_eq!(last.content.as_ref(), "...");
        // Total width should be <= 10
        let total: usize = result
            .spans
            .iter()
            .map(|s| display_width(s.content.as_ref()))
            .sum();
        assert!(total <= 10);
    }

    #[test]
    fn clamp_line_multi_span() {
        let line = Line::from(vec![
            Span::raw("hello "),
            Span::raw("world "),
            Span::raw("extra"),
        ]);
        let result = clamp_line(line, 12);
        let total: usize = result
            .spans
            .iter()
            .map(|s| display_width(s.content.as_ref()))
            .sum();
        assert!(total <= 12);
    }

    #[test]
    fn clamp_line_cjk() {
        // Each CJK char is 2 columns
        let line = Line::from(vec![Span::raw("日本語テスト")]);
        let result = clamp_line(line, 8);
        let total: usize = result
            .spans
            .iter()
            .map(|s| display_width(s.content.as_ref()))
            .sum();
        assert!(total <= 8);
    }

    #[test]
    fn truncate_str_basic() {
        assert_eq!(truncate_str("hello world", 5), "hello");
        assert_eq!(truncate_str("hi", 10), "hi");
    }

    #[test]
    fn truncate_str_cjk() {
        // "日" = 2 cols, "本" = 2 cols
        assert_eq!(truncate_str("日本語", 4), "日本");
        assert_eq!(truncate_str("日本語", 3), "日"); // can't fit half a char
    }

    #[test]
    fn anchored_popup_above() {
        let viewport = Rect::new(0, 0, 80, 24);
        let result = anchored_popup(viewport, 5, 20, 30, 10);
        assert!(result.right() <= viewport.right());
        assert!(result.bottom() <= viewport.bottom());
        assert_eq!(result.y, 9); // above anchor: 20 - 10 - 1 = 9
    }

    #[test]
    fn anchored_popup_below_when_near_top() {
        let viewport = Rect::new(0, 0, 80, 24);
        let result = anchored_popup(viewport, 5, 3, 30, 10);
        assert!(result.right() <= viewport.right());
        assert!(result.bottom() <= viewport.bottom());
        assert!(result.y > 3); // should go below
    }

    #[test]
    fn anchored_popup_clamps_to_viewport() {
        let viewport = Rect::new(0, 0, 40, 12);
        let result = anchored_popup(viewport, 30, 5, 30, 10);
        assert!(result.right() <= viewport.right());
        assert!(result.bottom() <= viewport.bottom());
    }

    #[test]
    fn fit_key_value_fits() {
        let line = fit_key_value("Key", "Value", 20, Style::default(), Style::default());
        let total: usize = line
            .spans
            .iter()
            .map(|s| display_width(s.content.as_ref()))
            .sum();
        assert_eq!(total, 20); // padded to fill width
    }

    #[test]
    fn fit_key_value_truncates_right() {
        let line = fit_key_value(
            "Key",
            "A very long value that should be truncated",
            15,
            Style::default(),
            Style::default(),
        );
        let total: usize = line
            .spans
            .iter()
            .map(|s| display_width(s.content.as_ref()))
            .sum();
        assert!(total <= 15);
    }

    #[test]
    fn clamp_line_ambiguous_chars() {
        let line = Line::from(vec![Span::raw("✓✓✓")]);
        let result = clamp_line(line, 4);
        let total: usize = result
            .spans
            .iter()
            .map(|s| display_width(s.content.as_ref()))
            .sum();
        assert!(total <= 4);
    }

    #[test]
    fn hard_clamp_line_preserves_alignment() {
        let mut line = Line::from(vec![Span::raw("hello world")]);
        line.alignment = Some(ratatui::layout::Alignment::Center);

        let result = hard_clamp_line(to_static_line(line), 5);

        assert_eq!(result.alignment, Some(ratatui::layout::Alignment::Center));
        let total: usize = result
            .spans
            .iter()
            .map(|s| display_width(s.content.as_ref()))
            .sum();
        assert!(total <= 5);
    }
}
