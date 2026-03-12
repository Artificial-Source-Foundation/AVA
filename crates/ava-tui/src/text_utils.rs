//! Unicode-safe text truncation helpers for TUI display.
//!
//! Every function in this module operates on **display width** (columns)
//! rather than byte length, so multi-byte and wide characters are handled
//! correctly without risking panics from slicing inside a code-point.

use unicode_width::UnicodeWidthStr;

/// Truncate `s` to fit within `max_display_cols` terminal columns.
///
/// If the string already fits it is returned unchanged (cloned).
/// Otherwise the string is truncated at a character boundary such that
/// the result plus a trailing `"..."` ellipsis fits within the budget.
///
/// When `max_display_cols < 4` (i.e. no room for even one char + ellipsis)
/// the raw string is returned as-is — it is the caller's responsibility to
/// ensure the budget is large enough.
pub fn truncate_display(s: &str, max_display_cols: usize) -> String {
    let width = s.width();
    if width <= max_display_cols {
        return s.to_string();
    }
    if max_display_cols < 4 {
        return s.to_string();
    }

    let budget = max_display_cols.saturating_sub(3); // room for "..."
    let mut col = 0;
    let mut end = 0;
    for ch in s.chars() {
        let cw = unicode_width::UnicodeWidthChar::width(ch).unwrap_or(0);
        if col + cw > budget {
            break;
        }
        col += cw;
        end += ch.len_utf8();
    }
    format!("{}...", &s[..end])
}

/// Truncate `s` from the **start** so the visible tail fits within
/// `max_display_cols` terminal columns (with a leading `"..."` prefix).
///
/// Useful for file-path labels where the end is more informative.
pub fn truncate_display_start(s: &str, max_display_cols: usize) -> String {
    let width = s.width();
    if width <= max_display_cols {
        return s.to_string();
    }
    if max_display_cols < 4 {
        return s.to_string();
    }

    let budget = max_display_cols.saturating_sub(3); // room for "..."
                                                     // Walk backwards
    let mut col = 0;
    let mut start = s.len();
    for ch in s.chars().rev() {
        let cw = unicode_width::UnicodeWidthChar::width(ch).unwrap_or(0);
        if col + cw > budget {
            break;
        }
        col += cw;
        start -= ch.len_utf8();
    }
    format!("...{}", &s[start..])
}

/// Compute the display width (terminal columns) of a string.
///
/// This is a thin wrapper around `UnicodeWidthStr::width` exported for
/// convenience so callers don't need to import `unicode_width` themselves.
#[inline]
pub fn display_width(s: &str) -> usize {
    s.width()
}

/// Compute the display width of a `Span`'s content.
///
/// Identical to `display_width` but accepts the `Cow<str>` inside a Span.
#[inline]
pub fn span_display_width(span: &ratatui::text::Span<'_>) -> usize {
    span.content.width()
}

/// Truncate a string at a character boundary (byte-oriented limit).
///
/// Unlike [`truncate_display`] this uses an approximate **byte** limit
/// rather than display columns. Useful for very large content blobs
/// (e.g. file contents injected into context) where display width is
/// irrelevant but slicing inside a UTF-8 code-point would panic.
///
/// Returns `(truncated_str, was_truncated)`.
pub fn truncate_bytes_safe(s: &str, max_bytes: usize) -> (&str, bool) {
    if s.len() <= max_bytes {
        return (s, false);
    }
    // Walk backwards from max_bytes to find a char boundary
    let mut end = max_bytes;
    while end > 0 && !s.is_char_boundary(end) {
        end -= 1;
    }
    (&s[..end], true)
}

#[cfg(test)]
mod tests {
    use super::*;

    // ── truncate_display ────────────────────────────────────────────

    #[test]
    fn ascii_within_budget() {
        assert_eq!(truncate_display("hello", 10), "hello");
    }

    #[test]
    fn ascii_exact_budget() {
        assert_eq!(truncate_display("hello", 5), "hello");
    }

    #[test]
    fn ascii_truncated() {
        let result = truncate_display("hello world", 8);
        assert_eq!(result, "hello...");
        assert!(display_width(&result) <= 8);
    }

    #[test]
    fn multibyte_within_budget() {
        // "café" is 4 display columns but 5 bytes
        assert_eq!(truncate_display("café", 10), "café");
    }

    #[test]
    fn multibyte_exact_budget() {
        assert_eq!(truncate_display("café", 4), "café");
    }

    #[test]
    fn multibyte_truncated() {
        // "café latte" = 10 display cols, budget = 7 → 7 - 3 = 4 cols for text
        // "café" is 4 display cols, fits exactly → "café..."
        let result = truncate_display("café latte", 7);
        assert_eq!(result, "café...");
        assert!(display_width(&result) <= 7);
    }

