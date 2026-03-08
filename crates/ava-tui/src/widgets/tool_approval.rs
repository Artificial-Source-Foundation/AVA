use ava_permissions::tags::RiskLevel;
use crate::state::permission::{ApprovalRequest, ApprovalStage, PermissionState};
use crate::state::theme::Theme;
use ratatui::style::{Color, Modifier, Style};
use ratatui::text::{Line, Span};

fn risk_color(level: RiskLevel) -> Color {
    match level {
        RiskLevel::Safe => Color::Green,
        RiskLevel::Low => Color::Blue,
        RiskLevel::Medium => Color::Yellow,
        RiskLevel::High => Color::Red,
        RiskLevel::Critical => Color::LightRed,
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
    _theme: &Theme,
) -> Vec<Line<'static>> {
    let mut lines = vec![
        Line::from(Span::raw(format!("Tool: {}", request.call.name))),
        Line::from(Span::raw(format!("Args: {}", request.call.arguments))),
    ];

    // Show risk information if available
    if let Some(info) = &request.inspection {
        let color = risk_color(info.risk_level);
        lines.push(Line::from(vec![
            Span::raw("Risk: "),
            Span::styled(
                risk_label(info.risk_level),
                Style::default().fg(color).add_modifier(
                    if info.risk_level >= RiskLevel::High {
                        Modifier::BOLD
                    } else {
                        Modifier::empty()
                    },
                ),
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
                Style::default().fg(Color::DarkGray),
            )));
        }

        for warning in &info.warnings {
            lines.push(Line::from(Span::styled(
                format!("⚠ {warning}"),
                Style::default().fg(Color::Yellow),
            )));
        }
    }

    lines.push(Line::from(Span::raw("")));

    match state.current_stage {
        ApprovalStage::Preview => {
            lines.push(Line::from(Span::raw("Preview: press any stage key")));
        }
        ApprovalStage::ActionSelect => {
            lines.push(Line::from(Span::raw("[a] Allow once")));
            lines.push(Line::from(Span::raw("[s] Allow for session")));
            lines.push(Line::from(Span::raw("[r] Reject")));
            lines.push(Line::from(Span::raw("[y] YOLO mode")));
        }
        ApprovalStage::RejectionReason => {
            lines.push(Line::from(Span::raw("Optional rejection reason:")));
            lines.push(Line::from(Span::raw(state.rejection_input.clone())));
        }
    }
    lines
}
