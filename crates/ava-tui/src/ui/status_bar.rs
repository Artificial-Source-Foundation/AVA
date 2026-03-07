use crate::app::AppState;
use ratatui::layout::Rect;
use ratatui::style::Style;
use ratatui::text::{Line, Span};
use ratatui::widgets::Paragraph;
use ratatui::Frame;

pub fn render_top(frame: &mut Frame<'_>, area: Rect, state: &AppState) {
    let yolo_indicator = if state.permission.yolo_mode {
        Span::styled(" YOLO ", Style::default().fg(state.theme.warning))
    } else {
        Span::raw("")
    };

    let line = Line::from(vec![
        Span::styled("AVA", Style::default().fg(state.theme.primary)),
        Span::styled(" | ", Style::default().fg(state.theme.border)),
        Span::styled(
            format!("{}/{}", state.agent.provider_name, state.agent.model_name),
            Style::default().fg(state.theme.text),
        ),
        Span::styled(" | ", Style::default().fg(state.theme.border)),
        Span::styled(
            format!(
                "tokens: {}in/{}out",
                state.agent.tokens_used.input, state.agent.tokens_used.output
            ),
            Style::default().fg(state.theme.text_muted),
        ),
        yolo_indicator,
    ]);

    let widget = Paragraph::new(line);
    frame.render_widget(widget, area);
}

pub fn render_bottom(frame: &mut Frame<'_>, area: Rect, state: &AppState) {
    let activity = state.agent.activity.to_string();
    let activity_style = if state.agent.is_running {
        Style::default().fg(state.theme.accent)
    } else {
        Style::default().fg(state.theme.text_muted)
    };

    let line = Line::from(vec![
        Span::styled(
            format!("turn {}/{}", state.agent.current_turn, state.agent.max_turns),
            Style::default().fg(state.theme.text_muted),
        ),
        Span::styled(" | ", Style::default().fg(state.theme.border)),
        Span::styled(activity, activity_style),
        Span::styled(" | ", Style::default().fg(state.theme.border)),
        Span::styled(
            format!("{} messages", state.messages.messages.len()),
            Style::default().fg(state.theme.text_muted),
        ),
        Span::styled(" | ", Style::default().fg(state.theme.border)),
        Span::styled(
            "Ctrl+/ palette  Ctrl+D quit",
            Style::default().fg(state.theme.text_muted),
        ),
    ]);

    let widget = Paragraph::new(line);
    frame.render_widget(widget, area);
}