    #[test]
    fn cjk_wide_chars() {
        // Each CJK char is 2 columns. "日本語" = 6 cols.
        assert_eq!(truncate_display("日本語", 6), "日本語");
        // Budget 5: only "日" fits (2 cols) + "..." (3 cols) = 5
        let result = truncate_display("日本語", 5);
        assert_eq!(result, "日...");
        assert!(display_width(&result) <= 5);
    }

    #[test]
    fn cjk_truncation_avoids_half_char() {
        // Budget 4: "日" is 2 cols, leaving 2 for ellipsis which needs 3
        // So only fits nothing + "..." = 3 cols
        let result = truncate_display("日本語", 4);
        // Should be "..." with zero chars because no CJK char + "..." fits in 4
        // Actually: budget = 4 - 3 = 1. "日" is 2 cols > 1. So 0 chars, "..."
        assert_eq!(result, "...");
        assert!(display_width(&result) <= 4);
    }

    #[test]
    fn emoji_handling() {
        // Simple emoji like "👍" is typically 2 columns
        let s = "Hello 👍 world";
        let result = truncate_display(s, 8);
        assert!(display_width(&result) <= 8);
        assert!(result.ends_with("..."));
    }

    #[test]
    fn tiny_budget_returns_as_is() {
        // Budget < 4 → return as-is
        assert_eq!(truncate_display("hello", 3), "hello");
        assert_eq!(truncate_display("hello", 0), "hello");
    }

    #[test]
    fn empty_string() {
        assert_eq!(truncate_display("", 10), "");
    }

    #[test]
    fn newlines_in_string() {
        // Newlines have 0 display width but are still characters
        let result = truncate_display("line1\nline2\nline3", 8);
        assert!(result.ends_with("..."));
    }

    // ── truncate_display_start ──────────────────────────────────────

    #[test]
    fn start_truncate_fits() {
        assert_eq!(
            truncate_display_start("/short/path.rs", 20),
            "/short/path.rs"
        );
    }

    #[test]
    fn start_truncate_long_path() {
        let path = "/very/long/deep/nested/directory/file.rs";
        let result = truncate_display_start(path, 20);
        assert!(result.starts_with("..."));
        assert!(display_width(&result) <= 20);
    }

    #[test]
    fn start_truncate_multibyte() {
        let s = "前置テキスト/ファイル名.rs";
        let result = truncate_display_start(s, 15);
        assert!(result.starts_with("..."));
        assert!(display_width(&result) <= 15);
    }

    // ── display_width ───────────────────────────────────────────────

    #[test]
    fn width_ascii() {
        assert_eq!(display_width("hello"), 5);
    }

    #[test]
    fn width_multibyte() {
        // 'é' is 1 column despite being 2 bytes in UTF-8
        assert_eq!(display_width("café"), 4);
    }

    #[test]
    fn width_cjk() {
        assert_eq!(display_width("日本語"), 6);
    }

    #[test]
    fn width_empty() {
        assert_eq!(display_width(""), 0);
    }

    #[test]
    fn width_mixed() {
        // "Hi 日本" = 2 + 1 + 2 + 2 = 7
        assert_eq!(display_width("Hi 日本"), 7);
    }

    // ── truncate_bytes_safe ─────────────────────────────────────────

    #[test]
    fn bytes_safe_within_limit() {
        let (s, truncated) = truncate_bytes_safe("hello", 10);
        assert_eq!(s, "hello");
        assert!(!truncated);
    }

    #[test]
    fn bytes_safe_ascii_truncated() {
        let (s, truncated) = truncate_bytes_safe("hello world", 5);
        assert_eq!(s, "hello");
        assert!(truncated);
    }

    #[test]
    fn bytes_safe_multibyte_boundary() {
        // "café" in UTF-8: c(1) a(1) f(1) é(2) = 5 bytes
        // Truncating at 4 bytes would land inside 'é', should back up to 3
        let (s, truncated) = truncate_bytes_safe("café", 4);
        assert_eq!(s, "caf");
        assert!(truncated);
    }

    #[test]
    fn bytes_safe_exact() {
        let (s, truncated) = truncate_bytes_safe("café", 5);
        assert_eq!(s, "café");
        assert!(!truncated);
    }

    #[test]
    fn bytes_safe_cjk() {
        // "日" is 3 bytes in UTF-8. "日本" is 6 bytes.
        let (s, truncated) = truncate_bytes_safe("日本語", 7);
        // 7 bytes: "日"(3) + "本"(3) = 6, next "語" starts at 6, boundary at 6
        assert_eq!(s, "日本");
        assert!(truncated);
    }

    #[test]
    fn bytes_safe_empty() {
        let (s, truncated) = truncate_bytes_safe("", 10);
        assert_eq!(s, "");
        assert!(!truncated);
    }
}
