//! Content replacement budget — evicts old tool outputs when total conversation
//! content exceeds a configurable character budget.
//!
//! Unlike token-based pruning in [`crate::pruner`], this module tracks individual
//! tool call outputs by `call_id` and evicts the oldest entries first while
//! protecting the most recent 40% of the budget from eviction.

use ava_types::{Message, Role};

/// Default maximum character budget for all tool outputs in a conversation.
const DEFAULT_MAX_BUDGET: usize = 500_000;

/// Fraction of the budget that is protected from eviction (most recent entries).
const PROTECTED_FRACTION: f64 = 0.40;

/// A single tracked tool output entry.
#[derive(Debug, Clone)]
pub struct ContentEntry {
    pub call_id: String,
    pub tool_name: String,
    pub char_count: usize,
    pub turn: usize,
}

/// Tracks total tool output size and evicts the oldest entries when the budget
/// is exceeded. The most recent 40% of tracked content is protected.
#[derive(Debug)]
pub struct ContentReplacementBudget {
    pub total_chars: usize,
    pub max_budget: usize,
    pub entries: Vec<ContentEntry>,
}

impl Default for ContentReplacementBudget {
    fn default() -> Self {
        Self::new(DEFAULT_MAX_BUDGET)
    }
}

impl ContentReplacementBudget {
    pub fn new(max_budget: usize) -> Self {
        Self {
            total_chars: 0,
            max_budget,
            entries: Vec::new(),
        }
    }

    /// Record a tool output for budget tracking.
    pub fn record_output(&mut self, call_id: &str, tool_name: &str, content: &str, turn: usize) {
        let char_count = content.len();
        self.total_chars += char_count;
        self.entries.push(ContentEntry {
            call_id: call_id.to_string(),
            tool_name: tool_name.to_string(),
            char_count,
            turn,
        });
    }

    /// Returns `true` if the total tracked content exceeds the budget.
    pub fn is_over_budget(&self) -> bool {
        self.total_chars > self.max_budget
    }

