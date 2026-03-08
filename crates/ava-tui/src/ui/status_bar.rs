use crate::app::{AppState, ModalType};
use crate::state::agent::AgentActivity;
use crate::state::voice::VoicePhase;
use ratatui::layout::Rect;
use ratatui::style::Style;
use ratatui::text::{Line, Span};
use ratatui::widgets::Paragraph;
use ratatui::Frame;
use std::time::Instant;

// --- TTL Status Messages ---

#[derive(Debug, Clone)]
pub struct StatusMessage {
    pub text: String,
    pub level: StatusLevel,
    pub expires_at: Instant,
}

#[derive(Debug, Clone, Copy)]
pub enum StatusLevel {
    Info,
    Warn,
    Error,
}

impl StatusLevel {
    pub fn default_ttl_secs(self) -> u64 {
        match self {
            Self::Info => 3,
            Self::Warn => 4,
            Self::Error => 5,
        }
    }
}

impl StatusMessage {
    pub fn new(text: impl Into<String>, level: StatusLevel) -> Self {
        let ttl = std::time::Duration::from_secs(level.default_ttl_secs());
        Self {
            text: text.into(),
            level,
            expires_at: Instant::now() + ttl,
        }
    }

    pub fn is_expired(&self) -> bool {
        Instant::now() >= self.expires_at
    }
}

// --- Token formatting ---

fn format_tokens(n: usize) -> String {
    if n >= 1_000_000 {
        format!("{:.1}M", n as f64 / 1_000_000.0)
    } else if n >= 1_000 {
        format!("{:.1}K", n as f64 / 1_000.0)
    } else {
        n.to_string()
    }
}

// --- Top bar ---

pub fn render_top(frame: &mut Frame<'_>, area: Rect, state: &AppState) {
    let sep = Span::styled(" | ", Style::default().fg(state.theme.border));

    let mut spans = vec![
        Span::styled("AVA", Style::default().fg(state.theme.primary)),
        sep.clone(),
        Span::styled(
            &state.agent.model_name,
            Style::default().fg(state.theme.text),
        ),
        sep.clone(),
        Span::styled(
            format!(
                "{}/{}",
                format_tokens(state.agent.tokens_used.input),
                format_tokens(state.agent.tokens_used.output),
            ),
            Style::default().fg(state.theme.text_muted),
        ),
    ];

    if state.agent.cost > 0.0 {
        spans.push(sep.clone());
        spans.push(Span::styled(
            format!("${:.2}", state.agent.cost),
            Style::default().fg(state.theme.text_muted),
        ));
    }

    // MCP info (if any servers connected)
    if state.agent.mcp_server_count > 0 {
        spans.push(sep.clone());
        spans.push(Span::styled(
            format!(
                "MCP: {} svr | {} tools",
                state.agent.mcp_server_count, state.agent.mcp_tool_count
            ),
            Style::default().fg(state.theme.text_muted),
        ));
    }

    // Voice recording indicator
    match state.voice.phase {
        VoicePhase::Recording => {
            let elapsed = state.voice.recording_duration();
            spans.push(sep.clone());
            spans.push(Span::styled(
                format!("REC {elapsed:.1}s"),
                Style::default().fg(state.theme.error),
            ));
            // Amplitude bar (5 chars wide)
            let bars = (state.voice.amplitude * 25.0).min(5.0) as usize;
            let bar_str: String = "\u{2588}".repeat(bars) + &"\u{2591}".repeat(5 - bars);
            spans.push(Span::styled(
                format!(" {bar_str}"),
                Style::default().fg(state.theme.accent),
            ));
        }
        VoicePhase::Transcribing => {
            spans.push(sep.clone());
            spans.push(Span::styled(
                "Transcribing...",
                Style::default().fg(state.theme.accent),
            ));
        }
        VoicePhase::Idle => {}
    }

    // Status message (TTL) or YOLO badge
    if let Some(ref msg) = state.status_message {
        let color = match msg.level {
            StatusLevel::Info => state.theme.text_muted,
            StatusLevel::Warn => state.theme.warning,
            StatusLevel::Error => state.theme.error,
        };
        spans.push(sep.clone());
        spans.push(Span::styled(&msg.text, Style::default().fg(color)));
    }

    if state.permission.yolo_mode {
        spans.push(Span::styled(" YOLO", Style::default().fg(state.theme.warning)));
    }

    let left = Paragraph::new(Line::from(spans));
    frame.render_widget(left, area);
}

// --- Bottom bar ---

