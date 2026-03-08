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
        Line::from(vec![
            Span::styled("Tool: ", Style::default().fg(theme.text_muted)),
            Span::styled(
                request.call.name.clone(),
                Style::default().fg(theme.text).add_modifier(Modifier::BOLD),
            ),
        ]),
        Line::from(vec![
            Span::styled("Args: ", Style::default().fg(theme.text_muted)),
            Span::styled(
                request.call.arguments.to_string(),
                Style::default().fg(theme.text),
            ),
        ]),
    ];

    // Show risk information if available
    if let Some(info) = &request.inspection {
        let color = risk_color(info.risk_level, theme);
        let modifier = if info.risk_level >= RiskLevel::High {
            Modifier::BOLD
        } else {
            Modifier::empty()
        };
        lines.push(Line::from(vec![
            Span::styled("Risk: ", Style::default().fg(theme.text_muted)),
            Span::styled(
                risk_label(info.risk_level),
                Style::default().fg(color).add_modifier(modifier),
            ),
        ]));

        if !info.tags.is_empty() {
            let tag_str = info
                .tags
                .iter()
                .map(|t| format!("{t:?}"))
                .collect::<Vec<_>>()
                .join(", ");
            lines.push(Line::from(Span::styled(
                format!("Tags: {tag_str}"),
                Style::default().fg(theme.text_dimmed),
            )));
        }

        for warning in &info.warnings {
            lines.push(Line::from(Span::styled(
                format!("  {warning}"),
                Style::default().fg(theme.warning),
            )));
        }
    }

    lines.push(Line::from(Span::raw("")));

    match state.current_stage {
        ApprovalStage::Preview => {
            lines.push(Line::from(Span::styled(
                "Press any key to continue...",
                Style::default().fg(theme.text_muted),
            )));
        }
        ApprovalStage::ActionSelect => {
            let key_style = Style::default().fg(theme.text);
            let desc_style = Style::default().fg(theme.text_muted);
            lines.push(Line::from(vec![
                Span::styled("[a]", key_style),
                Span::styled(" Allow once  ", desc_style),
                Span::styled("[s]", key_style),
                Span::styled(" Allow for session", desc_style),
            ]));
            lines.push(Line::from(vec![
                Span::styled("[r]", key_style),
                Span::styled(" Reject      ", desc_style),
                Span::styled("[y]", key_style),
                Span::styled(" YOLO mode", desc_style),
            ]));
        }
        ApprovalStage::RejectionReason => {
            lines.push(Line::from(Span::styled(
                "Rejection reason (optional):",
                Style::default().fg(theme.text_muted),
            )));
            lines.push(Line::from(Span::styled(
                state.rejection_input.clone(),
                Style::default().fg(theme.text),
            )));
        }
    }
    lines
}
