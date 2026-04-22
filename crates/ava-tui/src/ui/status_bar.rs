use crate::app::{AppState, ModalType, ViewMode};
use crate::state::agent::AgentActivity;
use crate::state::messages::spinner_frame;
use crate::state::voice::VoicePhase;
use crate::ui::layout::content_margin;
use crate::widgets::safe_render::{clamp_line, to_static_line};
use ratatui::layout::Rect;
use ratatui::style::{Modifier, Style};
use ratatui::text::{Line, Span};
use ratatui::widgets::Block;
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

/// Design: horizontal padding 20px → 2 chars
const H_PAD: &str = "  ";
/// Design: gap between hint items 16px → 2 chars
const ITEM_GAP: &str = "  ";

fn inset_area(area: Rect) -> Rect {
    let margin = content_margin(area.width);
    Rect {
        x: area.x.saturating_add(margin),
        y: area.y,
        width: area.width.saturating_sub(margin * 2),
        height: area.height,
    }
}

pub fn should_render_context_bar(state: &AppState) -> bool {
    state.active_modal.is_some()
        || matches!(
            state.view_mode,
            ViewMode::SubAgent { .. } | ViewMode::BackgroundTask { .. }
        )
}

// --- Top bar ---
// Design: quiet one-line shell identity.

pub fn render_top(frame: &mut Frame<'_>, area: Rect, state: &AppState) {
    let area = inset_area(area);
    if area.width == 0 || area.height == 0 {
        return;
    }
    frame.render_widget(ratatui::widgets::Clear, area);
    let sep = Span::styled("  ", Style::default());

    let mut left_spans = vec![
        Span::raw(H_PAD),
        Span::styled(
            "AVA",
            Style::default()
                .fg(state.theme.text)
                .add_modifier(Modifier::BOLD),
        ),
    ];

    // Session ID
    if let Some(ref session) = state.session.current_session {
        left_spans.push(sep.clone());
        let short_id = session.id.to_string();
        let display = if short_id.len() > 8 {
            &short_id[..8]
        } else {
            &short_id
        };
        left_spans.push(Span::styled(
            format!("[{display}]"),
            Style::default().fg(state.theme.text_muted),
        ));
    }

    // Voice recording indicator
    match state.voice.phase {
        VoicePhase::Recording => {
            let elapsed = state.voice.recording_duration();
            left_spans.push(sep.clone());
            left_spans.push(Span::styled(
                format!("REC {elapsed:.1}s"),
                Style::default().fg(state.theme.error),
            ));
            let bars = (state.voice.amplitude * 25.0).min(5.0) as usize;
            let bar_str: String = "\u{2588}".repeat(bars) + &"\u{2591}".repeat(5 - bars);
            left_spans.push(Span::styled(
                format!(" {bar_str}"),
                Style::default().fg(state.theme.accent),
            ));
        }
        VoicePhase::Transcribing => {
            left_spans.push(sep.clone());
            left_spans.push(Span::styled(
                "Transcribing...",
                Style::default().fg(state.theme.accent),
            ));
        }
        VoicePhase::Idle => {}
    }

    // BTW branch indicator
    if state.btw.active {
        left_spans.push(Span::styled(
            " [btw]",
            Style::default()
                .fg(state.theme.accent)
                .add_modifier(Modifier::BOLD),
        ));
    }

    // Background task indicator
    {
        let bg = state.background.lock().unwrap_or_else(|e| e.into_inner());
        let running = bg.running_count();
        if running > 0 {
            left_spans.push(Span::styled(
                format!(" [BG: {running} running]"),
                Style::default()
                    .fg(state.theme.accent)
                    .add_modifier(Modifier::BOLD),
            ));
        } else if !bg.tasks.is_empty() {
            let total = bg.tasks.len();
            left_spans.push(Span::styled(
                format!(" [BG: {total} tasks]"),
                Style::default().fg(state.theme.text_dimmed),
            ));
        }
    }

    // Sub-agent indicator
    {
        let total = state.agent.sub_agents.len();
        if total > 0 {
            let running = state
                .agent
                .sub_agents
                .iter()
                .filter(|sa| sa.is_running)
                .count();
            if running > 0 {
                left_spans.push(Span::styled(
                    format!(" [SA: {running} running]"),
                    Style::default()
                        .fg(state.theme.warning)
                        .add_modifier(Modifier::BOLD),
                ));
            } else {
                left_spans.push(Span::styled(
                    format!(" [SA: {total}]"),
                    Style::default().fg(state.theme.text_dimmed),
                ));
            }
        }
    }

    let mut right_spans: Vec<Span<'_>> = Vec::new();

    // Calculate widths and fill remaining space
    let left_width: usize = left_spans
        .iter()
        .map(|s| crate::text_utils::span_display_width(s))
        .sum();
    let right_width: usize = right_spans
        .iter()
        .map(|s| crate::text_utils::span_display_width(s))
        .sum::<usize>()
        + H_PAD.len();
    let gap = (area.width as usize).saturating_sub(left_width + right_width);

    let mut all_spans = left_spans;
    if gap > 0 {
        all_spans.push(Span::raw(" ".repeat(gap)));
    }
    all_spans.append(&mut right_spans);
    all_spans.push(Span::raw(H_PAD));

    // Clamp the final line to the area width
    let final_line = clamp_line(to_static_line(Line::from(all_spans)), area.width as usize);

    // Render a single quiet row with no explicit separator.
    let bg = ratatui::widgets::Block::default().style(Style::default().bg(state.theme.bg_surface));
    frame.render_widget(bg, area);
    let widget = Paragraph::new(final_line);
    frame.render_widget(widget, area);
}

