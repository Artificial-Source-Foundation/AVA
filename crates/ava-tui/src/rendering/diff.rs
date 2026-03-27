use crate::state::theme::Theme;
use crate::text_utils::truncate_display;
use ratatui::style::{Modifier, Style};
use ratatui::text::{Line, Span};
use similar::{ChangeTag, TextDiff};

pub fn render_diff(old: &str, new: &str, theme: &Theme) -> Vec<Line<'static>> {
    let diff = TextDiff::from_lines(old, new);
    let changes: Vec<_> = diff.iter_all_changes().collect();
    let mut lines = Vec::new();
    let mut i = 0;

    while i < changes.len() {
        let change = &changes[i];
        match change.tag() {
            ChangeTag::Equal => {
                let value = change.value().trim_end_matches('\n');
                lines.push(Line::from(Span::styled(
                    format!(" {value}"),
                    Style::default().fg(theme.diff_context),
                )));
                i += 1;
            }
            ChangeTag::Delete => {
                // Check if next change is an Insert (delete+insert pair for word-level diff)
                if i + 1 < changes.len() && changes[i + 1].tag() == ChangeTag::Insert {
                    let old_line = change.value().trim_end_matches('\n');
                    let new_line = changes[i + 1].value().trim_end_matches('\n');
                    let (del_spans, add_spans) = word_level_spans(old_line, new_line, theme);
                    lines.push(Line::from(del_spans));
                    lines.push(Line::from(add_spans));
                    i += 2;
                } else {
                    let value = change.value().trim_end_matches('\n');
                    lines.push(Line::from(Span::styled(
                        format!("-{value}"),
                        Style::default()
                            .fg(theme.diff_removed)
                            .bg(theme.diff_removed_bg),
                    )));
                    i += 1;
                }
            }
            ChangeTag::Insert => {
                let value = change.value().trim_end_matches('\n');
                lines.push(Line::from(Span::styled(
                    format!("+{value}"),
                    Style::default()
                        .fg(theme.diff_added)
                        .bg(theme.diff_added_bg),
                )));
                i += 1;
            }
        }
    }

    lines
}

fn word_level_spans(
    old_line: &str,
    new_line: &str,
    theme: &Theme,
) -> (Vec<Span<'static>>, Vec<Span<'static>>) {
    let word_diff = TextDiff::from_words(old_line, new_line);
    let del_base = Style::default()
        .fg(theme.diff_removed)
        .bg(theme.diff_removed_bg);
    let add_base = Style::default()
        .fg(theme.diff_added)
        .bg(theme.diff_added_bg);

    let mut del_spans: Vec<Span<'static>> = vec![Span::styled("-".to_string(), del_base)];
    let mut add_spans: Vec<Span<'static>> = vec![Span::styled("+".to_string(), add_base)];

    for change in word_diff.iter_all_changes() {
        let value = change.value().to_string();
        match change.tag() {
            ChangeTag::Equal => {
                del_spans.push(Span::styled(value.clone(), del_base));
                add_spans.push(Span::styled(value, add_base));
            }
            ChangeTag::Delete => {
                del_spans.push(Span::styled(
                    value,
                    del_base
                        .fg(theme.diff_removed_highlight)
                        .add_modifier(Modifier::BOLD),
                ));
            }
            ChangeTag::Insert => {
                add_spans.push(Span::styled(
                    value,
                    add_base
                        .fg(theme.diff_added_highlight)
                        .add_modifier(Modifier::BOLD),
                ));
            }
        }
    }

    (del_spans, add_spans)
}

// ---------------------------------------------------------------------------
// Side-by-side diff rendering
// ---------------------------------------------------------------------------

/// Minimum content width (excluding bar prefix) to use side-by-side layout.
/// Below this we fall back to unified diff.
const MIN_SBS_WIDTH: u16 = 60;

/// A parsed row for the side-by-side view.
enum SbsRow {
    /// Separator between hunks (shown as `⋮` or `···`).
    HunkSep,
    /// Context line visible on both sides.
    Both {
        old_no: usize,
        new_no: usize,
        content: String,
    },
    /// Removed line — left side only.
    Left { old_no: usize, content: String },
    /// Added line — right side only.
    Right { new_no: usize, content: String },
    /// Changed pair — old on left, new on right.
    Pair {
        old_no: usize,
        old_content: String,
        new_no: usize,
        new_content: String,
    },
}

