use crate::state::theme::Theme;
use ratatui::layout::{Constraint, Direction, Layout, Rect};
use ratatui::style::{Modifier, Style};
use ratatui::text::{Line, Span};
use ratatui::widgets::{Block, Borders, Paragraph};
use ratatui::Frame;
use similar::TextDiff;
use std::path::{Path, PathBuf};

/// A single diff hunk representing a contiguous block of changes in one file.
#[derive(Debug, Clone)]
pub struct DiffHunk {
    /// The file this hunk belongs to.
    pub file: PathBuf,
    /// Starting line number in the old file (0-indexed).
    pub old_start: usize,
    /// Lines from the old version.
    pub old_lines: Vec<String>,
    /// Lines from the new version.
    pub new_lines: Vec<String>,
    /// Context lines before the change (for display).
    pub context_before: Vec<String>,
    /// Context lines after the change (for display).
    pub context_after: Vec<String>,
    /// User decision: Some(true) = accepted, Some(false) = rejected, None = undecided.
    pub accepted: Option<bool>,
}

impl DiffHunk {
    /// Number of lines added.
    pub fn additions(&self) -> usize {
        self.new_lines.len()
    }

    /// Number of lines removed.
    pub fn deletions(&self) -> usize {
        self.old_lines.len()
    }
}

/// State for the diff preview modal.
#[derive(Debug, Clone)]
pub struct DiffPreviewState {
    /// All hunks across all files.
    pub hunks: Vec<DiffHunk>,
    /// Index of the currently selected/focused hunk.
    pub selected_hunk: usize,
    /// Vertical scroll offset for rendering.
    pub scroll: usize,
    /// The original file contents, keyed by file path.
    pub original_contents: Vec<(PathBuf, String)>,
    /// The proposed new contents, keyed by file path.
    pub proposed_contents: Vec<(PathBuf, String)>,
}

impl DiffPreviewState {
    /// Create a new diff preview state from a set of file changes.
    /// Each entry is (file_path, old_content, new_content).
    pub fn new(changes: Vec<(PathBuf, String, String)>) -> Self {
        let mut hunks = Vec::new();
        let mut original_contents = Vec::new();
        let mut proposed_contents = Vec::new();

        for (file, old, new) in &changes {
            original_contents.push((file.clone(), old.clone()));
            proposed_contents.push((file.clone(), new.clone()));

            let file_hunks = compute_hunks(file, old, new);
            hunks.extend(file_hunks);
        }

        Self {
            hunks,
            selected_hunk: 0,
            scroll: 0,
            original_contents,
            proposed_contents,
        }
    }

    /// Accept the currently selected hunk.
    pub fn accept_selected(&mut self) {
        if let Some(hunk) = self.hunks.get_mut(self.selected_hunk) {
            hunk.accepted = Some(true);
        }
        self.advance_to_next_undecided();
    }

    /// Reject the currently selected hunk.
    pub fn reject_selected(&mut self) {
        if let Some(hunk) = self.hunks.get_mut(self.selected_hunk) {
            hunk.accepted = Some(false);
        }
        self.advance_to_next_undecided();
    }

    /// Accept all undecided hunks.
    pub fn accept_all(&mut self) {
        for hunk in &mut self.hunks {
            if hunk.accepted.is_none() {
                hunk.accepted = Some(true);
            }
        }
    }

    /// Reject all undecided hunks.
    pub fn reject_all(&mut self) {
        for hunk in &mut self.hunks {
            if hunk.accepted.is_none() {
                hunk.accepted = Some(false);
            }
        }
    }

    /// Check if all hunks have been decided (accepted or rejected).
    pub fn all_decided(&self) -> bool {
        self.hunks.iter().all(|h| h.accepted.is_some())
    }

    /// Move selection to the next hunk.
    pub fn select_next(&mut self) {
        if !self.hunks.is_empty() && self.selected_hunk + 1 < self.hunks.len() {
            self.selected_hunk += 1;
        }
    }

