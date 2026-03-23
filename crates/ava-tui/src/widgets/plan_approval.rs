use crate::state::plan_approval::{PlanApprovalStage, PlanApprovalState};
use crate::state::theme::Theme;
use crate::widgets::safe_render::clamp_line;
use ratatui::layout::Rect;
use ratatui::style::{Modifier, Style};
use ratatui::text::{Line, Span};
use ratatui::widgets::{Block, Borders, Paragraph};
use ratatui::Frame;

/// Fixed height for the plan approval dock panel.
pub const PLAN_APPROVAL_DOCK_HEIGHT: u16 = 6;

/// Render the plan approval as a bottom dock bar.
///
/// Layout (4-5 rows inside bordered block):
///   Line 1: ◆ Plan Proposed                    [codename]
///   Line 2:   {summary} ({N} steps, ~{M} turns)
///   Line 3:   Steps: 1. {first_step} 2. {second_step} ...
///   Line 4: [e] Execute  [r] Reject  [f] Refine  [Esc] Cancel
pub fn render_plan_approval(
    frame: &mut Frame<'_>,
    area: Rect,
    state: &PlanApprovalState,
    theme: &Theme,
) {
    let block = Block::default()
        .style(Style::default().bg(theme.bg_elevated))
        .borders(Borders::ALL)
        .border_style(Style::default().fg(theme.accent));
    let inner = block.inner(area);
    frame.render_widget(block, area);

    if inner.height == 0 || inner.width < 10 {
        return;
    }

    let pad = 1u16;
    let content_x = inner.x + pad;
    let content_w = inner.width.saturating_sub(pad * 2);
    let cw = content_w as usize;

    // --- Line 1: header with codename badge ---
    let codename_display = state.plan.codename.as_deref().unwrap_or("Plan");
    let mut header_spans: Vec<Span<'_>> = vec![
        Span::styled(
            "\u{25C6} ",
            Style::default()
                .fg(theme.accent)
                .add_modifier(Modifier::BOLD),
        ),
        Span::styled(
            "Plan Proposed",
            Style::default().fg(theme.text).add_modifier(Modifier::BOLD),
        ),
    ];
    let badge = format!("[{}]", codename_display);
    let left_len: usize = header_spans
        .iter()
        .map(|s| crate::text_utils::display_width(s.content.as_ref()))
        .sum();
    let badge_len = crate::text_utils::display_width(&badge);
    let gap = cw.saturating_sub(left_len + badge_len);
    header_spans.push(Span::raw(" ".repeat(gap)));
    header_spans.push(Span::styled(
        badge,
        Style::default()
            .fg(theme.accent)
            .add_modifier(Modifier::BOLD),
    ));

    let header_line = clamp_line(Line::from(header_spans), cw);
    frame.render_widget(
        Paragraph::new(header_line),
        Rect {
            x: content_x,
            y: inner.y,
            width: content_w,
            height: 1,
        },
    );

    // --- Line 2: summary + step count ---
    if inner.height > 1 {
        let step_count = state.plan.steps.len();
        let turns_info = state
            .plan
            .estimated_turns
            .map(|t| format!(", ~{t} turns"))
            .unwrap_or_default();
        let summary_text = format!(
            "  {} ({} step{}{})",
            state.plan.summary,
            step_count,
            if step_count != 1 { "s" } else { "" },
            turns_info
        );
        let summary_display = crate::text_utils::truncate_display(&summary_text, cw);
        frame.render_widget(
            Paragraph::new(Line::from(Span::styled(
                summary_display,
                Style::default().fg(theme.text_muted),
            ))),
            Rect {
                x: content_x,
                y: inner.y + 1,
                width: content_w,
                height: 1,
            },
        );
    }

    // --- Line 3: step preview ---
    if inner.height > 2 {
        let steps_preview: String = state
            .plan
            .steps
            .iter()
            .enumerate()
            .take(4)
            .map(|(i, s)| {
                format!(
                    "{}. {}",
                    i + 1,
                    crate::text_utils::truncate_display(&s.description, 30)
                )
            })
            .collect::<Vec<_>>()
            .join("  ");
        let steps_text = if state.plan.steps.len() > 4 {
            format!("  {} (+{} more)", steps_preview, state.plan.steps.len() - 4)
        } else {
            format!("  {}", steps_preview)
        };
        let steps_display = crate::text_utils::truncate_display(&steps_text, cw);
        frame.render_widget(
            Paragraph::new(Line::from(Span::styled(
                steps_display,
                Style::default().fg(theme.text_dimmed),
            ))),
            Rect {
                x: content_x,
                y: inner.y + 2,
                width: content_w,
                height: 1,
            },
        );
    }

    // --- Line 4: action hints or feedback input ---
    if inner.height > 3 {
        let action_area = Rect {
            x: content_x,
            y: inner.y + 3,
            width: content_w,
            height: 1,
        };

        match state.stage {
            PlanApprovalStage::ActionSelect => {
                let line = clamp_line(
                    Line::from(vec![
                        Span::styled(
                            "[e]",
                            Style::default()
                                .fg(theme.primary)
                                .add_modifier(Modifier::BOLD),
                        ),
                        Span::styled(" Execute  ", Style::default().fg(theme.text_dimmed)),
                        Span::styled(
                            "[r]",
                            Style::default()
                                .fg(theme.error)
                                .add_modifier(Modifier::BOLD),
                        ),
                        Span::styled(" Reject  ", Style::default().fg(theme.text_dimmed)),
                        Span::styled(
                            "[f]",
                            Style::default()
                                .fg(theme.text_muted)
                                .add_modifier(Modifier::BOLD),
                        ),
                        Span::styled(" Refine  ", Style::default().fg(theme.text_dimmed)),
                        Span::styled(
                            "[Esc]",
                            Style::default()
                                .fg(theme.text_muted)
                                .add_modifier(Modifier::BOLD),
                        ),
                        Span::styled(" Cancel", Style::default().fg(theme.text_dimmed)),
                    ]),
                    cw,
                );
                frame.render_widget(Paragraph::new(line), action_area);
            }
            PlanApprovalStage::RejectionFeedback => {
                let line = clamp_line(
                    Line::from(vec![
                        Span::styled("Feedback: ", Style::default().fg(theme.text_muted)),
                        Span::styled(
                            state.feedback_input.clone(),
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
                    ]),
                    cw,
                );
                frame.render_widget(Paragraph::new(line), action_area);
            }
        }
    }
}
