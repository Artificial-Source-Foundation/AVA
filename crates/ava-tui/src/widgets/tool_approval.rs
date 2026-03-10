use ava_permissions::tags::RiskLevel;
use crate::state::permission::{ApprovalRequest, ApprovalStage, PermissionState};
use crate::state::theme::Theme;
use ratatui::layout::{Constraint, Direction, Layout, Rect};
use ratatui::style::{Modifier, Style};
use ratatui::text::{Line, Span};
use ratatui::widgets::{Block, Paragraph};
use ratatui::Frame;

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

/// Render the tool approval modal matching the Pencil design.
///
/// Design spec:
///   Modal: 800x400, bg=$bg-elevated, stroke=$border-subtle 1px, clip
///   Header: h=48, bg=$bg-surface, padding=[0,24], justify=space_between
///     Left: ⚠ (warning) + "Tool Approval Required" (bold, 14px)
///     Right: [Esc] (muted)
///   Body: padding=24, gap=20, vertical layout
///     Tool section: "TOOL" label (10px, letterspacing, muted) + tool name (16px, bold, warning)
///     Command section: "COMMAND" label + code box (bg=$bg-deep, stroke=$border-subtle)
///     Risk section: "RISK" label + badge + description
///     Button row: right-aligned — Reject (red outline), Allow session (muted outline), Approve (filled blue)
pub fn render_tool_approval(
    frame: &mut Frame<'_>,
    area: Rect,
    request: &ApprovalRequest,
    permission: &PermissionState,
    theme: &Theme,
) {
    // Fill modal bg
    let bg = Block::default()
        .style(Style::default().bg(theme.bg_elevated))
        .borders(ratatui::widgets::Borders::ALL)
        .border_style(Style::default().fg(theme.border));
    frame.render_widget(bg, area);

    // Split: header (3 rows) + body (rest)
    let header_h = 3u16;
    let chunks = Layout::default()
        .direction(Direction::Vertical)
        .constraints([
            Constraint::Length(header_h),
            Constraint::Min(0),
        ])
        .split(Rect {
            x: area.x + 1,
            y: area.y + 1,
            width: area.width.saturating_sub(2),
            height: area.height.saturating_sub(2),
        });

    let header_area = chunks[0];
    let body_area = chunks[1];

    // --- Header ---
    render_header(frame, header_area, theme);

    // --- Body ---
    render_body(frame, body_area, request, permission, theme);
}

fn render_header(frame: &mut Frame<'_>, area: Rect, theme: &Theme) {
    let bg = Block::default().style(Style::default().bg(theme.bg_surface));
    frame.render_widget(bg, area);

    let pad = 2u16;
    let inner = Rect {
        x: area.x + pad,
        y: area.y,
        width: area.width.saturating_sub(pad * 2),
        height: area.height,
    };

    // Left: ⚠ Tool Approval Required
    let left = Line::from(vec![
        Span::styled(
            "\u{26A0} ",
            Style::default().fg(theme.warning),
        ),
        Span::styled(
            "Tool Approval Required",
            Style::default()
                .fg(theme.text)
                .add_modifier(Modifier::BOLD),
        ),
    ]);

    // Right: [Esc]
    let right_text = "[Esc]";
    let right_width = right_text.len() as u16;

    // Render left text centered vertically
    let text_y = inner.y + (inner.height.saturating_sub(1)) / 2;
    let left_area = Rect { x: inner.x, y: text_y, width: inner.width.saturating_sub(right_width + 1), height: 1 };
    frame.render_widget(Paragraph::new(left), left_area);

    // Render right text
    let right_area = Rect {
        x: inner.x + inner.width.saturating_sub(right_width),
        y: text_y,
        width: right_width,
        height: 1,
    };
    frame.render_widget(
        Paragraph::new(Line::from(Span::styled(
            right_text,
            Style::default().fg(theme.text_muted),
        ))),
        right_area,
    );
}

