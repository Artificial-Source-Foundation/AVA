use crate::state::permission::{ApprovalRequest, ApprovalStage, PermissionState};
use crate::state::theme::Theme;
use ava_permissions::tags::RiskLevel;
use ratatui::layout::Rect;
use ratatui::style::{Modifier, Style};
use ratatui::text::{Line, Span};
use ratatui::widgets::{Block, Borders, Paragraph};
use ratatui::Frame;

/// Fixed height for the approval dock panel (replaces composer when active).
pub const APPROVAL_DOCK_HEIGHT: u16 = 5;

fn risk_color(level: RiskLevel, theme: &Theme) -> ratatui::style::Color {
    match level {
        RiskLevel::Safe => theme.risk_safe,
        RiskLevel::Low => theme.risk_low,
        RiskLevel::Medium => theme.risk_medium,
        RiskLevel::High => theme.risk_high,
        RiskLevel::Critical => theme.risk_critical,
    }
}

fn risk_label(level: RiskLevel) -> &'static str {
    match level {
        RiskLevel::Safe => "SAFE",
        RiskLevel::Low => "LOW",
        RiskLevel::Medium => "MEDIUM",
        RiskLevel::High => "HIGH",
        RiskLevel::Critical => "CRITICAL",
    }
}

/// Extract a one-line command summary from the JSON arguments.
///
/// For bash tools: returns the `command` field.
/// For edit/write tools: returns the `file_path` field.
/// For other tools: returns a truncated JSON representation.
fn command_summary(call: &ava_types::ToolCall, max_width: usize) -> String {
    let args = &call.arguments;

    // Try common field names in priority order
    let raw = if let Some(cmd) = args.get("command").and_then(|v| v.as_str()) {
        cmd.to_string()
    } else if let Some(path) = args.get("file_path").and_then(|v| v.as_str()) {
        if let Some(old) = args.get("old_string").and_then(|v| v.as_str()) {
            format!("{path} (edit: {old})")
        } else {
            path.to_string()
        }
    } else if let Some(pattern) = args.get("pattern").and_then(|v| v.as_str()) {
        if let Some(path) = args.get("path").and_then(|v| v.as_str()) {
            format!("{pattern} in {path}")
        } else {
            pattern.to_string()
        }
    } else {
        let s = args.to_string();
        if s == "{}" {
            String::new()
        } else {
            s
        }
    };

    // Collapse to single line and truncate
    let single_line: String = raw
        .chars()
        .map(|c| if c == '\n' || c == '\r' { ' ' } else { c })
        .collect();

    crate::text_utils::truncate_display(&single_line, max_width)
}

