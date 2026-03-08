use crate::app::{AppState, ModalType};
use crate::state::agent::AgentActivity;
use crate::state::messages::spinner_frame;
use crate::state::voice::VoicePhase;
use ratatui::layout::Rect;
use ratatui::style::{Modifier, Style};
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

fn format_elapsed(secs: u64) -> String {
    if secs >= 3600 {
        format!(
            "{}h {:02}m {:02}s",
            secs / 3600,
            (secs % 3600) / 60,
            secs % 60
        )
    } else if secs >= 60 {
        format!("{}m {:02}s", secs / 60, secs % 60)
    } else {
        format!("{}s", secs)
    }
}

// --- Top bar ---

pub fn render_top(frame: &mut Frame<'_>, area: Rect, state: &AppState) {
    let sep = Span::styled(" \u{2502} ", Style::default().fg(state.theme.border));

    let mut spans = vec![
        Span::styled(
            "AVA",
            Style::default()
                .fg(state.theme.primary)
                .add_modifier(Modifier::BOLD),
        ),
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

    // MCP info
    if state.agent.mcp_server_count > 0 {
        spans.push(sep.clone());
        spans.push(Span::styled(
            format!(
                "MCP: {} svr \u{2502} {} tools",
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
            let bars = (state.voice.amplitude * 25.0).min(5.0) as usize;
            let bar_str: String =
                "\u{2588}".repeat(bars) + &"\u{2591}".repeat(5 - bars);
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

    // Status message (TTL)
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
        spans.push(Span::styled(
            " YOLO",
            Style::default()
                .fg(state.theme.warning)
                .add_modifier(Modifier::BOLD),
        ));
    }

    let left =
        Paragraph::new(Line::from(spans)).style(Style::default().bg(state.theme.bg));
    frame.render_widget(left, area);
}

// --- Context bar (below composer) ---

pub fn render_context_bar(frame: &mut Frame<'_>, area: Rect, state: &AppState) {
    let spinner_tick = state.messages.spinner_tick;

    let mut spans: Vec<Span<'static>> = Vec::new();

    // Show modal-specific hints
    if let Some(modal) = state.active_modal {
        let hints = match modal {
            ModalType::ToolApproval => vec![
                ("a", "approve"),
                ("s", "session"),
                ("r", "reject"),
                ("Esc", "cancel"),
            ],
            _ => vec![
                ("\u{2191}/\u{2193}", "nav"),
                ("Enter", "select"),
                ("Esc", "close"),
            ],
        };
        for (i, (key, desc)) in hints.iter().enumerate() {
            if i > 0 {
                spans.push(Span::styled(
                    "  ",
                    Style::default().fg(state.theme.text_dimmed),
                ));
            }
            spans.push(Span::styled(
                key.to_string(),
                Style::default()
                    .fg(state.theme.text_muted)
                    .add_modifier(Modifier::BOLD),
            ));
            spans.push(Span::styled(
                format!(" {desc}"),
                Style::default().fg(state.theme.text_dimmed),
            ));
        }
    } else if state.agent.is_running {
        // Show spinner + activity + interrupt hint
        let (activity, style) =
            if let AgentActivity::ExecutingTool(ref name) = state.agent.activity {
                let elapsed_str = state
                    .agent
                    .tool_start
                    .map(|start| {
                        let secs = start.elapsed().as_secs();
                        format!(" ({})", format_elapsed(secs))
                    })
                    .unwrap_or_default();
                let is_slow = state
                    .agent
                    .tool_start
                    .map(|start| start.elapsed().as_secs() >= 30)
                    .unwrap_or(false);
                let s = if is_slow {
                    Style::default().fg(state.theme.warning)
                } else {
                    Style::default().fg(state.theme.accent)
                };
                (format!("{name}{elapsed_str}"), s)
            } else {
                (
                    state.agent.activity.to_string(),
                    Style::default().fg(state.theme.accent),
                )
            };

        let frame_char = spinner_frame(spinner_tick);
        spans.push(Span::styled(
            format!("{frame_char} "),
            Style::default().fg(state.theme.accent),
        ));
        spans.push(Span::styled(activity, style));

        // Workflow phase
        if let Some((idx, count, name)) = &state.agent.workflow_phase {
            spans.push(Span::styled(
                format!("  Phase {}/{}: {}", idx + 1, count, name),
                Style::default().fg(state.theme.text_muted),
            ));
        }

        spans.push(Span::styled(
            "  esc interrupt",
            Style::default().fg(state.theme.text_dimmed),
        ));
    } else {
        // Idle — show permission mode
        if state.permission.yolo_mode {
            spans.push(Span::styled(
                "\u{25b8}\u{25b8} ",
                Style::default().fg(state.theme.warning),
            ));
            spans.push(Span::styled(
                "bypass permissions on",
                Style::default()
                    .fg(state.theme.warning)
                    .add_modifier(Modifier::BOLD),
            ));
        }
    }

    let widget =
        Paragraph::new(Line::from(spans)).style(Style::default().bg(state.theme.bg));
    frame.render_widget(widget, area);
}