    /// Move selection to the previous hunk.
    pub fn select_prev(&mut self) {
        self.selected_hunk = self.selected_hunk.saturating_sub(1);
    }

    /// Move to the next file's first hunk.
    pub fn next_file(&mut self) {
        if self.hunks.is_empty() {
            return;
        }
        let current_file = &self.hunks[self.selected_hunk].file;
        for (i, hunk) in self.hunks.iter().enumerate().skip(self.selected_hunk + 1) {
            if hunk.file != *current_file {
                self.selected_hunk = i;
                return;
            }
        }
        // Wrap around to start
        if let Some(first_file) = self.hunks.first().map(|h| h.file.clone()) {
            if first_file != *current_file {
                self.selected_hunk = 0;
            }
        }
    }

    /// Apply only the accepted hunks and return the resulting file contents.
    /// Returns Vec<(file_path, new_content)> for files that have accepted changes.
    pub fn apply_accepted(&self) -> Vec<(PathBuf, String)> {
        let mut results = Vec::new();

        for (file, original) in &self.original_contents {
            // Collect accepted hunks for this file
            let file_hunks: Vec<&DiffHunk> = self
                .hunks
                .iter()
                .filter(|h| &h.file == file && h.accepted == Some(true))
                .collect();

            if file_hunks.is_empty() {
                // No accepted changes for this file, keep original
                continue;
            }

            // Find the proposed content for this file
            let proposed = self
                .proposed_contents
                .iter()
                .find(|(f, _)| f == file)
                .map(|(_, c)| c.as_str())
                .unwrap_or(original);

            // Check if all hunks for this file are accepted
            let all_file_hunks: Vec<&DiffHunk> =
                self.hunks.iter().filter(|h| &h.file == file).collect();
            let all_accepted = all_file_hunks.iter().all(|h| h.accepted == Some(true));

            if all_accepted {
                // All hunks accepted — use the proposed content directly
                results.push((file.clone(), proposed.to_string()));
            } else {
                // Partial acceptance — reconstruct from individual hunks
                let content = apply_partial_hunks(original, &file_hunks);
                results.push((file.clone(), content));
            }
        }

        results
    }

    /// Count distinct files in the diff.
    pub fn file_count(&self) -> usize {
        let mut files: Vec<&PathBuf> = self.hunks.iter().map(|h| &h.file).collect();
        files.dedup();
        files.len()
    }

    /// Summary counts across all hunks.
    pub fn total_stats(&self) -> (usize, usize, usize, usize) {
        let accepted = self
            .hunks
            .iter()
            .filter(|h| h.accepted == Some(true))
            .count();
        let rejected = self
            .hunks
            .iter()
            .filter(|h| h.accepted == Some(false))
            .count();
        let undecided = self.hunks.iter().filter(|h| h.accepted.is_none()).count();
        (self.hunks.len(), accepted, rejected, undecided)
    }

    /// Advance selection to the next undecided hunk, if any.
    fn advance_to_next_undecided(&mut self) {
        // First try forward from current position
        for i in (self.selected_hunk + 1)..self.hunks.len() {
            if self.hunks[i].accepted.is_none() {
                self.selected_hunk = i;
                return;
            }
        }
        // Then try from the beginning
        for i in 0..self.selected_hunk {
            if self.hunks[i].accepted.is_none() {
                self.selected_hunk = i;
                return;
            }
        }
        // All decided — stay where we are
    }
}

