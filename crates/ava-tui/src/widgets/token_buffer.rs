use std::time::{Duration, Instant};

/// Accumulates streaming tokens between frame renders to reduce flicker.
///
/// Tokens are buffered and only flushed when the frame interval has elapsed,
/// batching multiple rapid token arrivals into a single render update.
pub struct TokenBuffer {
    pending: String,
    last_flush: Instant,
    frame_interval: Duration,
}

impl TokenBuffer {
    pub fn new(target_fps: u32) -> Self {
        Self {
            pending: String::new(),
            last_flush: Instant::now(),
            frame_interval: Duration::from_millis(1000 / target_fps as u64),
        }
    }

    /// Add a token chunk to the buffer.
    pub fn push(&mut self, chunk: &str) {
        self.pending.push_str(chunk);
    }

    /// Check if it's time to flush (frame interval elapsed).
    pub fn should_flush(&self) -> bool {
        !self.pending.is_empty() && self.last_flush.elapsed() >= self.frame_interval
    }

    /// Take all buffered content if the frame interval has elapsed.
    pub fn flush(&mut self) -> Option<String> {
        if !self.should_flush() {
            return None;
        }
        self.last_flush = Instant::now();
        Some(std::mem::take(&mut self.pending))
    }

    /// Force flush regardless of timing (e.g., on stream end).
    pub fn force_flush(&mut self) -> Option<String> {
        if self.pending.is_empty() {
            return None;
        }
        self.last_flush = Instant::now();
        Some(std::mem::take(&mut self.pending))
    }

    /// Whether the buffer has pending content.
    pub fn has_pending(&self) -> bool {
        !self.pending.is_empty()
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::thread;

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
}
