use crate::state::permission::{ApprovalRequest, ApprovalStage, PermissionState};
use crate::state::theme::Theme;
use ratatui::text::{Line, Span};

pub fn render_tool_approval_lines(
    request: &ApprovalRequest,
    state: &PermissionState,
    _theme: &Theme,
) -> Vec<Line<'static>> {
    let mut lines = vec![
        Line::from(Span::raw(format!("Tool: {}", request.call.name))),
        Line::from(Span::raw(format!("Args: {}", request.call.arguments))),
        Line::from(Span::raw("")),
    ];

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
