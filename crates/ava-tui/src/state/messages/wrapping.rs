use crate::text_utils::{display_width, safe_char_width};
use ratatui::style::Style;
use ratatui::text::Span;

use super::UiMessage;

impl UiMessage {
    /// Break a sequence of spans into multiple rows, each fitting within
    /// `max_width` display columns. Splits on word boundaries when possible,
    /// falling back to character-level splits for long words.
    pub(crate) fn wrap_line_spans(
        spans: Vec<Span<'static>>,
        max_width: usize,
    ) -> Vec<Vec<Span<'static>>> {
        if max_width == 0 {
            return vec![spans];
        }

        // Flatten spans into styled segments we can split.
        struct Segment {
            text: String,
            style: Style,
        }
        let segments: Vec<Segment> = spans
            .into_iter()
            .map(|s| Segment {
                text: s.content.into_owned(),
                style: s.style,
            })
            .collect();

        // Check total width — fast path if no wrapping needed.
        let total_width: usize = segments.iter().map(|s| display_width(&s.text)).sum();
        if total_width <= max_width {
            return vec![segments
                .into_iter()
                .map(|s| Span::styled(s.text, s.style))
                .collect()];
        }

        // Wrap by walking through characters.
        let mut rows: Vec<Vec<Span<'static>>> = Vec::new();
        let mut current_row: Vec<Span<'static>> = Vec::new();
        let mut current_width: usize = 0;

        for seg in segments {
            let style = seg.style;
            let text = seg.text;

            if text.is_empty() {
                current_row.push(Span::styled(String::new(), style));
                continue;
            }

            let mut remaining = text.as_str();
            while !remaining.is_empty() {
                let avail = max_width.saturating_sub(current_width);
                if avail == 0 {
                    rows.push(std::mem::take(&mut current_row));
                    current_width = 0;
                    continue;
                }

                let rem_width = display_width(remaining);
                if rem_width <= avail {
                    current_row.push(Span::styled(remaining.to_owned(), style));
                    current_width += rem_width;
                    break;
                }

                // Need to split — find a break point at `avail` columns.
                let break_at = Self::find_break_point(remaining, avail);
                if break_at == 0 {
                    // Can't fit even one char — flush row first.
                    if !current_row.is_empty() {
                        rows.push(std::mem::take(&mut current_row));
                        current_width = 0;
                    } else if remaining.is_empty() {
                        break;
                    } else {
                        // Force at least one char to avoid infinite loop.
                        let ch = remaining.chars().next().expect("remaining is non-empty");
                        let clen = ch.len_utf8();
                        current_row.push(Span::styled(remaining[..clen].to_owned(), style));
                        remaining = &remaining[clen..];
                        rows.push(std::mem::take(&mut current_row));
                        current_width = 0;
                    }
                    continue;
                }

                let chunk = &remaining[..break_at];
                current_row.push(Span::styled(chunk.to_owned(), style));
                remaining = &remaining[break_at..];

                // Flush this row.
                rows.push(std::mem::take(&mut current_row));
                current_width = 0;

                // Skip leading space on next line (word-wrap behavior).
                if remaining.starts_with(' ') {
                    remaining = &remaining[1..];
                }
            }
        }

        if !current_row.is_empty() {
            rows.push(current_row);
        }
        if rows.is_empty() {
            rows.push(Vec::new());
        }
        rows
    }

    /// Find the byte offset to break `text` at, targeting `max_cols` display
    /// columns. Prefers breaking at the last space or hyphen within the limit;
    /// falls back to exact column boundary only when no word boundary exists
    /// (e.g. long URLs or file paths without spaces).
    pub(crate) fn find_break_point(text: &str, max_cols: usize) -> usize {
        let mut col = 0usize;
        let mut last_break_byte = None;
        let mut byte_at_max = 0usize;

        for (i, ch) in text.char_indices() {
            let w = safe_char_width(ch);
            if col + w > max_cols {
                byte_at_max = i;
                break;
            }
            if ch == ' ' {
                // Break AT the space — space becomes the last char on this line,
                // and wrap_line_spans will skip the leading space on the next line.
                last_break_byte = Some(i);
            } else if ch == '-' {
                // Break AFTER the hyphen so it stays on the current line.
                last_break_byte = Some(i + ch.len_utf8());
            } else if ch == ',' {
                // Break AFTER comma so comma stays on current line.
                last_break_byte = Some(i + ch.len_utf8());
            }
            col += w;
            byte_at_max = i + ch.len_utf8();
        }

        // If we consumed the entire string without exceeding, return full length.
        if col <= max_cols {
            return byte_at_max;
        }

        // Prefer word boundary (space or hyphen).
        if let Some(bp) = last_break_byte {
            if bp > 0 {
                return bp;
            }
        }
        // No word boundary found — character-break as fallback (long URLs, paths).
        byte_at_max
    }
}