    /// Evict the oldest tool results from `messages`, replacing their content
    /// with a short placeholder. The most recent 40% of the budget (by character
    /// count, measured from newest entries) is protected from eviction.
    ///
    /// Returns the number of messages evicted.
    pub fn evict_oldest(&mut self, messages: &mut [Message]) -> usize {
        if !self.is_over_budget() || self.entries.is_empty() {
            return 0;
        }

        let protected_chars = (self.max_budget as f64 * PROTECTED_FRACTION) as usize;

        // Walk entries from newest to oldest to find the protection boundary.
        // Everything at index >= `protected_boundary` is protected (not evicted).
        let mut chars_from_newest: usize = 0;
        let mut protected_boundary = self.entries.len();
        for i in (0..self.entries.len()).rev() {
            chars_from_newest += self.entries[i].char_count;
            protected_boundary = i;
            if chars_from_newest >= protected_chars {
                break;
            }
        }

        // If everything fits in the protected window, nothing to evict.
        if protected_boundary == 0 {
            return 0;
        }

        // Collect call_ids of entries to evict (oldest, outside protection)
        let evict_ids: std::collections::HashSet<String> = self.entries[..protected_boundary]
            .iter()
            .map(|e| e.call_id.clone())
            .collect();

        let mut evicted_count = 0;

        for msg in messages.iter_mut() {
            if msg.role != Role::Tool {
                continue;
            }

            // Check if this message's tool_call_id matches an eviction target
            let should_evict = msg
                .tool_call_id
                .as_ref()
                .map(|id| evict_ids.contains(id))
                .unwrap_or(false);

            // Also check tool_results for matching call_ids
            let mut result_evicted = false;
            for result in &mut msg.tool_results {
                if evict_ids.contains(&result.call_id) && result.content.len() > 50 {
                    let original_len = result.content.len();
                    result.content = format!(
                        "[output evicted — conversation budget exceeded, was {} chars]",
                        original_len
                    );
                    result_evicted = true;
                }
            }

            if should_evict && msg.content.len() > 50 {
                let original_len = msg.content.len();
                msg.content = format!(
                    "[output evicted — conversation budget exceeded, was {} chars]",
                    original_len
                );
                evicted_count += 1;
            } else if result_evicted {
                evicted_count += 1;
            }
        }

        // Update tracking: remove evicted entries and recalculate total
        self.entries = self.entries.split_off(protected_boundary);
        self.total_chars = self.entries.iter().map(|e| e.char_count).sum();

        evicted_count
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use ava_types::{Message, Role, ToolResult};

    fn tool_message_with_id(call_id: &str, content: &str) -> Message {
        let mut msg = Message::new(Role::Tool, content);
        msg.tool_call_id = Some(call_id.to_string());
        msg.tool_results.push(ToolResult {
            call_id: call_id.to_string(),
            content: content.to_string(),
            is_error: false,
        });
        msg
    }

    #[test]
    fn under_budget_no_eviction() {
        let mut budget = ContentReplacementBudget::new(10_000);
        budget.record_output("c1", "read", "short output", 1);
        budget.record_output("c2", "grep", "another output", 2);

        assert!(!budget.is_over_budget());

        let mut messages = vec![
            tool_message_with_id("c1", "short output"),
            tool_message_with_id("c2", "another output"),
        ];
        let evicted = budget.evict_oldest(&mut messages);
        assert_eq!(evicted, 0);
        assert_eq!(messages[0].content, "short output");
    }

    #[test]
    fn over_budget_oldest_evicted() {
        // Budget of 200 chars
        let mut budget = ContentReplacementBudget::new(200);

        let old_content = "x".repeat(150);
        let new_content = "y".repeat(100);

        budget.record_output("c1", "read", &old_content, 1);
        budget.record_output("c2", "read", &new_content, 2);

        assert!(budget.is_over_budget()); // 250 > 200

        let mut messages = vec![
            Message::new(Role::User, "hello"),
            tool_message_with_id("c1", &old_content),
            Message::new(Role::Assistant, "result"),
            tool_message_with_id("c2", &new_content),
        ];

        let evicted = budget.evict_oldest(&mut messages);
        assert!(evicted > 0);
        assert!(messages[1].content.contains("evicted"));
        assert!(messages[1].content.contains("150 chars"));
        // Recent content should be protected
        assert_eq!(messages[3].content, new_content);
    }

    #[test]
    fn recent_protected_from_eviction() {
        // Budget of 100, protection = 40%
        let mut budget = ContentReplacementBudget::new(100);

        // Three entries: turn 1, 2, 3. Total = 180 > 100
        budget.record_output("c1", "read", &"a".repeat(60), 1);
        budget.record_output("c2", "read", &"b".repeat(60), 2);
        budget.record_output("c3", "read", &"c".repeat(60), 3);

        assert!(budget.is_over_budget());

        let mut messages = vec![
            tool_message_with_id("c1", &"a".repeat(60)),
            tool_message_with_id("c2", &"b".repeat(60)),
            tool_message_with_id("c3", &"c".repeat(60)),
        ];

        budget.evict_oldest(&mut messages);

        // c3 (newest, within 40% = 40 chars protection) should be safe
        assert_eq!(messages[2].content, "c".repeat(60));
        // c1 (oldest) should be evicted
        assert!(messages[0].content.contains("evicted"));
    }

    #[test]
    fn empty_budget_no_panic() {
        let mut budget = ContentReplacementBudget::new(1000);
        assert!(!budget.is_over_budget());

        let mut messages: Vec<Message> = vec![];
        let evicted = budget.evict_oldest(&mut messages);
        assert_eq!(evicted, 0);
    }

    #[test]
    fn default_budget_value() {
        let budget = ContentReplacementBudget::default();
        assert_eq!(budget.max_budget, 500_000);
        assert_eq!(budget.total_chars, 0);
        assert!(budget.entries.is_empty());
    }

    #[test]
    fn record_output_accumulates_chars() {
        let mut budget = ContentReplacementBudget::new(1000);
        budget.record_output("c1", "read", "hello", 1);
        assert_eq!(budget.total_chars, 5);
        budget.record_output("c2", "grep", "world!", 2);
        assert_eq!(budget.total_chars, 11);
        assert_eq!(budget.entries.len(), 2);
    }

    #[test]
    fn eviction_updates_tracking() {
        let mut budget = ContentReplacementBudget::new(100);
        budget.record_output("c1", "read", &"x".repeat(80), 1);
        budget.record_output("c2", "read", &"y".repeat(80), 2);

        assert!(budget.is_over_budget()); // 160 > 100

        let mut messages = vec![
            tool_message_with_id("c1", &"x".repeat(80)),
            tool_message_with_id("c2", &"y".repeat(80)),
        ];

        budget.evict_oldest(&mut messages);

        // After eviction, tracking should only contain protected entries
        assert!(
            budget.total_chars <= 100,
            "total_chars should be within budget after eviction"
        );
    }
}
