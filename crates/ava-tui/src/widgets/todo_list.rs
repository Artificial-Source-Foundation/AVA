//! Todo list helpers for the TUI sidebar.
//!
//! Provides utility functions for rendering the agent's todo checklist.
//! The actual rendering is inlined into `ui::sidebar::render_sidebar` for
//! tight integration with the sidebar layout.

use ava_types::TodoStatus;

/// Returns true if any item is incomplete (pending or in_progress).
pub fn has_incomplete(items: &[ava_types::TodoItem]) -> bool {
    items
        .iter()
        .any(|t| matches!(t.status, TodoStatus::Pending | TodoStatus::InProgress))
}
