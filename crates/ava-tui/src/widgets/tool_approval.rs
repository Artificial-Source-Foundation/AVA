use ava_permissions::tags::RiskLevel;
use crate::state::permission::{ApprovalRequest, ApprovalStage, PermissionState};
use crate::state::theme::Theme;
use ratatui::style::{Modifier, Style};
use ratatui::text::{Line, Span};

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

pub fn render_tool_approval_lines(
    request: &ApprovalRequest,
    state: &PermissionState,
    theme: &Theme,
) -> Vec<Line<'static>> {
    let mut lines = vec![
        // Header: warning icon + title
        Line::from(vec![
            Span::styled(
                " \u{26A0}  Tool Approval Required",
                Style::default()
                    .fg(theme.warning)
                    .add_modifier(Modifier::BOLD),
            ),
        ]),
        Line::from(""),
    ];

    // TOOL section
    lines.push(Line::from(Span::styled(
        " TOOL",
        Style::default()
            .fg(theme.text_muted)
            .add_modifier(Modifier::BOLD),
    )));
    lines.push(Line::from(vec![
        Span::styled("   ", Style::default()),
        Span::styled(
            request.call.name.clone(),
            Style::default()
                .fg(theme.warning)
                .add_modifier(Modifier::BOLD),
        ),
    ]));
    lines.push(Line::from(""));

    // COMMAND section: show arguments in a code-style box
    let args_str = request.call.arguments.to_string();
    if !args_str.is_empty() && args_str != "{}" {
        lines.push(Line::from(Span::styled(
            " COMMAND",
            Style::default()
                .fg(theme.text_muted)
                .add_modifier(Modifier::BOLD),
        )));

        // Wrap long argument strings
        let display_args = if args_str.len() > 120 {
            format!("{}...", &args_str[..117])
        } else {
            args_str
        };
        lines.push(Line::from(vec![
            Span::styled("   ", Style::default().bg(theme.bg_deep)),
            Span::styled(
                display_args,
                Style::default().fg(theme.success).bg(theme.bg_deep),
            ),
        ]));
        lines.push(Line::from(""));
    }

    // RISK section
    if let Some(info) = &request.inspection {
        let color = risk_color(info.risk_level, theme);

        lines.push(Line::from(Span::styled(
            " RISK",
            Style::default()
                .fg(theme.text_muted)
                .add_modifier(Modifier::BOLD),
        )));

        // Risk badge: [LEVEL] with colored background
        let label = risk_label(info.risk_level);
        let badge_modifier = if info.risk_level >= RiskLevel::High {
            Modifier::BOLD
        } else {
            Modifier::empty()
        };
        lines.push(Line::from(vec![
            Span::styled("   ", Style::default()),
            Span::styled(
                format!("[{label}]"),
                Style::default().fg(color).add_modifier(badge_modifier),
            ),
        ]));

        // Tags
        if !info.tags.is_empty() {
            let tag_str = info
                .tags
                .iter()
                .map(|t| format!("{t:?}"))
                .collect::<Vec<_>>()
                .join(", ");
            lines.push(Line::from(vec![
                Span::styled("   ", Style::default()),
                Span::styled(
                    tag_str,
                    Style::default().fg(theme.text_dimmed),
                ),
            ]));
        }

        // Warnings
        for warning in &info.warnings {
            lines.push(Line::from(vec![
                Span::styled("   ", Style::default()),
                Span::styled(
                    warning.clone(),
                    Style::default().fg(theme.warning),
                ),
            ]));
        }

        lines.push(Line::from(""));
    }

    // Action buttons / stage
    match state.current_stage {
        ApprovalStage::Preview => {
            lines.push(Line::from(Span::styled(
                " Press any key to continue...",
                Style::default().fg(theme.text_muted),
            )));
        }
        ApprovalStage::ActionSelect => {
            // Separator
            lines.push(Line::from(""));

            // Action hints row
            let key_style = Style::default()
                .fg(theme.text)
                .add_modifier(Modifier::BOLD);
            let desc_style = Style::default().fg(theme.text_muted);

            lines.push(Line::from(vec![
                Span::styled(" a", key_style),
                Span::styled(" approve  ", desc_style),
                Span::styled("s", key_style),
                Span::styled(" session  ", desc_style),
                Span::styled("r", key_style),
                Span::styled(" reject  ", desc_style),
                Span::styled("y", key_style),
                Span::styled(" yolo  ", desc_style),
                Span::styled("Esc", key_style),
                Span::styled(" cancel", desc_style),
            ]));
        }
        ApprovalStage::RejectionReason => {
            lines.push(Line::from(Span::styled(
                " Rejection reason (optional):",
                Style::default().fg(theme.text_muted),
            )));
            let cursor = "\u{2588}";
            lines.push(Line::from(vec![
                Span::styled("   ", Style::default().bg(theme.bg_deep)),
                Span::styled(
                    state.rejection_input.clone(),
                    Style::default().fg(theme.text).bg(theme.bg_deep),
                ),
                Span::styled(
                    cursor,
                    Style::default().fg(theme.primary).bg(theme.bg_deep),
                ),
            ]));
            lines.push(Line::from(""));
            lines.push(Line::from(vec![
                Span::styled(
                    " Enter",
                    Style::default().fg(theme.text).add_modifier(Modifier::BOLD),
                ),
                Span::styled(" confirm  ", Style::default().fg(theme.text_muted)),
                Span::styled(
                    "Esc",
                    Style::default().fg(theme.text).add_modifier(Modifier::BOLD),
                ),
                Span::styled(" cancel", Style::default().fg(theme.text_muted)),
            ]));
        }
    }
    lines
}
