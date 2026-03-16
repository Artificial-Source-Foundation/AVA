use std::time::{Duration, Instant};

const TOAST_DURATION: Duration = Duration::from_secs(3);
const MAX_TOASTS: usize = 3;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum ToastKind {
    Success,
    Info,
}

#[derive(Debug, Clone)]
pub struct Toast {
    pub message: String,
    pub kind: ToastKind,
    pub created_at: Instant,
}

#[derive(Debug, Default)]
pub struct ToastState {
    pub toasts: Vec<Toast>,
}

impl ToastState {
    pub fn push(&mut self, message: impl Into<String>) {
        self.push_kind(message, ToastKind::Info);
    }

    pub fn push_success(&mut self, message: impl Into<String>) {
        self.push_kind(message, ToastKind::Success);
    }

    fn push_kind(&mut self, message: impl Into<String>, kind: ToastKind) {
        self.toasts.push(Toast {
            message: message.into(),
            kind,
            created_at: Instant::now(),
        });
        if self.toasts.len() > MAX_TOASTS {
            self.toasts.remove(0);
        }
    }

    /// Remove expired toasts. Call this on each render tick.
    pub fn cleanup(&mut self) {
        self.toasts
            .retain(|t| t.created_at.elapsed() < TOAST_DURATION);
    }

    pub fn is_empty(&self) -> bool {
        self.toasts.is_empty()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn push_and_cleanup() {
        let mut state = ToastState::default();
        state.push("hello");
        assert_eq!(state.toasts.len(), 1);
        assert!(!state.is_empty());

        state.cleanup();
        assert_eq!(state.toasts.len(), 1); // still fresh

        // Force-expire by backdating
        state.toasts[0].created_at = Instant::now() - Duration::from_secs(5);
        state.cleanup();
        assert!(state.is_empty());
    }

    #[test]
    fn max_toasts_evicts_oldest() {
        let mut state = ToastState::default();
        state.push("one");
        state.push("two");
        state.push("three");
        state.push("four"); // should evict "one"
        assert_eq!(state.toasts.len(), MAX_TOASTS);
        assert_eq!(state.toasts[0].message, "two");
    }
}