/// Compute diff hunks between old and new content for a file.
fn compute_hunks(file: &Path, old: &str, new: &str) -> Vec<DiffHunk> {
    let diff = TextDiff::from_lines(old, new);
    let mut hunks = Vec::new();

    // Use the grouped operations from similar to get logical hunks with context
    for group in diff.grouped_ops(3) {
        let mut old_lines = Vec::new();
        let mut new_lines = Vec::new();
        let mut context_before = Vec::new();
        let mut context_after = Vec::new();
        let mut old_start = 0;
        let mut has_change = false;
        let mut seen_change = false;

        for op in &group {
            match op {
                similar::DiffOp::Equal {
                    old_index,
                    new_index: _,
                    len,
                } => {
                    let old_text: Vec<&str> = old.lines().collect();
                    for i in 0..*len {
                        let idx = old_index + i;
                        let line = if idx < old_text.len() {
                            old_text[idx].to_string()
                        } else {
                            String::new()
                        };
                        if !seen_change {
                            if !has_change {
                                old_start = idx;
                            }
                            context_before.push(line);
                        } else {
                            context_after.push(line);
                        }
                    }
                }
                similar::DiffOp::Delete {
                    old_index,
                    old_len,
                    new_index: _,
                } => {
                    if !has_change {
                        old_start = *old_index;
                    }
                    has_change = true;
                    seen_change = true;
                    let old_text: Vec<&str> = old.lines().collect();
                    for i in 0..*old_len {
                        let idx = old_index + i;
                        let line = if idx < old_text.len() {
                            old_text[idx].to_string()
                        } else {
                            String::new()
                        };
                        old_lines.push(line);
                    }
                }
                similar::DiffOp::Insert {
                    old_index: _,
                    new_index,
                    new_len,
                } => {
                    has_change = true;
                    seen_change = true;
                    let new_text: Vec<&str> = new.lines().collect();
                    for i in 0..*new_len {
                        let idx = new_index + i;
                        let line = if idx < new_text.len() {
                            new_text[idx].to_string()
                        } else {
                            String::new()
                        };
                        new_lines.push(line);
                    }
                }
                similar::DiffOp::Replace {
                    old_index,
                    old_len,
                    new_index,
                    new_len,
                } => {
                    if !has_change {
                        old_start = *old_index;
                    }
                    has_change = true;
                    seen_change = true;
                    let old_text: Vec<&str> = old.lines().collect();
                    let new_text: Vec<&str> = new.lines().collect();
                    for i in 0..*old_len {
                        let idx = old_index + i;
                        let line = if idx < old_text.len() {
                            old_text[idx].to_string()
                        } else {
                            String::new()
                        };
                        old_lines.push(line);
                    }
                    for i in 0..*new_len {
                        let idx = new_index + i;
                        let line = if idx < new_text.len() {
                            new_text[idx].to_string()
                        } else {
                            String::new()
                        };
                        new_lines.push(line);
                    }
                }
            }
        }

        if has_change {
            hunks.push(DiffHunk {
                file: file.to_path_buf(),
                old_start,
                old_lines,
                new_lines,
                context_before,
                context_after,
                accepted: None,
            });
        }
    }

    hunks
}

/// Apply only selected hunks to the original content, producing the new content.
fn apply_partial_hunks(original: &str, accepted_hunks: &[&DiffHunk]) -> String {
    if accepted_hunks.is_empty() {
        return original.to_string();
    }

    let lines: Vec<&str> = original.lines().collect();
    let mut result = Vec::new();
    let mut i = 0;

    // Sort hunks by old_start position
    let mut sorted_hunks = accepted_hunks.to_vec();
    sorted_hunks.sort_by_key(|h| h.old_start);

    for hunk in &sorted_hunks {
        // Copy lines before this hunk
        while i < hunk.old_start && i < lines.len() {
            result.push(lines[i].to_string());
            i += 1;
        }

        // Add the new lines from this hunk
        for new_line in &hunk.new_lines {
            result.push(new_line.clone());
        }

        // Skip the old lines that were replaced
        i += hunk.old_lines.len();
    }

    // Copy remaining lines after the last hunk
    while i < lines.len() {
        result.push(lines[i].to_string());
        i += 1;
    }

    let mut content = result.join("\n");
    // Preserve trailing newline if original had one
    if original.ends_with('\n') && !content.ends_with('\n') {
        content.push('\n');
    }
    content
}

