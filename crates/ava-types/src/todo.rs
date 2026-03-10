//! Todo/progress tracking types shared between ava-tools and ava-tui.

use std::sync::{Arc, RwLock};

/// A single todo item tracked by the agent.
#[derive(Debug, Clone, serde::Serialize, serde::Deserialize)]
pub struct TodoItem {
    pub content: String,
    pub status: TodoStatus,
    pub priority: TodoPriority,
}

/// Status of a todo item.
#[derive(Debug, Clone, Copy, PartialEq, Eq, serde::Serialize, serde::Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum TodoStatus {
    Pending,
    InProgress,
    Completed,
    Cancelled,
}

impl std::fmt::Display for TodoStatus {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Self::Pending => write!(f, "pending"),
            Self::InProgress => write!(f, "in_progress"),
            Self::Completed => write!(f, "completed"),
            Self::Cancelled => write!(f, "cancelled"),
        }
    }
}

/// Priority of a todo item.
#[derive(Debug, Clone, Copy, PartialEq, Eq, serde::Serialize, serde::Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum TodoPriority {
    High,
    Medium,
    Low,
}

impl std::fmt::Display for TodoPriority {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Self::High => write!(f, "high"),
            Self::Medium => write!(f, "medium"),
            Self::Low => write!(f, "low"),
        }
    }
}

/// Shared todo state accessible by both the tool implementations and the TUI.
///
/// Uses `std::sync::RwLock` (not tokio) because the protected data is a simple
/// `Vec` with no I/O. This allows synchronous access from the TUI render path.
#[derive(Debug, Clone, Default)]
pub struct TodoState {
    items: Arc<RwLock<Vec<TodoItem>>>,
}

impl TodoState {
    pub fn new() -> Self {
        Self::default()
    }

    /// Replace the entire todo list (full-replace semantics).
    pub fn set(&self, items: Vec<TodoItem>) {
        *self.items.write().expect("TodoState poisoned") = items;
    }

    /// Get a snapshot of the current todo list.
    pub fn get(&self) -> Vec<TodoItem> {
        self.items.read().expect("TodoState poisoned").clone()
    }

    /// Count of non-completed (pending + in_progress) items.
    pub fn incomplete_count(&self) -> usize {
        self.items
            .read()
            .expect("TodoState poisoned")
            .iter()
            .filter(|t| !matches!(t.status, TodoStatus::Completed | TodoStatus::Cancelled))
            .count()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn todo_status_display() {
        assert_eq!(TodoStatus::Pending.to_string(), "pending");
        assert_eq!(TodoStatus::InProgress.to_string(), "in_progress");
        assert_eq!(TodoStatus::Completed.to_string(), "completed");
        assert_eq!(TodoStatus::Cancelled.to_string(), "cancelled");
    }

    #[test]
    fn todo_priority_display() {
        assert_eq!(TodoPriority::High.to_string(), "high");
        assert_eq!(TodoPriority::Medium.to_string(), "medium");
        assert_eq!(TodoPriority::Low.to_string(), "low");
    }

    #[test]
    fn todo_state_set_and_get() {
        let state = TodoState::new();
        assert!(state.get().is_empty());

        state.set(vec![TodoItem {
            content: "Write tests".into(),
            status: TodoStatus::Pending,
            priority: TodoPriority::High,
        }]);

        let items = state.get();
        assert_eq!(items.len(), 1);
        assert_eq!(items[0].content, "Write tests");
    }

    #[test]
    fn todo_state_incomplete_count() {
        let state = TodoState::new();
        state.set(vec![
            TodoItem {
                content: "Done".into(),
                status: TodoStatus::Completed,
                priority: TodoPriority::Low,
            },
            TodoItem {
                content: "Active".into(),
                status: TodoStatus::InProgress,
                priority: TodoPriority::Medium,
            },
            TodoItem {
                content: "Todo".into(),
                status: TodoStatus::Pending,
                priority: TodoPriority::High,
            },
            TodoItem {
                content: "Dropped".into(),
                status: TodoStatus::Cancelled,
                priority: TodoPriority::Low,
            },
        ]);

        assert_eq!(state.incomplete_count(), 2);
    }

    #[test]
    fn todo_state_full_replace() {
        let state = TodoState::new();
        state.set(vec![TodoItem {
            content: "First".into(),
            status: TodoStatus::Pending,
            priority: TodoPriority::High,
        }]);

        // Full replace — old items gone
        state.set(vec![TodoItem {
            content: "Second".into(),
            status: TodoStatus::InProgress,
            priority: TodoPriority::Medium,
        }]);

        let items = state.get();
        assert_eq!(items.len(), 1);
        assert_eq!(items[0].content, "Second");
    }

    #[test]
    fn todo_item_serde_roundtrip() {
        let item = TodoItem {
            content: "Test serde".into(),
            status: TodoStatus::InProgress,
            priority: TodoPriority::High,
        };
        let json = serde_json::to_string(&item).unwrap();
        let deserialized: TodoItem = serde_json::from_str(&json).unwrap();
        assert_eq!(deserialized.content, "Test serde");
        assert_eq!(deserialized.status, TodoStatus::InProgress);
        assert_eq!(deserialized.priority, TodoPriority::High);
    }

    #[test]
    fn todo_state_shared_across_clones() {
        let state1 = TodoState::new();
        let state2 = state1.clone();

        state1.set(vec![TodoItem {
            content: "Shared".into(),
            status: TodoStatus::Pending,
            priority: TodoPriority::Medium,
        }]);

        let items = state2.get();
        assert_eq!(items.len(), 1);
        assert_eq!(items[0].content, "Shared");
    }
}