// --- Context bar (below composer) ---
// Design: height=28, bg=#131720, padding=[0,20], justify=space_between
// Left: keyboard hints (key bold + desc dimmed, gap=16)
// Right: tokens + cost + model badge (gap=16)

pub fn render_context_bar(frame: &mut Frame<'_>, area: Rect, state: &AppState) {
    let area = inset_area(area);
    if area.width == 0 || area.height == 0 {
        return;
    }
    frame.render_widget(ratatui::widgets::Clear, area);
    let spinner_tick = state.messages.spinner_tick;

    let mut left_spans: Vec<Span<'static>> = vec![Span::raw(H_PAD)];

    // Show modal-specific hints
    if let Some(modal) = state.active_modal {
        let hints = match modal {
            ModalType::ToolApproval => vec![
                ("a", "approve"),
                ("s", "session"),
                ("r", "reject"),
                ("Esc", "cancel"),
            ],
            ModalType::InfoPanel => vec![("\u{2191}/\u{2193}", "scroll"), ("Esc", "close")],
            _ => vec![
                ("\u{2191}/\u{2193}", "nav"),
                ("Enter", "select"),
                ("Esc", "close"),
            ],
        };
        render_hints(&mut left_spans, &hints, state);
    } else if matches!(
        state.view_mode,
        ViewMode::SubAgent { .. } | ViewMode::BackgroundTask { .. }
    ) {
        // Sub-agent or background task view hints take precedence over the
        // parent run strip so child transcript views stay read-oriented.
        let mut hints: Vec<(&str, &str)> = vec![("Esc", "back")];
        if let ViewMode::SubAgent { .. } = state.view_mode {
            if state.agent.sub_agents.len() > 1 {
                hints.push(("←/→", "sibling"));
            }
            hints.push(("PgUp/PgDn", "scroll"));
            hints.push(("ro", "read-only transcript"));
        } else {
            hints.push(("PgUp/PgDn", "scroll"));
            hints.push(("ro", "read-only transcript"));
        }
        render_hints(&mut left_spans, &hints, state);
    } else if state.agent.is_running {
        // Spinner + activity + a single interrupt cue
        let (activity, style) = if let AgentActivity::ExecutingTool(ref name) = state.agent.activity
        {
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
            (format!("running {name}...{elapsed_str}"), s)
        } else {
            (
                state.agent.activity.to_string(),
                Style::default().fg(state.theme.accent),
            )
        };

        // Equalizer bars: 3 chars wide + 1 space = 4 chars total, fixed width
        let frame = spinner_frame(spinner_tick);
        left_spans.push(Span::styled(
            format!("{frame} "),
            Style::default().fg(state.theme.accent),
        ));
        let activity_style = match state.agent.activity {
            AgentActivity::ExecutingTool(_) => style,
            _ => Style::default().fg(state.theme.text_dimmed),
        };
        left_spans.push(Span::styled(activity, activity_style));

        // Workflow phase
        if let Some((idx, count, name)) = &state.agent.workflow_phase {
            left_spans.push(Span::styled(
                format!("{ITEM_GAP}{}/{} {}", idx + 1, count, name),
                Style::default().fg(state.theme.text_muted),
            ));
        }

        // Queue count indicator
        let queue_count = state.input.queue_display.total_count();
        if queue_count > 0 {
            left_spans.push(Span::raw(ITEM_GAP));
            left_spans.push(Span::styled(
                format!("{queue_count} queued"),
                Style::default().fg(state.theme.text_muted),
            ));
        }

        left_spans.push(Span::raw(ITEM_GAP));
        left_spans.push(Span::styled(
            "esc interrupt",
            Style::default().fg(state.theme.text_dimmed),
        ));
    } else {
        return;
    }

    // Right side: keep the running/status strip light.
    let right_spans: Vec<Span<'static>> = if matches!(
        state.view_mode,
        ViewMode::SubAgent { .. } | ViewMode::BackgroundTask { .. }
    ) {
        Vec::new()
    } else {
        vec![Span::styled(
            state.agent.activity.to_string(),
            Style::default().fg(state.theme.text_dimmed),
        )]
    };

    // Fill gap between left and right
    let left_width: usize = left_spans
        .iter()
        .map(|s| crate::text_utils::span_display_width(s))
        .sum();
    let right_width: usize = right_spans
        .iter()
        .map(|s| crate::text_utils::span_display_width(s))
        .sum::<usize>()
        + H_PAD.len();
    let gap = (area.width as usize).saturating_sub(left_width + right_width);

    let mut all_spans = left_spans;
    if gap > 0 {
        all_spans.push(Span::raw(" ".repeat(gap)));
    }
    all_spans.extend(right_spans);
    all_spans.push(Span::raw(H_PAD));

    // Clamp the final line to the area width
    let final_line = clamp_line(Line::from(all_spans), area.width as usize);

    // Single quiet footer row with no extra rule.
    let bg = Block::default().style(Style::default().bg(state.theme.bg_surface));
    frame.render_widget(bg, area);
    let widget = Paragraph::new(final_line);
    frame.render_widget(widget, area);
}

/// Render a list of (key, description) hint pairs with consistent styling.
fn render_hints(spans: &mut Vec<Span<'static>>, hints: &[(&str, &str)], state: &AppState) {
    for (i, (key, desc)) in hints.iter().enumerate() {
        if i > 0 {
            spans.push(Span::raw(ITEM_GAP));
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
}
