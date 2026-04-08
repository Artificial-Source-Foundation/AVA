use std::time::{Duration, Instant};

/// Byte-length threshold above which the buffer is considered backlogged.
///
/// When pending content exceeds this, the flush interval is shortened to
/// allow faster catch-up without changing the normal-cadence feel.
const BACKLOG_THRESHOLD: usize = 512;

/// Minimum flush interval during backlog catch-up (≈120 fps ceiling).
/// Keeps a floor so we never flush *every* push, which would regress to
/// one-render-per-token flicker.
const BACKLOG_MIN_INTERVAL: Duration = Duration::from_millis(8);

/// Accumulates streaming tokens between frame renders to reduce flicker.
///
/// Tokens are buffered and only flushed when the frame interval has elapsed,
/// batching multiple rapid token arrivals into a single render update.
///
/// # Adaptive flushing
///
/// Under normal streaming the buffer respects the configured frame interval
/// (e.g. 16 ms at 60 fps). When pending content exceeds `BACKLOG_THRESHOLD`
/// bytes the interval is shortened proportionally — the larger the backlog,
/// the more aggressively we flush — bottoming out at `BACKLOG_MIN_INTERVAL`.
/// This lets the UI catch up during token bursts while preserving the smooth
/// cadence during steady-state streaming.
pub struct TokenBuffer {
    pending: String,
    last_flush: Instant,
    frame_interval: Duration,
    /// Lightweight metrics for diagnostics (non-zero-cost only when read).
    metrics: FlushMetrics,
}

/// Diagnostic counters exposed for tracing / tests.
///
/// All fields are cumulative over the buffer's lifetime.
#[derive(Debug, Clone, Default, PartialEq, Eq)]
pub struct FlushMetrics {
    /// Total number of `flush()` calls that returned content.
    pub flush_count: u64,
    /// Number of those flushes that triggered under backlog (early).
    pub backlog_flush_count: u64,
    /// Total bytes flushed.
    pub bytes_flushed: u64,
    /// High-water mark for pending buffer length in bytes.
    pub peak_pending_bytes: usize,
}

impl TokenBuffer {
    pub fn new(target_fps: u32) -> Self {
        let fps = target_fps.max(1);
        Self {
            pending: String::new(),
            last_flush: Instant::now(),
            frame_interval: Duration::from_millis(1000 / fps as u64),
            metrics: FlushMetrics::default(),
        }
    }

    /// Add a token chunk to the buffer.
    pub fn push(&mut self, chunk: &str) {
        self.pending.push_str(chunk);
        let len = self.pending.len();
        if len > self.metrics.peak_pending_bytes {
            self.metrics.peak_pending_bytes = len;
        }
    }

    /// Check if it's time to flush — respects adaptive interval.
    pub fn should_flush(&self) -> bool {
        !self.pending.is_empty() && self.last_flush.elapsed() >= self.effective_interval()
    }

    /// Take all buffered content if the (adaptive) frame interval has elapsed.
    pub fn flush(&mut self) -> Option<String> {
        if !self.should_flush() {
            return None;
        }
        let is_backlog = self.pending.len() >= BACKLOG_THRESHOLD;
        self.last_flush = Instant::now();
        let content = std::mem::take(&mut self.pending);
        self.metrics.flush_count += 1;
        self.metrics.bytes_flushed += content.len() as u64;
        if is_backlog {
            self.metrics.backlog_flush_count += 1;
        }
        Some(content)
    }

    /// Force flush regardless of timing (e.g., on stream end).
    pub fn force_flush(&mut self) -> Option<String> {
        if self.pending.is_empty() {
            return None;
        }
        self.last_flush = Instant::now();
        let content = std::mem::take(&mut self.pending);
        self.metrics.flush_count += 1;
        self.metrics.bytes_flushed += content.len() as u64;
        Some(content)
    }

    /// Whether the buffer has pending content.
    pub fn has_pending(&self) -> bool {
        !self.pending.is_empty()
    }

    /// Current pending buffer length in bytes.
    pub fn pending_len(&self) -> usize {
        self.pending.len()
    }

    /// Snapshot of cumulative flush metrics (cheap copy).
    pub fn metrics(&self) -> &FlushMetrics {
        &self.metrics
    }

    // ── private ────────────────────────────────────────────────────