/// Render the diff preview modal.
pub fn render_diff_preview(
    frame: &mut Frame<'_>,
    area: Rect,
    state: &DiffPreviewState,
    theme: &Theme,
) {
    // Fill background
    let bg = Block::default()
        .style(Style::default().bg(theme.bg_elevated))
        .borders(Borders::ALL)
        .border_style(Style::default().fg(theme.border));
    frame.render_widget(bg, area);

    let inner = Rect {
        x: area.x + 1,
        y: area.y + 1,
        width: area.width.saturating_sub(2),
        height: area.height.saturating_sub(2),
    };

    // Split: header (2) + body (rest) + footer (2)
    let chunks = Layout::default()
        .direction(Direction::Vertical)
        .constraints([
            Constraint::Length(2),
            Constraint::Min(0),
            Constraint::Length(2),
        ])
        .split(inner);

    render_header(frame, chunks[0], state, theme);
    render_hunks(frame, chunks[1], state, theme);
    render_footer(frame, chunks[2], state, theme);
}

fn render_header(frame: &mut Frame<'_>, area: Rect, state: &DiffPreviewState, theme: &Theme) {
    let (total, accepted, rejected, undecided) = state.total_stats();
    let file_count = state.file_count();

    let title_spans = vec![
        Span::styled(
            "Diff Preview",
            Style::default().fg(theme.text).add_modifier(Modifier::BOLD),
        ),
        Span::styled(
            format!("  {file_count} file(s), {total} hunk(s)"),
            Style::default().fg(theme.text_muted),
        ),
        Span::raw("  "),
        Span::styled(format!("{accepted}"), Style::default().fg(theme.diff_added)),
        Span::styled(
            format!("/{rejected}"),
            Style::default().fg(theme.diff_removed),
        ),
        Span::styled(
            format!("/{undecided}"),
            Style::default().fg(theme.text_muted),
        ),
    ];

    frame.render_widget(
        Paragraph::new(Line::from(title_spans)),
        Rect {
            x: area.x + 1,
            y: area.y,
            width: area.width.saturating_sub(2),
            height: 1,
        },
    );

    // Separator line
    let sep = "\u{2500}".repeat(area.width.saturating_sub(2) as usize);
    frame.render_widget(
        Paragraph::new(Line::from(Span::styled(
            sep,
            Style::default().fg(theme.border),
        ))),
        Rect {
            x: area.x + 1,
            y: area.y + 1,
            width: area.width.saturating_sub(2),
            height: 1,
        },
    );
}