/// A parsed hunk header from unified diff (`@@ -a,b +c,d @@`).
struct HunkHeader {
    old_start: usize,
    new_start: usize,
}

fn parse_hunk_header(line: &str) -> Option<HunkHeader> {
    let trimmed = line.strip_prefix("@@ ")?;
    let end = trimmed.find(" @@")?;
    let ranges = &trimmed[..end];
    let mut parts = ranges.split_whitespace();
    let old_range = parts.next()?.strip_prefix('-')?;
    let new_range = parts.next()?.strip_prefix('+')?;
    let old_start: usize = old_range.split(',').next()?.parse().ok()?;
    let new_start: usize = new_range.split(',').next()?.parse().ok()?;
    Some(HunkHeader {
        old_start,
        new_start,
    })
}

fn normalize_diff_path(raw: &str) -> Option<String> {
    let path = raw.trim();
    if path == "/dev/null" || path.is_empty() {
        return None;
    }
    Some(
        path.strip_prefix("a/")
            .or_else(|| path.strip_prefix("b/"))
            .unwrap_or(path)
            .to_string(),
    )
}

fn find_unified_diff_start(content: &str) -> Option<usize> {
    let mut offset = 0;
    for line in content.split_inclusive('\n') {
        if line.starts_with("--- ") {
            return Some(offset);
        }
        offset += line.len();
    }
    None
}

fn finalize_pending_diff_block(
    rows: &mut Vec<SbsRow>,
    removes: &mut Vec<(usize, String)>,
    adds: &mut Vec<(usize, String)>,
) {
    let max_len = removes.len().max(adds.len());
    for index in 0..max_len {
        match (removes.get(index), adds.get(index)) {
            (Some((old_no, old_content)), Some((new_no, new_content))) => rows.push(SbsRow::Pair {
                old_no: *old_no,
                old_content: old_content.clone(),
                new_no: *new_no,
                new_content: new_content.clone(),
            }),
            (Some((old_no, old_content)), None) => rows.push(SbsRow::Left {
                old_no: *old_no,
                content: old_content.clone(),
            }),
            (None, Some((new_no, new_content))) => rows.push(SbsRow::Right {
                new_no: *new_no,
                content: new_content.clone(),
            }),
            (None, None) => {}
        }
    }
    removes.clear();
    adds.clear();
}

/// Parse unified diff text (from tool result content) into side-by-side rows.
fn parse_unified_to_sbs_rows(content: &str) -> (Option<String>, Vec<SbsRow>) {
    let mut old_path: Option<String> = None;
    let mut new_path: Option<String> = None;
    let mut rows: Vec<SbsRow> = Vec::new();
    let mut old_no: usize = 0;
    let mut new_no: usize = 0;
    let mut in_diff = false;
    let mut first_hunk = true;

    // Buffer for pairing consecutive removes with adds
    let mut removes: Vec<(usize, String)> = Vec::new();
    let mut adds: Vec<(usize, String)> = Vec::new();

    for raw_line in content.lines() {
        if let Some(path) = raw_line.strip_prefix("--- ") {
            old_path = normalize_diff_path(path);
            continue;
        }
        if let Some(path) = raw_line.strip_prefix("+++ ") {
            new_path = normalize_diff_path(path);
            continue;
        }

        // Hunk header
        if raw_line.starts_with("@@ ") {
            finalize_pending_diff_block(&mut rows, &mut removes, &mut adds);

            if let Some(hh) = parse_hunk_header(raw_line) {
                if !first_hunk {
                    rows.push(SbsRow::HunkSep);
                }
                first_hunk = false;
                old_no = hh.old_start;
                new_no = hh.new_start;
                in_diff = true;
            }
            continue;
        }

        if !in_diff {
            continue;
        }

        if let Some(removed) = raw_line.strip_prefix('-') {
            if !adds.is_empty() {
                finalize_pending_diff_block(&mut rows, &mut removes, &mut adds);
            }
            removes.push((old_no, removed.to_string()));
            old_no += 1;
        } else if let Some(added) = raw_line.strip_prefix('+') {
            adds.push((new_no, added.to_string()));
            new_no += 1;
        } else {
            // Context line (starts with ' ' or is plain)
            finalize_pending_diff_block(&mut rows, &mut removes, &mut adds);
            let ctx = raw_line.strip_prefix(' ').unwrap_or(raw_line);
            rows.push(SbsRow::Both {
                old_no,
                new_no,
                content: ctx.to_string(),
            });
            old_no += 1;
            new_no += 1;
        }
    }

    finalize_pending_diff_block(&mut rows, &mut removes, &mut adds);

    (new_path.or(old_path), rows)
}