fn render_body(
    frame: &mut Frame<'_>,
    area: Rect,
    request: &ApprovalRequest,
    permission: &PermissionState,
    theme: &Theme,
) {
    let bg = Block::default().style(Style::default().bg(theme.bg_elevated));
    frame.render_widget(bg, area);

    let pad = 2u16;
    let inner = Rect {
        x: area.x + pad,
        y: area.y + 1,
        width: area.width.saturating_sub(pad * 2),
        height: area.height.saturating_sub(2),
    };

    let mut y = inner.y;
    let w = inner.width;

    // --- TOOL section ---
    // Label
    frame.render_widget(
        Paragraph::new(Line::from(Span::styled(
            "TOOL",
            Style::default()
                .fg(theme.text_muted)
                .add_modifier(Modifier::BOLD),
        ))),
        Rect { x: inner.x, y, width: w, height: 1 },
    );
    y += 1;

    // Tool name (large, bold, warning color)
    frame.render_widget(
        Paragraph::new(Line::from(Span::styled(
            request.call.name.clone(),
            Style::default()
                .fg(theme.warning)
                .add_modifier(Modifier::BOLD),
        ))),
        Rect { x: inner.x, y, width: w, height: 1 },
    );
    y += 2; // gap

    // --- COMMAND section ---
    let args_str = request.call.arguments.to_string();
    if !args_str.is_empty() && args_str != "{}" {
        // Label
        frame.render_widget(
            Paragraph::new(Line::from(Span::styled(
                "COMMAND",
                Style::default()
                    .fg(theme.text_muted)
                    .add_modifier(Modifier::BOLD),
            ))),
            Rect { x: inner.x, y, width: w, height: 1 },
        );
        y += 1;

        // Code box: bg_deep with border, green text
        let cmd_display = if args_str.len() > 200 {
            format!("{}...", &args_str[..197])
        } else {
            args_str
        };
        let cmd_lines = textwrap_simple(&cmd_display, w.saturating_sub(4) as usize);
        let cmd_height = (cmd_lines.len() as u16).min(6).max(1) + 2; // +2 for padding
        let cmd_area = Rect { x: inner.x, y, width: w, height: cmd_height };

        let cmd_bg = Block::default()
            .style(Style::default().bg(theme.bg_deep))
            .borders(ratatui::widgets::Borders::ALL)
            .border_style(Style::default().fg(theme.border));
        frame.render_widget(cmd_bg, cmd_area);

        let cmd_inner = Rect {
            x: cmd_area.x + 1,
            y: cmd_area.y + 1,
            width: cmd_area.width.saturating_sub(2),
            height: cmd_area.height.saturating_sub(2),
        };
        let cmd_paragraph: Vec<Line<'_>> = cmd_lines
            .into_iter()
            .map(|l| Line::from(Span::styled(l, Style::default().fg(theme.success))))
            .collect();
        frame.render_widget(
            Paragraph::new(cmd_paragraph).style(Style::default().bg(theme.bg_deep)),
            cmd_inner,
        );
        y += cmd_height + 1; // gap
    }

    // --- RISK section ---
    if let Some(info) = &request.inspection {
        let color = risk_color(info.risk_level, theme);

        // Label
        frame.render_widget(
            Paragraph::new(Line::from(Span::styled(
                "RISK",
                Style::default()
                    .fg(theme.text_muted)
                    .add_modifier(Modifier::BOLD),
            ))),
            Rect { x: inner.x, y, width: w, height: 1 },
        );
        y += 1;

        // Risk badge + description on same line
        let label = risk_label(info.risk_level);
        let mut risk_spans: Vec<Span<'static>> = vec![
            Span::styled(
                format!(" {label} "),
                Style::default()
                    .fg(color)
                    .add_modifier(Modifier::BOLD),
            ),
        ];

        // Add first warning as description
        if let Some(warning) = info.warnings.first() {
            risk_spans.push(Span::styled(
                format!("  {warning}"),
                Style::default().fg(theme.text_dimmed),
            ));
        }
        frame.render_widget(
            Paragraph::new(Line::from(risk_spans)),
            Rect { x: inner.x, y, width: w, height: 1 },
        );
        y += 1;

        // Additional warnings
        for warning in info.warnings.iter().skip(1) {
            frame.render_widget(
                Paragraph::new(Line::from(Span::styled(
                    format!("  {warning}"),
                    Style::default().fg(theme.warning),
                ))),
                Rect { x: inner.x, y, width: w, height: 1 },
            );
            y += 1;
        }
        y += 1; // gap
    }

    // --- Button row (bottom of body) ---
    let btn_y = (inner.y + inner.height).saturating_sub(1);
    if btn_y > y {
        render_buttons(frame, Rect { x: inner.x, y: btn_y, width: w, height: 1 }, permission, theme);
    }
}

fn render_buttons(
    frame: &mut Frame<'_>,
    area: Rect,
    permission: &PermissionState,
    theme: &Theme,
) {
    match permission.current_stage {
        ApprovalStage::Preview => {
            frame.render_widget(
                Paragraph::new(Line::from(Span::styled(
                    "Press any key to continue...",
                    Style::default().fg(theme.text_muted),
                ))),
                area,
            );
        }
        ApprovalStage::ActionSelect => {
            // Right-aligned button hints
            let buttons = Line::from(vec![
                Span::styled("r", Style::default().fg(theme.error).add_modifier(Modifier::BOLD)),
                Span::styled(" Reject", Style::default().fg(theme.error)),
                Span::raw("  "),
                Span::styled("s", Style::default().fg(theme.text_dimmed).add_modifier(Modifier::BOLD)),
                Span::styled(" Allow session", Style::default().fg(theme.text_dimmed)),
                Span::raw("  "),
                Span::styled("a", Style::default().fg(theme.primary).add_modifier(Modifier::BOLD)),
                Span::styled(" Approve", Style::default().fg(theme.primary).add_modifier(Modifier::BOLD)),
            ]);
            // Right-align by calculating width
            let btn_width: usize = buttons.spans.iter().map(|s| s.content.len()).sum();
            let offset = (area.width as usize).saturating_sub(btn_width);
            let btn_area = Rect {
                x: area.x + offset as u16,
                y: area.y,
                width: btn_width as u16,
                height: 1,
            };
            frame.render_widget(Paragraph::new(buttons), btn_area);
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
                Span::styled("Enter", Style::default().fg(theme.text).add_modifier(Modifier::BOLD)),
                Span::styled(" confirm  ", Style::default().fg(theme.text_muted)),
                Span::styled("Esc", Style::default().fg(theme.text).add_modifier(Modifier::BOLD)),
                Span::styled(" cancel", Style::default().fg(theme.text_muted)),
            ]);
            frame.render_widget(Paragraph::new(line), area);
        }
    }
}

/// Simple text wrapping by character count.
fn textwrap_simple(text: &str, width: usize) -> Vec<String> {
    if width == 0 {
        return vec![text.to_string()];
    }
    let mut lines = Vec::new();
    for line in text.lines() {
        if line.len() <= width {
            lines.push(line.to_string());
        } else {
            let mut remaining = line;
            while remaining.len() > width {
                lines.push(remaining[..width].to_string());
                remaining = &remaining[width..];
            }
            if !remaining.is_empty() {
                lines.push(remaining.to_string());
            }
        }
    }
    if lines.is_empty() {
        lines.push(String::new());
    }
    lines
}