pub fn render_bottom(frame: &mut Frame<'_>, area: Rect, state: &AppState) {
    let sep = Span::styled(" | ", Style::default().fg(state.theme.border));

    // Build activity string with elapsed time for tool execution
    let (activity, activity_style) = if let AgentActivity::ExecutingTool(ref name) = state.agent.activity {
        let elapsed_str = state
            .agent
            .tool_start
            .map(|start| {
                let elapsed = start.elapsed().as_secs_f64();
                format!(" ({elapsed:.1}s)")
            })
            .unwrap_or_default();
        let is_slow = state
            .agent
            .tool_start
            .map(|start| start.elapsed().as_secs() >= 30)
            .unwrap_or(false);
        let style = if is_slow {
            Style::default().fg(state.theme.warning)
        } else {
            Style::default().fg(state.theme.accent)
        };
        (format!("\u{27F3} {name}{elapsed_str}"), style)
    } else if state.agent.is_running {
        (state.agent.activity.to_string(), Style::default().fg(state.theme.accent))
    } else {
        (state.agent.activity.to_string(), Style::default().fg(state.theme.text_muted))
    };

    // Workflow phase/iteration info
    let workflow_info: Option<String> = state.agent.workflow_phase.as_ref().map(|(idx, count, name)| {
        let phase = format!("Phase {}/{}: {}", idx + 1, count, name);
        if let Some((iter, max_iter)) = &state.agent.workflow_iteration {
            format!("{} | Iter {}/{}", phase, iter, max_iter)
        } else {
            phase
        }
    });

    // Left side: turn + activity + session
    let session_id = state
        .session
        .current_session
        .as_ref()
        .map(|s| s.id.to_string()[..8].to_string())
        .unwrap_or_else(|| "none".to_string());

    let mut left_spans = vec![
        Span::styled(
            format!("turn {}/{}", state.agent.current_turn, state.agent.max_turns),
            Style::default().fg(state.theme.text_muted),
        ),
        sep.clone(),
        Span::styled(activity, activity_style),
        sep.clone(),
        Span::styled(
            format!("session: {session_id}"),
            Style::default().fg(state.theme.text_muted),
        ),
        sep.clone(),
    ];

    if let Some(ref wf) = workflow_info {
        left_spans.push(Span::styled(
            wf.clone(),
            Style::default().fg(state.theme.accent),
        ));
        left_spans.push(sep.clone());
    }

    // Right side: context-sensitive hints
    let hint_spans = build_hints(state);

    let mut all_spans = left_spans;
    all_spans.extend(hint_spans);

    let widget = Paragraph::new(Line::from(all_spans));
    frame.render_widget(widget, area);
}

// --- Context-sensitive hints ---

fn build_hints(state: &AppState) -> Vec<Span<'static>> {
    match state.active_modal {
        Some(ModalType::ToolApproval) => hint_line(&[
            ("a", "approve"),
            ("s", "session"),
            ("r", "reject"),
            ("y", "yolo"),
            ("Esc", "cancel"),
        ], state),
        Some(ModalType::CommandPalette) => hint_line(&[
            ("\u{2191}/\u{2193}", "navigate"),
            ("Enter", "select"),
            ("Esc", "close"),
        ], state),
        Some(ModalType::SessionList) => hint_line(&[
            ("\u{2191}/\u{2193}", "navigate"),
            ("Enter", "switch"),
            ("Esc", "close"),
        ], state),
        Some(ModalType::ModelSelector) | Some(ModalType::ToolList) => hint_line(&[
            ("\u{2191}/\u{2193}", "navigate"),
            ("Enter", "select"),
            ("Esc", "close"),
        ], state),
        None if state.agent.is_running => hint_line(&[
            ("Ctrl+C", "cancel"),
            ("Ctrl+D", "quit"),
        ], state),
        None => hint_line(&[
            ("Enter", "send"),
            ("Ctrl+/", "cmds"),
            ("Ctrl+M", "model"),
            ("Ctrl+V", "voice"),
            ("Ctrl+D", "quit"),
        ], state),
    }
}

fn hint_line(hints: &[(&str, &str)], state: &AppState) -> Vec<Span<'static>> {
    let mut spans = Vec::new();
    for (i, (key, desc)) in hints.iter().enumerate() {
        if i > 0 {
            spans.push(Span::styled("  ", Style::default().fg(state.theme.border)));
        }
        spans.push(Span::styled(
            key.to_string(),
            Style::default().fg(state.theme.text),
        ));
        spans.push(Span::styled(
            format!(" {desc}"),
            Style::default().fg(state.theme.text_muted),
        ));
    }
    spans
}