/// Render a side-by-side diff from unified diff text (tool result content).
///
/// Returns `None` if the width is too narrow or the content has no parseable diff.
/// The caller should fall back to unified rendering in that case.
pub fn render_side_by_side(content: &str, theme: &Theme, width: u16) -> Option<Vec<Line<'static>>> {
    if width < MIN_SBS_WIDTH {
        return None;
    }

    // Find the start of the unified diff (skip summary lines)
    let diff_start = find_unified_diff_start(content)?;
    let diff_text = &content[diff_start..];

    // Also grab the summary line(s) before the diff
    let summary = content[..diff_start].trim();

    let (file_path, rows) = parse_unified_to_sbs_rows(diff_text);
    if rows.is_empty() {
        return None;
    }

    // Layout: each half = (width - 1) / 2, center divider = │
    let half_width = ((width as usize).saturating_sub(1)) / 2;
    let gutter_w: usize = 4; // line numbers up to 9999
                             // Content area per side: half_width - gutter - marker - spaces
                             // Format: `NNN  content` or `NNN -content` or `NNN +content`
                             // = gutter_w + 1(space) + 1(marker) + content
    let content_w = half_width.saturating_sub(gutter_w + 2);
    if content_w < 8 {
        return None;
    }

    let mut lines: Vec<Line<'static>> = Vec::new();

    // File header
    let header_text = if let Some(ref path) = file_path {
        format!("\u{2190} Patched {path}")
    } else {
        "\u{2190} Patched".to_string()
    };

    // Summary + file header
    if !summary.is_empty() {
        lines.push(Line::from(Span::styled(
            summary.to_string(),
            Style::default().fg(theme.text_dimmed),
        )));
    }
    lines.push(Line::from(Span::styled(
        header_text,
        Style::default()
            .fg(theme.diff_hunk_header)
            .add_modifier(Modifier::BOLD),
    )));
    // Separator under header
    lines.push(Line::from(Span::styled(
        "\u{2500}".repeat(width as usize),
        Style::default().fg(theme.border),
    )));

    let ctx_style = Style::default().fg(theme.diff_context);
    let removed_style = Style::default()
        .fg(theme.diff_removed)
        .bg(theme.diff_removed_bg);
    let removed_highlight = Style::default()
        .fg(theme.diff_removed_highlight)
        .bg(theme.diff_removed_bg)
        .add_modifier(Modifier::BOLD);
    let added_style = Style::default()
        .fg(theme.diff_added)
        .bg(theme.diff_added_bg);
    let added_highlight = Style::default()
        .fg(theme.diff_added_highlight)
        .bg(theme.diff_added_bg)
        .add_modifier(Modifier::BOLD);
    let empty_style = Style::default().fg(theme.text_dimmed);
    let gutter_style = Style::default().fg(theme.text_dimmed);
    let divider_style = Style::default().fg(theme.border);

    for row in &rows {
        let spans = match row {
            SbsRow::HunkSep => {
                let sep_char = "\u{22ee}"; // ⋮
                let left_pad = " ".repeat(half_width.saturating_sub(gutter_w + 1));
                vec![
                    Span::styled(
                        format!("{:>gutter_w$} {sep_char}{left_pad}", ""),
                        Style::default().fg(theme.text_dimmed),
                    ),
                    Span::styled("\u{2502}", divider_style),
                    Span::styled(
                        format!("{:>gutter_w$} {sep_char}", ""),
                        Style::default().fg(theme.text_dimmed),
                    ),
                ]
            }
            SbsRow::Both {
                old_no,
                new_no,
                content: ctx,
            } => {
                let left_text = truncate_display(ctx, content_w);
                let right_text = truncate_display(ctx, content_w);
                let left_pad = " ".repeat(content_w.saturating_sub(display_width_fast(&left_text)));
                let right_pad =
                    " ".repeat(content_w.saturating_sub(display_width_fast(&right_text)));
                vec![
                    Span::styled(format!("{old_no:>gutter_w$}"), gutter_style),
                    Span::styled(format!("  {left_text}{left_pad}"), ctx_style),
                    Span::styled("\u{2502}", divider_style),
                    Span::styled(format!("{new_no:>gutter_w$}"), gutter_style),
                    Span::styled(format!("  {right_text}{right_pad}"), ctx_style),
                ]
            }
            SbsRow::Left {
                old_no,
                content: text,
            } => {
                let left_text = truncate_display(text, content_w);
                let left_pad = " ".repeat(content_w.saturating_sub(display_width_fast(&left_text)));
                let right_empty = " ".repeat(half_width);
                vec![
                    Span::styled(format!("{old_no:>gutter_w$}"), gutter_style),
                    Span::styled(format!(" -{left_text}{left_pad}"), removed_style),
                    Span::styled("\u{2502}", divider_style),
                    Span::styled(right_empty, empty_style),
                ]
            }
            SbsRow::Right {
                new_no,
                content: text,
            } => {
                let left_empty = " ".repeat(half_width);
                let right_text = truncate_display(text, content_w);
                let right_pad =
                    " ".repeat(content_w.saturating_sub(display_width_fast(&right_text)));
                vec![
                    Span::styled(left_empty, empty_style),
                    Span::styled("\u{2502}", divider_style),
                    Span::styled(format!("{new_no:>gutter_w$}"), gutter_style),
                    Span::styled(format!(" +{right_text}{right_pad}"), added_style),
                ]
            }
            SbsRow::Pair {
                old_no,
                old_content,
                new_no,
                new_content,
            } => {
                // Word-level diff within the pair
                let word_diff = TextDiff::from_words(old_content.as_str(), new_content.as_str());
                let changes: Vec<_> = word_diff.iter_all_changes().collect();

                // Build left spans (removed side)
                let mut left_spans: Vec<Span<'static>> = vec![
                    Span::styled(format!("{old_no:>gutter_w$}"), gutter_style),
                    Span::styled(" -".to_string(), removed_style),
                ];
                let mut left_col: usize = 0;
                for change in &changes {
                    if left_col >= content_w {
                        break;
                    }
                    let val = change.value();
                    match change.tag() {
                        ChangeTag::Equal => {
                            let remaining = content_w - left_col;
                            let truncated = truncate_display(val, remaining);
                            left_col += display_width_fast(&truncated);
                            left_spans.push(Span::styled(truncated, removed_style));
                        }
                        ChangeTag::Delete => {
                            let remaining = content_w - left_col;
                            let truncated = truncate_display(val, remaining);
                            left_col += display_width_fast(&truncated);
                            left_spans.push(Span::styled(truncated, removed_highlight));
                        }
                        ChangeTag::Insert => {} // skip on left side
                    }
                }
                // Pad left to half_width
                if left_col < content_w {
                    left_spans.push(Span::styled(
                        " ".repeat(content_w - left_col),
                        removed_style,
                    ));
                }

                // Build right spans (added side)
                let mut right_spans: Vec<Span<'static>> = vec![
                    Span::styled("\u{2502}", divider_style),
                    Span::styled(format!("{new_no:>gutter_w$}"), gutter_style),
                    Span::styled(" +".to_string(), added_style),
                ];
                let mut right_col: usize = 0;
                for change in &changes {
                    if right_col >= content_w {
                        break;
                    }
                    let val = change.value();
                    match change.tag() {
                        ChangeTag::Equal => {
                            let remaining = content_w - right_col;
                            let truncated = truncate_display(val, remaining);
                            right_col += display_width_fast(&truncated);
                            right_spans.push(Span::styled(truncated, added_style));
                        }
                        ChangeTag::Insert => {
                            let remaining = content_w - right_col;
                            let truncated = truncate_display(val, remaining);
                            right_col += display_width_fast(&truncated);
                            right_spans.push(Span::styled(truncated, added_highlight));
                        }
                        ChangeTag::Delete => {} // skip on right side
                    }
                }

                let mut all_spans = left_spans;
                all_spans.extend(right_spans);
                all_spans
            }
        };

        lines.push(Line::from(spans));
    }

    Some(lines)
}

