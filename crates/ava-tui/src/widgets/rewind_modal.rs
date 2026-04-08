//! Render the rewind modal — shows rewind options with context about what
//! will be restored.

use crate::state::rewind::{RewindOption, RewindState};
use crate::state::theme::Theme;
use crate::widgets::safe_render::truncate_str;
use ratatui::layout::Rect;
use ratatui::style::{Modifier, Style};
use ratatui::text::{Line, Span};
use ratatui::widgets::{Block, Paragraph};
use ratatui::Frame;

pub fn render_rewind_modal(frame: &mut Frame<'_>, area: Rect, rewind: &RewindState, theme: &Theme) {
    let inner_w = area.width.saturating_sub(2) as usize; // account for block borders
    let mut lines: Vec<Line<'_>> = Vec::new();

    // Title
    lines.push(Line::from(Span::styled(
        "Rewind Conversation",
        Style::default()
            .fg(theme.accent)
            .add_modifier(Modifier::BOLD),
    )));
    lines.push(Line::from(""));

    // Show which message we're rewinding to
    if let Some(checkpoint) = rewind.latest_checkpoint() {
        lines.push(Line::from(Span::styled(
            "Restore to the point before you sent:",
            Style::default().fg(theme.text_muted),
        )));
        lines.push(Line::from(""));

        // Truncate preview to fit dynamic width (minus quote + indent)
        let truncated = crate::text_utils::truncate_display(
            &checkpoint.message_preview,
            inner_w.saturating_sub(4),
        );
        let preview = format!("\"{truncated}\"");
        lines.push(Line::from(Span::styled(
            truncate_str(&format!("  {preview}"), inner_w),
            Style::default()
                .fg(theme.text)
                .add_modifier(Modifier::ITALIC),
        )));
        lines.push(Line::from(""));

        // File change stats
        let file_count = rewind.file_change_count_after(rewind.checkpoints.len().saturating_sub(1));
        if file_count > 0 {
            lines.push(Line::from(Span::styled(
                truncate_str(
                    &format!("  {file_count} file(s) changed in this turn"),
                    inner_w,
                ),
                Style::default().fg(theme.text_dimmed),
            )));

            // Show individual file changes (up to 5)
            let checkpoint_idx = rewind.checkpoints.len().saturating_sub(1);
            let changes = &rewind.checkpoints[checkpoint_idx].file_changes;
            for (i, change) in changes.iter().enumerate().take(5) {
                let symbol = match change.change_type {
                    crate::state::rewind::ChangeType::Created => "+",
                    crate::state::rewind::ChangeType::Modified => "~",
                    crate::state::rewind::ChangeType::Deleted => "-",
                };
                // Show just the filename
                let filename = std::path::Path::new(&change.path)
                    .file_name()
                    .and_then(|f| f.to_str())
                    .unwrap_or(&change.path);
                lines.push(Line::from(Span::styled(
                    truncate_str(&format!("    {symbol} {filename}"), inner_w),
                    Style::default().fg(theme.text_dimmed),
                )));
                if i == 4 && changes.len() > 5 {
                    lines.push(Line::from(Span::styled(
                        truncate_str(&format!("    ... and {} more", changes.len() - 5), inner_w),
                        Style::default().fg(theme.text_dimmed),
                    )));
                }
            }
            lines.push(Line::from(""));
        }
    } else {
        lines.push(Line::from(Span::styled(
            "No checkpoints available to rewind to.",
            Style::default().fg(theme.text_muted),
        )));
        lines.push(Line::from(""));
    }

    // Options
    for (i, option) in RewindOption::ALL.iter().enumerate() {
        let is_selected = i == rewind.selected_option;
        let prefix = if is_selected { "> " } else { "  " };
        let number = format!("{}. ", i + 1);

        let style = if is_selected {
            Style::default()
                .fg(theme.accent)
                .add_modifier(Modifier::BOLD)
        } else {
            Style::default().fg(theme.text)
        };

        lines.push(Line::from(vec![
            Span::styled(prefix, style),
            Span::styled(number, Style::default().fg(theme.text_dimmed)),
            Span::styled(option.label(), style),
        ]));

        // Show description for the selected option
        if is_selected {
            lines.push(Line::from(Span::styled(
                truncate_str(&format!("     {}", option.description()), inner_w),
                Style::default()
                    .fg(theme.text_dimmed)
                    .add_modifier(Modifier::ITALIC),
            )));
        }
    }

    lines.push(Line::from(""));

    // Warning
    lines.push(Line::from(Span::styled(
        truncate_str(
            "Note: Rewinding does not affect files edited manually or via bash.",
            inner_w,
        ),
        Style::default()
            .fg(theme.warning)
            .add_modifier(Modifier::ITALIC),
    )));

    lines.push(Line::from(""));

    // Keybind hints
    lines.push(Line::from(Span::styled(
        truncate_str(
            "[1-5] select  [\u{2191}\u{2193}] navigate  [Enter] confirm  [Esc] cancel",
            inner_w,
        ),
        Style::default().fg(theme.text_muted),
    )));

    let block = Block::default().style(Style::default().bg(theme.bg_elevated));
    let paragraph = Paragraph::new(lines).block(block);
    frame.render_widget(paragraph, area);
}