/// Render the tool approval as a bottom dock bar (OpenCode-style).
///
/// Layout (4-5 rows inside a bordered block):
///   Line 1: △ Permission required                    [MEDIUM]
///   Line 2:   {tool_name}: {command_summary}
///   Line 3: [a] Approve  [s] Allow session  [r] Reject  [Esc]
///     — or stage-specific content (rejection reason input, etc.)
pub fn render_tool_approval(
    frame: &mut Frame<'_>,
    area: Rect,
    request: &ApprovalRequest,
    permission: &PermissionState,
    theme: &Theme,
) {
    // Bordered block with elevated background
    let block = Block::default()
        .style(Style::default().bg(theme.bg_elevated))
        .borders(Borders::ALL)
        .border_style(Style::default().fg(theme.border));
    let inner = block.inner(area);
    frame.render_widget(block, area);

    if inner.height == 0 || inner.width < 10 {
        return;
    }

    let w = inner.width as usize;
    let pad = 1u16; // horizontal padding inside the border
    let content_x = inner.x + pad;
    let content_w = inner.width.saturating_sub(pad * 2);

    // --- Line 1: header with risk badge ---
    let mut header_spans: Vec<Span<'_>> = vec![
        Span::styled(
            "\u{25B3} ",
            Style::default()
                .fg(theme.warning)
                .add_modifier(Modifier::BOLD),
        ),
        Span::styled(
            "Permission required",
            Style::default().fg(theme.text).add_modifier(Modifier::BOLD),
        ),
    ];

    // Right-aligned risk badge
    if let Some(info) = &request.inspection {
        let label = risk_label(info.risk_level);
        let badge = format!("[{label}]");
        let left_len = 2 + 19; // "△ " + "Permission required"
        let gap = w.saturating_sub(left_len + badge.len() + (pad as usize) * 2);
        let spaces = " ".repeat(gap);
        header_spans.push(Span::raw(spaces));
        let color = risk_color(info.risk_level, theme);
        header_spans.push(Span::styled(
            badge,
            Style::default().fg(color).add_modifier(Modifier::BOLD),
        ));
    }

    let header_line = Line::from(header_spans);
    frame.render_widget(
        Paragraph::new(header_line),
        Rect {
            x: content_x,
            y: inner.y,
            width: content_w,
            height: 1,
        },
    );

    // --- Line 2: tool name + command summary ---
    let summary = command_summary(&request.call, w.saturating_sub(request.call.name.len() + 6));
    let detail_text = if summary.is_empty() {
        format!("  {}", request.call.name)
    } else {
        format!("  {}: {}", request.call.name, summary)
    };
    let detail_display = crate::text_utils::truncate_display(&detail_text, content_w as usize);

    let detail_line = Line::from(Span::styled(
        detail_display,
        Style::default().fg(theme.accent),
    ));
    if inner.height > 1 {
        frame.render_widget(
            Paragraph::new(detail_line),
            Rect {
                x: content_x,
                y: inner.y + 1,
                width: content_w,
                height: 1,
            },
        );
    }

    // --- Line 3: action hints (stage-dependent) ---
    if inner.height > 2 {
        let action_y = inner.y + 2;
        let action_area = Rect {
            x: content_x,
            y: action_y,
            width: content_w,
            height: 1,
        };

        match permission.current_stage {
            ApprovalStage::Preview => {
                let line = Line::from(Span::styled(
                    "Press any key to continue...",
                    Style::default().fg(theme.text_muted),
                ));
                frame.render_widget(Paragraph::new(line), action_area);
            }
            ApprovalStage::ActionSelect => {
                let line = Line::from(vec![
                    Span::styled(
                        "[a]",
                        Style::default()
                            .fg(theme.primary)
                            .add_modifier(Modifier::BOLD),
                    ),
                    Span::styled(" Approve  ", Style::default().fg(theme.text_dimmed)),
                    Span::styled(
                        "[s]",
                        Style::default()
                            .fg(theme.text_muted)
                            .add_modifier(Modifier::BOLD),
                    ),
                    Span::styled(" Allow session  ", Style::default().fg(theme.text_dimmed)),
                    Span::styled(
                        "[r]",
                        Style::default()
                            .fg(theme.error)
                            .add_modifier(Modifier::BOLD),
                    ),
                    Span::styled(" Reject  ", Style::default().fg(theme.text_dimmed)),
                    Span::styled(
                        "[y]",
                        Style::default()
                            .fg(theme.text_muted)
                            .add_modifier(Modifier::BOLD),
                    ),
                    Span::styled(" Auto-approve  ", Style::default().fg(theme.text_dimmed)),
                    Span::styled(
                        "[Esc]",
                        Style::default()
                            .fg(theme.text_muted)
                            .add_modifier(Modifier::BOLD),
                    ),
                    Span::styled(" Cancel", Style::default().fg(theme.text_dimmed)),
                ]);
                frame.render_widget(Paragraph::new(line), action_area);
            }
            ApprovalStage::RejectionReason => {
                let line = Line::from(vec![
                    Span::styled("Reason: ", Style::default().fg(theme.text_muted)),
                    Span::styled(
                        permission.rejection_input.clone(),
                        Style::default().fg(theme.text),
                    ),
                    Span::styled("\u{2588}", Style::default().fg(theme.primary)),
                    Span::raw("  "),
                    Span::styled(
                        "Enter",
                        Style::default().fg(theme.text).add_modifier(Modifier::BOLD),
                    ),
                    Span::styled(" confirm  ", Style::default().fg(theme.text_muted)),
                    Span::styled(
                        "Esc",
                        Style::default().fg(theme.text).add_modifier(Modifier::BOLD),
                    ),
                    Span::styled(" cancel", Style::default().fg(theme.text_muted)),
                ]);
                frame.render_widget(Paragraph::new(line), action_area);
            }
        }
    }
}