/// Fast ASCII-optimized display width (falls back to unicode for non-ASCII).
fn display_width_fast(s: &str) -> usize {
    if s.is_ascii() {
        s.len()
    } else {
        crate::text_utils::safe_display_width(s)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn word_level_diff_produces_highlighted_spans() {
        let theme = Theme::default_theme();
        let old = "fn hello() {}";
        let new = "fn world() {}";
        let (del, add) = word_level_spans(old, new, &theme);
        // Should have prefix "-" + at least some spans
        assert!(del.len() >= 2);
        assert!(add.len() >= 2);
        // Check that bold modifier is present on changed words
        let bold_del = del
            .iter()
            .any(|s| s.style.add_modifier.contains(Modifier::BOLD));
        let bold_add = add
            .iter()
            .any(|s| s.style.add_modifier.contains(Modifier::BOLD));
        assert!(bold_del, "delete line should have bold highlighted word");
        assert!(bold_add, "add line should have bold highlighted word");
    }

    #[test]
    fn render_diff_equal_lines() {
        let theme = Theme::default_theme();
        let lines = render_diff("a\nb\n", "a\nb\n", &theme);
        assert_eq!(lines.len(), 2);
    }

    #[test]
    fn render_diff_paired_change_uses_word_level() {
        let theme = Theme::default_theme();
        let lines = render_diff("hello world\n", "hello rust\n", &theme);
        // Should produce 2 lines (word-level delete + insert pair)
        assert_eq!(lines.len(), 2);
        // First line should have multiple spans (word-level)
        assert!(lines[0].spans.len() > 1);
    }

    // --- Side-by-side tests ---

    #[test]
    fn parse_hunk_header_basic() {
        let hh = parse_hunk_header("@@ -10,7 +10,8 @@").unwrap();
        assert_eq!(hh.old_start, 10);
        assert_eq!(hh.new_start, 10);
    }

    #[test]
    fn parse_hunk_header_single_line() {
        let hh = parse_hunk_header("@@ -1 +1 @@").unwrap();
        assert_eq!(hh.old_start, 1);
        assert_eq!(hh.new_start, 1);
    }

    #[test]
    fn parse_hunk_header_with_context() {
        // Some tools append function context after @@
        let hh = parse_hunk_header("@@ -5,3 +5,4 @@ fn main() {").unwrap();
        assert_eq!(hh.old_start, 5);
        assert_eq!(hh.new_start, 5);
    }

    #[test]
    fn parse_unified_to_rows_simple_change() {
        let diff = "\
--- a/test.rs
+++ b/test.rs
@@ -1,3 +1,3 @@
 hello
-world
+rust
 end
";
        let (path, rows) = parse_unified_to_sbs_rows(diff);
        assert_eq!(path.as_deref(), Some("test.rs"));
        // Expect: Both(hello), Pair(world/rust), Both(end)
        assert_eq!(rows.len(), 3);
        assert!(matches!(
            &rows[0],
            SbsRow::Both {
                old_no: 1,
                new_no: 1,
                ..
            }
        ));
        assert!(matches!(
            &rows[1],
            SbsRow::Pair {
                old_no: 2,
                new_no: 2,
                ..
            }
        ));
        assert!(matches!(
            &rows[2],
            SbsRow::Both {
                old_no: 3,
                new_no: 3,
                ..
            }
        ));
    }

    #[test]
    fn parse_unified_to_rows_addition_only() {
        let diff = "\
--- a/file.rs
+++ b/file.rs
@@ -1,2 +1,3 @@
 line1
+new_line
 line2
";
        let (_, rows) = parse_unified_to_sbs_rows(diff);
        // Expect: Both(line1), Right(new_line), Both(line2)
        assert_eq!(rows.len(), 3);
        assert!(matches!(&rows[0], SbsRow::Both { .. }));
        assert!(matches!(&rows[1], SbsRow::Right { new_no: 2, .. }));
        assert!(matches!(&rows[2], SbsRow::Both { .. }));
    }

    #[test]
    fn parse_unified_to_rows_deletion_only() {
        let diff = "\
--- a/file.rs
+++ b/file.rs
@@ -1,3 +1,2 @@
 line1
-removed
 line2
";
        let (_, rows) = parse_unified_to_sbs_rows(diff);
        // Expect: Both(line1), Left(removed), Both(line2)
        assert_eq!(rows.len(), 3);
        assert!(matches!(&rows[0], SbsRow::Both { .. }));
        assert!(matches!(&rows[1], SbsRow::Left { old_no: 2, .. }));
        assert!(matches!(&rows[2], SbsRow::Both { .. }));
    }

    #[test]
    fn parse_unified_multiple_hunks_produces_separator() {
        let diff = "\
--- a/file.rs
+++ b/file.rs
@@ -1,3 +1,3 @@
 ctx
-old1
+new1
 ctx
@@ -10,3 +10,3 @@
 ctx2
-old2
+new2
 ctx2
";
        let (_, rows) = parse_unified_to_sbs_rows(diff);
        // Should contain a HunkSep between the two hunks
        assert!(rows.iter().any(|r| matches!(r, SbsRow::HunkSep)));
    }

    #[test]
    fn parse_unified_multiline_replace_pairs_all_lines() {
        let diff = "\
--- a/file.rs
+++ b/file.rs
@@ -1,4 +1,4 @@
 keep
-old one
-old two
+new one
+new two
 keep
";
        let (_, rows) = parse_unified_to_sbs_rows(diff);

        assert!(matches!(
            &rows[1],
            SbsRow::Pair {
                old_no: 2,
                new_no: 2,
                ..
            }
        ));
        assert!(matches!(
            &rows[2],
            SbsRow::Pair {
                old_no: 3,
                new_no: 3,
                ..
            }
        ));
    }

    #[test]
    fn parse_unified_prefers_new_path_for_added_files() {
        let diff = "\
--- /dev/null
+++ b/src/new_file.rs
@@ -0,0 +1,1 @@
+fn main() {}
";
        let (path, rows) = parse_unified_to_sbs_rows(diff);

        assert_eq!(path.as_deref(), Some("src/new_file.rs"));
        assert_eq!(rows.len(), 1);
        assert!(matches!(&rows[0], SbsRow::Right { new_no: 1, .. }));
    }

    #[test]
    fn render_side_by_side_returns_none_for_narrow() {
        let theme = Theme::default_theme();
        let content = "Applied edit\n\n--- a/test.rs\n+++ b/test.rs\n@@ -1,1 +1,1 @@\n-old\n+new\n";
        assert!(render_side_by_side(content, &theme, 40).is_none());
    }

    #[test]
    fn render_side_by_side_returns_none_without_diff() {
        let theme = Theme::default_theme();
        assert!(render_side_by_side("no diff here", &theme, 120).is_none());
    }

    #[test]
    fn render_side_by_side_handles_diff_without_summary_prefix() {
        let theme = Theme::default_theme();
        let content = "--- a/test.rs
+++ b/test.rs
@@ -1,1 +1,1 @@
-old
+new
";
        assert!(render_side_by_side(content, &theme, 120).is_some());
    }

    #[test]
    fn render_side_by_side_produces_lines() {
        let theme = Theme::default_theme();
        let content = "\
Applied exact-match; changed 1 lines

--- a/src/main.rs
+++ b/src/main.rs
@@ -1,3 +1,3 @@
 fn main() {
-    println!(\"hello\");
+    println!(\"world\");
 }
";
        let lines = render_side_by_side(content, &theme, 120).expect("should produce lines");
        // header (summary + file header + separator) + 3 content rows = 6
        assert!(lines.len() >= 6, "got {} lines", lines.len());

        // Check file header contains the path
        let header_text: String = lines[1].spans.iter().map(|s| s.content.as_ref()).collect();
        assert!(
            header_text.contains("src/main.rs"),
            "header should contain file path, got: {header_text}"
        );
    }

    #[test]
    fn render_side_by_side_word_level_highlighting() {
        let theme = Theme::default_theme();
        let content = "\
Changed 1 lines

--- a/lib.rs
+++ b/lib.rs
@@ -1,1 +1,1 @@
-fn hello() {}
+fn world() {}
";
        let lines = render_side_by_side(content, &theme, 120).expect("should produce lines");
        // The Pair row should have word-level spans (more than just gutter + single content span)
        // Find the pair row (after header lines)
        let pair_line = &lines[3]; // skip summary, header, separator
                                   // Pair rows have word-level highlighting so they have multiple spans
        assert!(
            pair_line.spans.len() > 4,
            "pair row should have word-level spans, got {} spans",
            pair_line.spans.len()
        );
    }

    #[test]
    fn display_width_fast_ascii() {
        assert_eq!(display_width_fast("hello"), 5);
        assert_eq!(display_width_fast(""), 0);
    }
}
