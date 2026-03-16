use crate::state::messages::{inline_spinner_frame, MessageKind, UiMessage};
use crate::state::theme::Theme;
use crate::widgets::safe_render::clamp_line;
use ratatui::style::{Modifier, Style};
use ratatui::text::{Line, Span};

pub fn render_message(
    message: &UiMessage,
    theme: &Theme,
    spinner_tick: usize,
    width: u16,
) -> Vec<Line<'static>> {
    message.to_lines(theme, spinner_tick, width)
}

pub fn render_message_with_options(
    message: &UiMessage,
    theme: &Theme,
    spinner_tick: usize,
    width: u16,
    show_thinking: bool,
) -> Vec<Line<'static>> {
    message.to_lines_with_options(theme, spinner_tick, width, show_thinking)
}

/// Generate a human-readable activity description for a single tool call.
/// `active` = true means the tool is currently running.
pub(crate) fn tool_activity_line(content: &str, active: bool) -> String {
    let tool_name = content.split_whitespace().next().unwrap_or("tool");
    let args_str = content[tool_name.len()..].trim_start();

    // Try to extract useful fields from JSON args
    let args: Option<serde_json::Value> = serde_json::from_str(args_str).ok();

    match tool_name {
        "read" => {
            let path = args
                .as_ref()
                .and_then(|v| v.get("file_path").or(v.get("path")))
                .and_then(|v| v.as_str())
                .unwrap_or("file");
            let short = short_path(path);
            if active {
                format!("Reading {short}...")
            } else {
                format!("Read {short}")
            }
        }
        "write" => {
            let path = args
                .as_ref()
                .and_then(|v| v.get("file_path").or(v.get("path")))
                .and_then(|v| v.as_str())
                .unwrap_or("file");
            let short = short_path(path);
            if active {
                format!("Writing {short}...")
            } else {
                format!("Wrote {short}")
            }
        }
        "edit" | "multiedit" => {
            let path = args
                .as_ref()
                .and_then(|v| v.get("file_path").or(v.get("path")))
                .and_then(|v| v.as_str())
                .unwrap_or("file");
            let short = short_path(path);
            if active {
                format!("Editing {short}...")
            } else {
                format!("Edited {short}")
            }
        }
        "bash" => {
            let cmd = args
                .as_ref()
                .and_then(|v| v.get("command").or(v.get("cmd")))
                .and_then(|v| v.as_str())
                .unwrap_or("command");
            let short_cmd = truncate_inline(cmd, 60);
            if active {
                format!("Running: {short_cmd}")
            } else {
                format!("Ran: {short_cmd}")
            }
        }
        "glob" => {
            let pattern = args
                .as_ref()
                .and_then(|v| v.get("pattern"))
                .and_then(|v| v.as_str())
                .unwrap_or("*");
            if active {
                format!("Searching for {pattern}...")
            } else {
                format!("Searched for {pattern}")
            }
        }
        "grep" => {
            let pattern = args
                .as_ref()
                .and_then(|v| v.get("pattern"))
                .and_then(|v| v.as_str())
                .unwrap_or("pattern");
            let short_pat = truncate_inline(pattern, 40);
            if active {
                format!("Searching for '{short_pat}'...")
            } else {
                format!("Searched for '{short_pat}'")
            }
        }
        "apply_patch" => {
            if active {
                "Applying patch...".to_string()
            } else {
                "Applied patch".to_string()
            }
        }
        "task" => {
            if active {
                "Spawning sub-agent...".to_string()
            } else {
                "Sub-agent completed".to_string()
            }
        }
        "codebase_search" => {
            let query = args
                .as_ref()
                .and_then(|v| v.get("query"))
                .and_then(|v| v.as_str())
                .unwrap_or("query");
            let short_q = truncate_inline(query, 40);
            if active {
                format!("Searching codebase for '{short_q}'...")
            } else {
                format!("Searched codebase for '{short_q}'")
            }
        }
        _ => {
            if active {
                format!("Running {tool_name}...")
            } else {
                format!("{tool_name} completed")
            }
        }
    }
}

/// Shorten a file path to just the last 2-3 components for display.
fn short_path(path: &str) -> String {
    let parts: Vec<&str> = path.split('/').filter(|s| !s.is_empty()).collect();
    if parts.len() <= 3 {
        return path.to_string();
    }
    format!(".../{}", parts[parts.len() - 3..].join("/"))
}

/// Truncate a string inline (for commands/patterns), adding "..." if too long.
fn truncate_inline(s: &str, max: usize) -> String {
    let first_line = s.lines().next().unwrap_or(s);
    crate::text_utils::truncate_display(first_line, max)
}

