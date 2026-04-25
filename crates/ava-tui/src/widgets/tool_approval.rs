use crate::state::permission::{ApprovalRequest, ApprovalStage, PermissionState};
use crate::state::theme::Theme;
use crate::widgets::safe_render::clamp_line;
use ava_permissions::tags::RiskLevel;
use ratatui::layout::Rect;
use ratatui::style::{Modifier, Style};
use ratatui::text::{Line, Span};
use ratatui::widgets::{Block, Paragraph};
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

fn key_pill(key: &str, theme: &Theme) -> Span<'static> {
    Span::styled(
        format!(" {key} "),
        Style::default()
            .fg(theme.bg_elevated)
            .bg(theme.text_muted)
            .add_modifier(Modifier::BOLD),
    )
}

/// Render the tool approval as a bottom dock bar (OpenCode-style).
///
/// Layout (4-5 rows inside a bordered block):
///   Line 1: △ Permission required                    `[MEDIUM]`
///   Line 2:   {tool_name}: {full_json_arguments}
///   Line 3: `[a]` Approve  `[s]` Allow session  `[r]` Reject  `[Esc]`
///     — or stage-specific content (rejection reason input, etc.)
pub fn render_tool_approval(
    frame: &mut Frame<'_>,
    area: Rect,
    request: &ApprovalRequest,
    permission: &PermissionState,
    theme: &Theme,
) {
    let block = Block::default().style(Style::default().bg(theme.bg_elevated));
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
        Span::styled("\u{25B3} ", Style::default().fg(theme.text_muted)),
        Span::styled(
            "permission required",
            Style::default().fg(theme.text).add_modifier(Modifier::BOLD),
        ),
    ];

    // Right-aligned risk badge
    if let Some(info) = &request.inspection {
        let label = risk_label(info.risk_level);
        let badge = format!(" {} ", label.to_lowercase());
        let left_len: usize = header_spans
            .iter()
            .map(|s| crate::text_utils::display_width(s.content.as_ref()))
            .sum();
        let badge_len = crate::text_utils::display_width(&badge);
        let gap = w.saturating_sub(left_len + badge_len + (pad as usize) * 2);
        let spaces = " ".repeat(gap);
        header_spans.push(Span::raw(spaces));
        let color = risk_color(info.risk_level, theme);
        header_spans.push(Span::styled(
            badge,
            Style::default().fg(color).add_modifier(Modifier::BOLD),
        ));
    }

    let header_line = clamp_line(Line::from(header_spans), content_w as usize);
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
    let detail_text = request.preview_detail_text();
    let detail_display = crate::text_utils::truncate_display(&detail_text, content_w as usize);

    let detail_line = Line::from(Span::styled(
        detail_display,
        Style::default().fg(theme.text_muted),
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

        let cw = content_w as usize;
        match permission.current_stage {
            ApprovalStage::Preview => {
                let line = clamp_line(
                    Line::from(Span::styled(
                        "Press any key to continue...".to_string(),
                        Style::default().fg(theme.text_muted),
                    )),
                    cw,
                );
                frame.render_widget(Paragraph::new(line), action_area);
            }
            ApprovalStage::ActionSelect => {
                if !request.preview_complete_for_width(cw) {
                    let line = clamp_line(
                        Line::from(Span::styled(
                            "Approval disabled: payload preview is truncated. Reject and retry with a narrower request.".to_string(),
                            Style::default().fg(theme.risk_high),
                        )),
                        cw,
                    );
                    frame.render_widget(Paragraph::new(line), action_area);
                    return;
                }
                let line = clamp_line(
                    Line::from(vec![
                        key_pill("a", theme),
                        Span::styled(
                            " Approve  ".to_string(),
                            Style::default().fg(theme.text_dimmed),
                        ),
                        key_pill("s", theme),
                        Span::styled(
                            " Allow session  ".to_string(),
                            Style::default().fg(theme.text_dimmed),
                        ),
                        key_pill("r", theme),
                        Span::styled(
                            " Reject  ".to_string(),
                            Style::default().fg(theme.text_dimmed),
                        ),
                        key_pill("y", theme),
                        Span::styled(
                            " Auto-approve  ".to_string(),
                            Style::default().fg(theme.text_dimmed),
                        ),
                        key_pill("esc", theme),
                        Span::styled(
                            " Cancel".to_string(),
                            Style::default().fg(theme.text_dimmed),
                        ),
                    ]),
                    cw,
                );
                frame.render_widget(Paragraph::new(line), action_area);
            }
            ApprovalStage::RejectionReason => {
                let line = clamp_line(
                    Line::from(vec![
                        Span::styled(
                            "Reason: ".to_string(),
                            Style::default().fg(theme.text_muted),
                        ),
                        Span::styled(
                            permission.rejection_input.clone(),
                            Style::default().fg(theme.text),
                        ),
                        Span::styled("\u{2588}".to_string(), Style::default().fg(theme.primary)),
                        Span::raw("  ".to_string()),
                        Span::styled(
                            "Enter".to_string(),
                            Style::default().fg(theme.text).add_modifier(Modifier::BOLD),
                        ),
                        Span::styled(
                            " confirm  ".to_string(),
                            Style::default().fg(theme.text_muted),
                        ),
                        Span::styled(
                            "Esc".to_string(),
                            Style::default().fg(theme.text).add_modifier(Modifier::BOLD),
                        ),
                        Span::styled(" cancel".to_string(), Style::default().fg(theme.text_muted)),
                    ]),
                    cw,
                );
                frame.render_widget(Paragraph::new(line), action_area);
            }
        }
    }
}