    /// Compute the effective flush interval, shortened under backlog.
    ///
    /// When `pending.len() < BACKLOG_THRESHOLD` → normal `frame_interval`.
    /// Above threshold the interval shrinks linearly with backlog size,
    /// clamped to [`BACKLOG_MIN_INTERVAL`].
    fn effective_interval(&self) -> Duration {
        let len = self.pending.len();
        if len < BACKLOG_THRESHOLD {
            return self.frame_interval;
        }
        // ratio in 1..=∞  (how many times over threshold)
        // At 1× threshold → frame_interval / 2
        // At 2× threshold → frame_interval / 3, etc.
        let ratio = len / BACKLOG_THRESHOLD; // ≥1
        let divisor = (ratio + 1) as u32; // ≥2
        let shortened = self.frame_interval / divisor;
        shortened.max(BACKLOG_MIN_INTERVAL)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::thread;

    // ── original tests (preserved) ────────────────────────────────

    #[test]
    fn push_accumulates_tokens() {
        let mut buf = TokenBuffer::new(60);
        buf.push("hello");
        buf.push(" world");
        assert!(buf.has_pending());
    }

    #[test]
    fn flush_returns_none_when_empty() {
        let mut buf = TokenBuffer::new(60);
        assert!(buf.flush().is_none());
    }

    #[test]
    fn force_flush_returns_all_content() {
        let mut buf = TokenBuffer::new(60);
        buf.push("a");
        buf.push("b");
        buf.push("c");
        let content = buf.force_flush().unwrap();
        assert_eq!(content, "abc");
        assert!(!buf.has_pending());
    }

    #[test]
    fn force_flush_empty_returns_none() {
        let mut buf = TokenBuffer::new(60);
        assert!(buf.force_flush().is_none());
    }

    #[test]
    fn flush_after_interval_returns_content() {
        let mut buf = TokenBuffer::new(1000); // 1fps = 1000ms interval
        buf.push("fast");
        // Won't flush immediately
        assert!(buf.flush().is_none());

        // Wait for interval
        let mut buf = TokenBuffer::new(60);
        buf.push("data");
        thread::sleep(Duration::from_millis(20));
        let content = buf.flush();
        assert_eq!(content.as_deref(), Some("data"));
    }

    #[test]
    fn flush_resets_pending() {
        let mut buf = TokenBuffer::new(60);
        buf.push("test");
        thread::sleep(Duration::from_millis(20));
        buf.flush();
        assert!(!buf.has_pending());
        assert!(buf.force_flush().is_none());
    }

    // ── adaptive flushing tests ───────────────────────────────────

    #[test]
    fn effective_interval_normal_when_below_threshold() {
        let buf = TokenBuffer::new(60);
        // Empty buffer → normal interval
        assert_eq!(buf.effective_interval(), buf.frame_interval);
    }

    #[test]
    fn effective_interval_shortens_at_threshold() {
        let mut buf = TokenBuffer::new(60);
        // Push exactly BACKLOG_THRESHOLD bytes
        buf.push(&"x".repeat(BACKLOG_THRESHOLD));
        let effective = buf.effective_interval();
        // Should be frame_interval / 2 (ratio=1, divisor=2)
        assert_eq!(effective, buf.frame_interval / 2);
    }

    #[test]
    fn effective_interval_shortens_further_with_larger_backlog() {
        // Use a slower fps so the shortened interval stays above the floor.
        // 10 fps → 100ms interval. At 3× threshold → divisor 4 → 25ms (> 8ms floor).
        let mut buf = TokenBuffer::new(10);
        buf.push(&"x".repeat(BACKLOG_THRESHOLD * 3));
        let effective = buf.effective_interval();
        assert_eq!(effective, buf.frame_interval / 4);
        assert!(effective > BACKLOG_MIN_INTERVAL);
    }

    #[test]
    fn effective_interval_floors_at_minimum() {
        let mut buf = TokenBuffer::new(60);
        // Huge backlog → should clamp to BACKLOG_MIN_INTERVAL
        buf.push(&"x".repeat(BACKLOG_THRESHOLD * 1000));
        let effective = buf.effective_interval();
        assert_eq!(effective, BACKLOG_MIN_INTERVAL);
    }

    #[test]
    fn backlogged_flush_triggers_earlier_than_normal() {
        // Use a low fps (long interval) so we can demonstrate the gap
        let mut buf = TokenBuffer::new(4); // 250ms interval
                                           // Push enough to trigger backlog
        buf.push(&"x".repeat(BACKLOG_THRESHOLD * 2));
        // Sleep just past the shortened interval (~83ms) but well under
        // the full 250ms interval.
        thread::sleep(Duration::from_millis(90));
        assert!(
            buf.should_flush(),
            "backlogged buffer should flush before full interval"
        );
        let content = buf.flush();
        assert!(content.is_some());
    }

    #[test]
    fn small_buffer_does_not_flush_early() {
        let mut buf = TokenBuffer::new(4); // 250ms interval
        buf.push("tiny");
        // Sleep 90ms — well under the 250ms interval
        thread::sleep(Duration::from_millis(90));
        assert!(
            !buf.should_flush(),
            "small buffer should respect full interval"
        );
    }

    // ── metrics tests ─────────────────────────────────────────────

    #[test]
    fn metrics_start_zeroed() {
        let buf = TokenBuffer::new(60);
        let m = buf.metrics();
        assert_eq!(m.flush_count, 0);
        assert_eq!(m.backlog_flush_count, 0);
        assert_eq!(m.bytes_flushed, 0);
        assert_eq!(m.peak_pending_bytes, 0);
    }

    #[test]
    fn metrics_track_normal_flush() {
        let mut buf = TokenBuffer::new(60);
        buf.push("hello");
        thread::sleep(Duration::from_millis(20));
        buf.flush();
        let m = buf.metrics();
        assert_eq!(m.flush_count, 1);
        assert_eq!(m.backlog_flush_count, 0);
        assert_eq!(m.bytes_flushed, 5);
    }

    #[test]
    fn metrics_track_backlog_flush() {
        let mut buf = TokenBuffer::new(4); // 250ms interval
        let payload = "x".repeat(BACKLOG_THRESHOLD * 2);
        buf.push(&payload);
        thread::sleep(Duration::from_millis(90));
        buf.flush();
        let m = buf.metrics();
        assert_eq!(m.flush_count, 1);
        assert_eq!(m.backlog_flush_count, 1);
        assert_eq!(m.bytes_flushed, payload.len() as u64);
    }

    #[test]
    fn metrics_track_force_flush() {
        let mut buf = TokenBuffer::new(60);
        buf.push("abc");
        buf.force_flush();
        let m = buf.metrics();
        assert_eq!(m.flush_count, 1);
        // force_flush doesn't count as backlog flush
        assert_eq!(m.backlog_flush_count, 0);
        assert_eq!(m.bytes_flushed, 3);
    }

    #[test]
    fn metrics_peak_pending_tracks_high_water() {
        let mut buf = TokenBuffer::new(60);
        buf.push(&"x".repeat(100));
        buf.push(&"y".repeat(200));
        // peak should be 300
        assert_eq!(buf.metrics().peak_pending_bytes, 300);
        // After flush, peak is preserved
        buf.force_flush();
        assert_eq!(buf.metrics().peak_pending_bytes, 300);
        buf.push("z");
        // Peak stays at historical high
        assert_eq!(buf.metrics().peak_pending_bytes, 300);
    }

    #[test]
    fn metrics_accumulate_across_flushes() {
        let mut buf = TokenBuffer::new(60);
        buf.push("aaa");
        thread::sleep(Duration::from_millis(20));
        buf.flush();
        buf.push("bb");
        thread::sleep(Duration::from_millis(20));
        buf.flush();
        let m = buf.metrics();
        assert_eq!(m.flush_count, 2);
        assert_eq!(m.bytes_flushed, 5);
    }

    #[test]
    fn pending_len_reflects_buffer_size() {
        let mut buf = TokenBuffer::new(60);
        assert_eq!(buf.pending_len(), 0);
        buf.push("hello");
        assert_eq!(buf.pending_len(), 5);
        buf.push(" world");
        assert_eq!(buf.pending_len(), 11);
        buf.force_flush();
        assert_eq!(buf.pending_len(), 0);
    }

    #[test]
    fn new_with_zero_fps_does_not_panic() {
        // Edge case: fps=0 should not divide by zero
        let buf = TokenBuffer::new(0);
        // Clamped to fps=1 → 1000ms interval
        assert_eq!(buf.frame_interval, Duration::from_millis(1000));
    }
}