/// Generate a summary of completed tool activity for the action group header.
fn tool_activity_summary(tool_calls: &[&UiMessage]) -> String {
    let mut reads = 0usize;
    let mut writes = 0usize;
    let mut edits = 0usize;
    let mut bashes = 0usize;
    let mut searches = 0usize;
    let mut others = Vec::new();

    for call in tool_calls {
        let tool_name = call.content.split_whitespace().next().unwrap_or("tool");
        match tool_name {
            "read" => reads += 1,
            "write" => writes += 1,
            "edit" | "multiedit" | "apply_patch" => edits += 1,
            "bash" => bashes += 1,
            "glob" | "grep" | "codebase_search" => searches += 1,
            other => {
                if !others.contains(&other.to_string()) {
                    others.push(other.to_string());
                }
            }
        }
    }

    let mut parts = Vec::new();
    if reads > 0 {
        parts.push(format!(
            "read {} file{}",
            reads,
            if reads > 1 { "s" } else { "" }
        ));
    }
    if edits > 0 {
        parts.push(format!(
            "edited {} file{}",
            edits,
            if edits > 1 { "s" } else { "" }
        ));
    }
    if writes > 0 {
        parts.push(format!(
            "wrote {} file{}",
            writes,
            if writes > 1 { "s" } else { "" }
        ));
    }
    if bashes > 0 {
        parts.push(format!(
            "ran {} command{}",
            bashes,
            if bashes > 1 { "s" } else { "" }
        ));
    }
    if searches > 0 {
        parts.push(format!(
            "searched {} pattern{}",
            searches,
            if searches > 1 { "s" } else { "" }
        ));
    }
    for other in &others {
        parts.push(format!("ran {other}"));
    }

    if parts.is_empty() {
        "completed".to_string()
    } else {
        // Capitalize first letter
        let mut summary = parts.join(", ");
        if let Some(first) = summary.get_mut(..1) {
            first.make_ascii_uppercase();
        }
        summary
    }
}

pub fn render_action_group(
    messages: &[&UiMessage],
    theme: &Theme,
    spinner_tick: usize,
    width: u16,
    expanded: bool,
) -> Vec<Line<'static>> {
    let tool_calls: Vec<&UiMessage> = messages
        .iter()
        .copied()
        .filter(|msg| matches!(msg.kind, MessageKind::ToolCall))
        .collect();
    let tool_results: Vec<&UiMessage> = messages
        .iter()
        .copied()
        .filter(|msg| matches!(msg.kind, MessageKind::ToolResult))
        .collect();
    let active = messages
        .last()
        .is_some_and(|msg| matches!(msg.kind, MessageKind::ToolCall));

    // Check if any tool call in this group was cancelled
    let has_cancelled = messages.iter().any(|msg| msg.cancelled);

    let dim = Style::default()
        .fg(theme.text_dimmed)
        .add_modifier(Modifier::DIM);

    if has_cancelled {
        // Interrupted: show dimmed summary — truncate to width so it cannot bleed
        let summary = tool_activity_summary(&tool_calls);
        let max_summary = width.saturating_sub(2 + 14) as usize; // 2 = "● ", 14 = " [interrupted]"
        let summary = crate::text_utils::truncate_display(&summary, max_summary);
        let interrupted_line = clamp_line(
            Line::from(vec![
                Span::styled("\u{25cf} ".to_string(), dim),
                Span::styled(summary, dim),
                Span::styled(" [interrupted]".to_string(), dim),
            ]),
            width as usize,
        );
        let mut lines = vec![interrupted_line];

        if expanded {
            render_expanded_details(&tool_calls, &tool_results, theme, width, &mut lines);
        }

        return lines;
    }

    if active {
        // Currently running: show the current tool activity with spinner
        let current_call = tool_calls.last();
        let activity = current_call
            .map(|c| tool_activity_line(&c.content, true))
            .unwrap_or_else(|| "Running...".to_string());
        // Truncate activity text to fit within width (icon is 2 cols)
        let activity =
            crate::text_utils::truncate_display(&activity, width.saturating_sub(2) as usize);

        let icon = format!("{} ", inline_spinner_frame(spinner_tick));
        let mut lines = vec![Line::from(vec![
            Span::styled(icon, Style::default().fg(theme.accent)),
            Span::styled(activity, Style::default().fg(theme.text_muted)),
        ])];

        // Show file/detail hint for the current tool below the activity line
        if let Some(call) = current_call {
            let detail = tool_detail_hint(&call.content, width);
            if let Some(hint) = detail {
                lines.push(Line::from(vec![
                    Span::raw("  "),
                    Span::styled("\u{2514} ", Style::default().fg(theme.text_dimmed)),
                    Span::styled(
                        hint,
                        Style::default()
                            .fg(theme.text_dimmed)
                            .add_modifier(Modifier::DIM),
                    ),
                ]));
            }
        }

        return lines;
    }

    // Completed: show summary line — truncate to width so it cannot bleed
    let summary = tool_activity_summary(&tool_calls);
    let dim = Style::default()
        .fg(theme.text_dimmed)
        .add_modifier(Modifier::DIM);

    if expanded {
        // Expanded header with ▼ indicator
        let prefix = "\u{25cf} \u{25bc} ";
        let summary = crate::text_utils::truncate_display(
            &summary,
            width.saturating_sub(crate::text_utils::display_width(prefix) as u16) as usize,
        );
        let mut lines = vec![Line::from(vec![
            Span::styled("\u{25cf} ", dim),
            Span::styled("\u{25bc} ", dim), // ▼
            Span::styled(summary, dim),
        ])];
        render_expanded_details(&tool_calls, &tool_results, theme, width, &mut lines);
        lines
    } else {
        // Collapsed header with ▶ indicator
        let prefix = "\u{25cf} \u{25b6} ";
        let summary = crate::text_utils::truncate_display(
            &summary,
            width.saturating_sub(crate::text_utils::display_width(prefix) as u16) as usize,
        );
        vec![Line::from(vec![
            Span::styled("\u{25cf} ", dim),
            Span::styled("\u{25b6} ", dim), // ▶
            Span::styled(summary, dim),
        ])]
    }
}