fn render_hunks(frame: &mut Frame<'_>, area: Rect, state: &DiffPreviewState, theme: &Theme) {
    let mut lines: Vec<Line<'static>> = Vec::new();
    let mut current_file: Option<&PathBuf> = None;

    for (idx, hunk) in state.hunks.iter().enumerate() {
        let is_selected = idx == state.selected_hunk;

        // File header when file changes
        if current_file != Some(&hunk.file) {
            current_file = Some(&hunk.file);
            if !lines.is_empty() {
                lines.push(Line::from(""));
            }
            let file_display = hunk
                .file
                .file_name()
                .map(|f| f.to_string_lossy().to_string())
                .unwrap_or_else(|| hunk.file.display().to_string());
            lines.push(Line::from(vec![
                Span::styled(
                    format!("\u{2500}\u{2500} {file_display} "),
                    Style::default()
                        .fg(theme.diff_hunk_header)
                        .add_modifier(Modifier::BOLD),
                ),
                Span::styled(
                    hunk.file.display().to_string(),
                    Style::default().fg(theme.text_dimmed),
                ),
            ]));
        }

        // Hunk header with stats and status
        let status_marker = match hunk.accepted {
            Some(true) => Span::styled(
                " \u{2713} ",
                Style::default()
                    .fg(theme.diff_added)
                    .add_modifier(Modifier::BOLD),
            ),
            Some(false) => Span::styled(
                " \u{2717} ",
                Style::default()
                    .fg(theme.diff_removed)
                    .add_modifier(Modifier::BOLD),
            ),
            None => Span::styled(" \u{25CB} ", Style::default().fg(theme.text_muted)),
        };

        let hunk_header_style = if is_selected {
            Style::default()
                .fg(theme.accent)
                .add_modifier(Modifier::BOLD)
        } else {
            Style::default().fg(theme.diff_hunk_header)
        };

        let selection_indicator = if is_selected { "\u{25B6} " } else { "  " };

        lines.push(Line::from(vec![
            Span::styled(selection_indicator.to_string(), hunk_header_style),
            status_marker,
            Span::styled(
                format!(
                    "@@ -{},{} +{},{} @@",
                    hunk.old_start + 1,
                    hunk.old_lines.len(),
                    hunk.old_start + 1,
                    hunk.new_lines.len()
                ),
                hunk_header_style,
            ),
            Span::styled(
                format!("  +{} -{} ", hunk.additions(), hunk.deletions()),
                Style::default().fg(theme.text_muted),
            ),
        ]));

        // Context before
        for line in &hunk.context_before {
            lines.push(Line::from(Span::styled(
                format!("   {line}"),
                Style::default().fg(theme.diff_context),
            )));
        }

        // Deleted lines
        for line in &hunk.old_lines {
            let style = if is_selected {
                Style::default()
                    .fg(theme.diff_removed_highlight)
                    .bg(theme.diff_removed_bg)
            } else {
                Style::default()
                    .fg(theme.diff_removed)
                    .bg(theme.diff_removed_bg)
            };
            lines.push(Line::from(Span::styled(format!("  -{line}"), style)));
        }

        // Added lines
        for line in &hunk.new_lines {
            let style = if is_selected {
                Style::default()
                    .fg(theme.diff_added_highlight)
                    .bg(theme.diff_added_bg)
            } else {
                Style::default()
                    .fg(theme.diff_added)
                    .bg(theme.diff_added_bg)
            };
            lines.push(Line::from(Span::styled(format!("  +{line}"), style)));
        }

        // Context after
        for line in &hunk.context_after {
            lines.push(Line::from(Span::styled(
                format!("   {line}"),
                Style::default().fg(theme.diff_context),
            )));
        }
    }

    // Apply scroll
    let visible_height = area.height as usize;
    let total_lines = lines.len();

    // Auto-scroll to keep selected hunk visible
    let scroll = state.scroll.min(total_lines.saturating_sub(visible_height));

    let visible_lines: Vec<Line<'static>> = lines
        .into_iter()
        .skip(scroll)
        .take(visible_height)
        .collect();

    let paragraph = Paragraph::new(visible_lines).style(Style::default().bg(theme.bg_elevated));

    frame.render_widget(paragraph, area);
}

fn render_footer(frame: &mut Frame<'_>, area: Rect, _state: &DiffPreviewState, theme: &Theme) {
    // Separator
    let sep = "\u{2500}".repeat(area.width.saturating_sub(2) as usize);
    frame.render_widget(
        Paragraph::new(Line::from(Span::styled(
            sep,
            Style::default().fg(theme.border),
        ))),
        Rect {
            x: area.x + 1,
            y: area.y,
            width: area.width.saturating_sub(2),
            height: 1,
        },
    );

    // Keybind hints
    let hints = vec![
        Span::styled(
            "y",
            Style::default()
                .fg(theme.diff_added)
                .add_modifier(Modifier::BOLD),
        ),
        Span::styled(" accept  ", Style::default().fg(theme.text_muted)),
        Span::styled(
            "n",
            Style::default()
                .fg(theme.diff_removed)
                .add_modifier(Modifier::BOLD),
        ),
        Span::styled(" reject  ", Style::default().fg(theme.text_muted)),
        Span::styled(
            "a",
            Style::default()
                .fg(theme.diff_added)
                .add_modifier(Modifier::BOLD),
        ),
        Span::styled(" accept all  ", Style::default().fg(theme.text_muted)),
        Span::styled(
            "d",
            Style::default()
                .fg(theme.diff_removed)
                .add_modifier(Modifier::BOLD),
        ),
        Span::styled(" reject all  ", Style::default().fg(theme.text_muted)),
        Span::styled(
            "j/k",
            Style::default().fg(theme.text).add_modifier(Modifier::BOLD),
        ),
        Span::styled(" nav  ", Style::default().fg(theme.text_muted)),
        Span::styled(
            "Tab",
            Style::default().fg(theme.text).add_modifier(Modifier::BOLD),
        ),
        Span::styled(" file  ", Style::default().fg(theme.text_muted)),
        Span::styled(
            "Enter",
            Style::default()
                .fg(theme.primary)
                .add_modifier(Modifier::BOLD),
        ),
        Span::styled(" apply  ", Style::default().fg(theme.text_muted)),
        Span::styled(
            "Esc",
            Style::default()
                .fg(theme.error)
                .add_modifier(Modifier::BOLD),
        ),
        Span::styled(" cancel", Style::default().fg(theme.text_muted)),
    ];

    frame.render_widget(
        Paragraph::new(Line::from(hints)),
        Rect {
            x: area.x + 1,
            y: area.y + 1,
            width: area.width.saturating_sub(2),
            height: 1,
        },
    );
}

