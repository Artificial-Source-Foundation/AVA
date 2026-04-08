/// Equalizer-bar spinner frames — three bars that rise and fall like an audio
/// visualizer.  Each frame is exactly 3 characters wide (no jitter).
/// Used ONLY in the status bar (`status_bar.rs`).
pub const SPINNER_FRAMES: &[&str] = &["▁▃▇", "▃▇▅", "▇▅▂", "▅▂▃", "▂▃▇", "▃▇▃", "▇▃▁", "▃▁▃"];

/// Divisor to slow spinner animation. At 16ms ticks, each frame lasts ~128ms
/// giving a smooth ~1s full cycle.
const SPINNER_FRAME_DIVISOR: usize = 8;

/// Returns the current equalizer spinner frame (always exactly 3 columns wide).
/// Prefer `inline_spinner_frame()` for use inside the chat/message area.
pub fn spinner_frame(tick: usize) -> &'static str {
    SPINNER_FRAMES[(tick / SPINNER_FRAME_DIVISOR) % SPINNER_FRAMES.len()]
}

/// Compact single-character spinner for inline use in chat (tool activity,
/// sub-agent indicators, streaming indicators).  Produces a smooth rotating
/// arc that takes up only 1 column.
pub const INLINE_SPINNER_FRAMES: &[&str] = &["⠋", "⠙", "⠹", "⠸", "⠼", "⠴", "⠦", "⠧", "⠇", "⠏"];

pub fn inline_spinner_frame(tick: usize) -> &'static str {
    INLINE_SPINNER_FRAMES[(tick / SPINNER_FRAME_DIVISOR) % INLINE_SPINNER_FRAMES.len()]
}