/// Render expanded details for an action group (tool calls + last result).
fn render_expanded_details(
    tool_calls: &[&UiMessage],
    tool_results: &[&UiMessage],
    theme: &Theme,
    width: u16,
    lines: &mut Vec<Line<'static>>,
) {
    let dim = Style::default()
        .fg(theme.text_dimmed)
        .add_modifier(Modifier::DIM);

    for call in tool_calls.iter().take(5) {
        let detail = tool_activity_line(&call.content, false);
        let preview =
            crate::text_utils::truncate_display(&detail, width.saturating_sub(6) as usize);
        lines.push(Line::from(vec![
            Span::raw("  "),
            Span::styled("\u{2514} ", dim),
            Span::styled(preview, dim),
        ]));
    }
    if tool_calls.len() > 5 {
        lines.push(clamp_line(
            Line::from(Span::styled(
                format!("  ... {} more", tool_calls.len() - 5),
                dim,
            )),
            width as usize,
        ));
    }

    if let Some(last_result) = tool_results.last() {
        let preview = crate::text_utils::truncate_display(
            &last_result.content.replace('\n', " "),
            width.saturating_sub(6) as usize,
        );
        lines.push(Line::from(vec![
            Span::raw("  "),
            Span::styled("\u{21b3} ", Style::default().fg(theme.primary)),
            Span::styled(preview, Style::default().fg(theme.text_muted)),
        ]));
    }
}

/// Extract a detail hint to show below the activity line (e.g., file path, command).
/// The returned string is truncated to fit within the available width (accounting
/// for the `"  └ "` prefix that the caller prepends — 4 display columns).
fn tool_detail_hint(content: &str, width: u16) -> Option<String> {
    let tool_name = content.split_whitespace().next().unwrap_or("tool");
    let args_str = content[tool_name.len()..].trim_start();
    let args: Option<serde_json::Value> = serde_json::from_str(args_str).ok();

    // Budget for the hint text itself: total width minus the 4-column prefix "  └ ".
    let max_hint = width.saturating_sub(4) as usize;

    let hint = match tool_name {
        "read" | "write" | "edit" | "multiedit" => {
            let path = args
                .as_ref()
                .and_then(|v| v.get("file_path").or(v.get("path")))
                .and_then(|v| v.as_str())?;
            Some(crate::text_utils::truncate_display(
                &short_path(path),
                max_hint,
            ))
        }
        "bash" => {
            let cmd = args
                .as_ref()
                .and_then(|v| v.get("command").or(v.get("cmd")))
                .and_then(|v| v.as_str())?;
            let first_line = cmd.lines().next().unwrap_or(cmd);
            Some(crate::text_utils::truncate_display(first_line, max_hint))
        }
        "glob" => {
            let pattern = args
                .as_ref()
                .and_then(|v| v.get("pattern"))
                .and_then(|v| v.as_str())?;
            Some(crate::text_utils::truncate_display(pattern, max_hint))
        }
        "grep" => {
            let pattern = args
                .as_ref()
                .and_then(|v| v.get("pattern"))
                .and_then(|v| v.as_str())?;
            let formatted = format!("'{pattern}'");
            Some(crate::text_utils::truncate_display(&formatted, max_hint))
        }
        _ => None,
    };

    hint
}