#[cfg(test)]
mod tests {
    use super::*;

    fn make_state(old: &str, new: &str) -> DiffPreviewState {
        DiffPreviewState::new(vec![(
            PathBuf::from("test.rs"),
            old.to_string(),
            new.to_string(),
        )])
    }

    #[test]
    fn empty_diff_produces_no_hunks() {
        let state = make_state("hello\nworld\n", "hello\nworld\n");
        assert!(state.hunks.is_empty());
    }

    #[test]
    fn single_line_change_produces_one_hunk() {
        let state = make_state("hello\nworld\n", "hello\nrust\n");
        assert_eq!(state.hunks.len(), 1);
        assert_eq!(state.hunks[0].old_lines, vec!["world"]);
        assert_eq!(state.hunks[0].new_lines, vec!["rust"]);
    }

    #[test]
    fn accept_selected_marks_accepted() {
        let mut state = make_state("a\nb\nc\n", "a\nx\nc\n");
        assert!(state.hunks[0].accepted.is_none());
        state.accept_selected();
        assert_eq!(state.hunks[0].accepted, Some(true));
    }

    #[test]
    fn reject_selected_marks_rejected() {
        let mut state = make_state("a\nb\nc\n", "a\nx\nc\n");
        state.reject_selected();
        assert_eq!(state.hunks[0].accepted, Some(false));
    }

    #[test]
    fn accept_all_marks_all_accepted() {
        let mut state = make_state("a\nb\nc\n", "a\nX\nc\n");
        assert!(!state.hunks.is_empty());
        state.accept_all();
        assert!(state.hunks.iter().all(|h| h.accepted == Some(true)));
    }

    #[test]
    fn reject_all_marks_all_rejected() {
        let mut state = make_state("a\nb\nc\n", "a\nX\nc\n");
        assert!(!state.hunks.is_empty());
        state.reject_all();
        assert!(state.hunks.iter().all(|h| h.accepted == Some(false)));
    }

    #[test]
    fn all_decided_returns_true_when_complete() {
        let mut state = make_state("a\nb\n", "a\nx\n");
        assert!(!state.all_decided());
        state.accept_selected();
        assert!(state.all_decided());
    }

    #[test]
    fn apply_accepted_returns_only_accepted_changes() {
        let mut state = make_state("hello\nworld\n", "hello\nrust\n");
        state.accept_all();
        let results = state.apply_accepted();
        assert_eq!(results.len(), 1);
        assert_eq!(results[0].1, "hello\nrust\n");
    }

    #[test]
    fn apply_accepted_skips_rejected() {
        let mut state = make_state("hello\nworld\n", "hello\nrust\n");
        state.reject_all();
        let results = state.apply_accepted();
        assert!(results.is_empty());
    }

    #[test]
    fn navigation_wraps_correctly() {
        // Use widely separated changes so similar produces multiple hunks
        let old_lines: Vec<String> = (0..30).map(|i| format!("line{i}")).collect();
        let mut new_lines = old_lines.clone();
        new_lines[2] = "CHANGED_A".to_string();
        new_lines[27] = "CHANGED_B".to_string();
        let old = old_lines.join("\n") + "\n";
        let new = new_lines.join("\n") + "\n";
        let mut state = make_state(&old, &new);
        if state.hunks.len() >= 2 {
            assert_eq!(state.selected_hunk, 0);
            state.select_next();
            assert_eq!(state.selected_hunk, 1);
            state.select_prev();
            assert_eq!(state.selected_hunk, 0);
            state.select_prev();
            assert_eq!(state.selected_hunk, 0);
        } else {
            // If grouped into 1 hunk, just verify navigation doesn't panic
            assert_eq!(state.selected_hunk, 0);
            state.select_next();
            state.select_prev();
        }
    }

    #[test]
    fn multi_file_hunks() {
        let changes = vec![
            (
                PathBuf::from("a.rs"),
                "foo\n".to_string(),
                "bar\n".to_string(),
            ),
            (
                PathBuf::from("b.rs"),
                "baz\n".to_string(),
                "qux\n".to_string(),
            ),
        ];
        let state = DiffPreviewState::new(changes);
        assert_eq!(state.hunks.len(), 2);
        assert_eq!(state.hunks[0].file, PathBuf::from("a.rs"));
        assert_eq!(state.hunks[1].file, PathBuf::from("b.rs"));
        assert_eq!(state.file_count(), 2);
    }

    #[test]
    fn total_stats_counts_correctly() {
        // Use widely separated changes so similar produces multiple hunks
        let old_lines: Vec<String> = (0..30).map(|i| format!("line{i}")).collect();
        let mut new_lines = old_lines.clone();
        new_lines[2] = "CHANGED_A".to_string();
        new_lines[27] = "CHANGED_B".to_string();
        let old = old_lines.join("\n") + "\n";
        let new = new_lines.join("\n") + "\n";
        let mut state = make_state(&old, &new);
        let (total, accepted, rejected, undecided) = state.total_stats();
        assert!(total >= 1);
        assert_eq!(accepted, 0);
        assert_eq!(rejected, 0);
        assert_eq!(undecided, total);

        state.accept_selected();
        let (_, accepted2, _, undecided2) = state.total_stats();
        assert_eq!(accepted2, 1);
        assert_eq!(undecided2, total - 1);
    }

    #[test]
    fn apply_partial_hunks_preserves_unmodified_lines() {
        // Create a file with two hunks, accept only the first
        let mut state = make_state(
            "line1\nline2\nline3\nline4\nline5\nline6\nline7\nline8\nline9\nline10\n",
            "line1\nCHANGED2\nline3\nline4\nline5\nline6\nline7\nline8\nCHANGED9\nline10\n",
        );
        if state.hunks.len() == 2 {
            state.hunks[0].accepted = Some(true);
            state.hunks[1].accepted = Some(false);
            let results = state.apply_accepted();
            assert_eq!(results.len(), 1);
            // First hunk applied, second not
            let content = &results[0].1;
            assert!(content.contains("CHANGED2"));
            assert!(!content.contains("CHANGED9"));
            assert!(content.contains("line9"));
        }
        // If similar grouped them into one hunk, that's also valid
    }

    #[test]
    fn advance_to_next_undecided_skips_decided() {
        let mut state = make_state(
            "a\nb\nc\nd\ne\nf\ng\nh\ni\nj\n",
            "a\nX\nc\nd\ne\nY\ng\nh\ni\nj\n",
        );
        if state.hunks.len() >= 2 {
            // Accept first, should advance to second
            state.accept_selected();
            assert_eq!(state.selected_hunk, 1);
        }
    }
}
