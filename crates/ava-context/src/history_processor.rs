//! History processors pipeline (BG2-35, inspired by SWE-Agent).
//!
//! Pluggable pipeline of processors that transform conversation history
//! before sending to the model. Each processor can truncate, filter,
//! or annotate messages.

use ava_types::Message;

/// A processor that transforms conversation history before model calls.
pub trait HistoryProcessor: Send + Sync {
    fn name(&self) -> &str;
    fn process(&self, messages: Vec<Message>) -> Vec<Message>;
}

/// Pipeline of history processors applied in order.
pub struct HistoryPipeline {
    processors: Vec<Box<dyn HistoryProcessor>>,
}

impl Default for HistoryPipeline {
    fn default() -> Self {
        Self::new()
    }
}

impl HistoryPipeline {
    pub fn new() -> Self {
        Self {
            processors: Vec::new(),
        }
    }

    pub fn add(&mut self, processor: Box<dyn HistoryProcessor>) {
        self.processors.push(processor);
    }

    pub fn process(&self, mut messages: Vec<Message>) -> Vec<Message> {
        for processor in &self.processors {
            messages = processor.process(messages);
        }
        messages
    }

    pub fn len(&self) -> usize {
        self.processors.len()
    }

    pub fn is_empty(&self) -> bool {
        self.processors.is_empty()
    }
}

/// Truncate tool result content to a maximum length.
pub struct TruncateObservations {
    pub max_chars: usize,
}

impl TruncateObservations {
    pub fn new(max_chars: usize) -> Self {
        Self { max_chars }
    }
}

impl HistoryProcessor for TruncateObservations {
    fn name(&self) -> &str {
        "truncate_observations"
    }

    fn process(&self, messages: Vec<Message>) -> Vec<Message> {
        messages
            .into_iter()
            .map(|mut msg| {
                for tr in &mut msg.tool_results {
                    if tr.content.len() > self.max_chars {
                        tr.content = format!(
                            "{}...\n[truncated to {} chars]",
                            &tr.content[..self.max_chars],
                            self.max_chars
                        );
                    }
                }
                if msg.role == ava_types::Role::Tool && msg.content.len() > self.max_chars {
                    msg.content = format!(
                        "{}...\n[truncated to {} chars]",
                        &msg.content[..self.max_chars],
                        self.max_chars
                    );
                }
                msg
            })
            .collect()
    }
}

/// Filter out messages matching a predicate.
pub struct FilterContent {
    /// Roles to keep. Messages with other roles are dropped.
    pub keep_roles: Vec<ava_types::Role>,
}

impl FilterContent {
    pub fn keep_all_except_system() -> Self {
        Self {
            keep_roles: vec![
                ava_types::Role::User,
                ava_types::Role::Assistant,
                ava_types::Role::Tool,
            ],
        }
    }
}

impl HistoryProcessor for FilterContent {
    fn name(&self) -> &str {
        "filter_content"
    }

    fn process(&self, messages: Vec<Message>) -> Vec<Message> {
        messages
            .into_iter()
            .filter(|m| self.keep_roles.contains(&m.role))
            .collect()
    }
}

/// Remove empty messages (no content, no tool calls, no tool results).
pub struct RemoveEmpty;

impl HistoryProcessor for RemoveEmpty {
    fn name(&self) -> &str {
        "remove_empty"
    }

    fn process(&self, messages: Vec<Message>) -> Vec<Message> {
        messages
            .into_iter()
            .filter(|m| {
                !m.content.trim().is_empty()
                    || !m.tool_calls.is_empty()
                    || !m.tool_results.is_empty()
            })
            .collect()
    }
}

/// Keep only the last N messages.
pub struct KeepRecent {
    pub count: usize,
}

impl KeepRecent {
    pub fn new(count: usize) -> Self {
        Self { count }
    }
}

impl HistoryProcessor for KeepRecent {
    fn name(&self) -> &str {
        "keep_recent"
    }

    fn process(&self, messages: Vec<Message>) -> Vec<Message> {
        if messages.len() <= self.count {
            messages
        } else {
            messages[messages.len() - self.count..].to_vec()
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use ava_types::{Message, Role, ToolResult};

    #[test]
    fn truncate_observations_shortens_long_results() {
        let processor = TruncateObservations::new(50);
        let mut msg = Message::new(Role::Assistant, "read the file");
        msg.tool_results.push(ToolResult {
            call_id: "1".to_string(),
            content: "x".repeat(200),
            is_error: false,
        });

        let result = processor.process(vec![msg]);
        assert!(result[0].tool_results[0].content.len() < 100);
        assert!(result[0].tool_results[0].content.contains("[truncated"));
    }

    #[test]
    fn truncate_leaves_short_results_alone() {
        let processor = TruncateObservations::new(1000);
        let mut msg = Message::new(Role::Assistant, "ok");
        msg.tool_results.push(ToolResult {
            call_id: "1".to_string(),
            content: "short result".to_string(),
            is_error: false,
        });

        let result = processor.process(vec![msg]);
        assert_eq!(result[0].tool_results[0].content, "short result");
    }

    #[test]
    fn filter_removes_system_messages() {
        let filter = FilterContent::keep_all_except_system();
        let messages = vec![
            Message::new(Role::System, "system prompt"),
            Message::new(Role::User, "hello"),
            Message::new(Role::Assistant, "hi"),
        ];

        let result = filter.process(messages);
        assert_eq!(result.len(), 2);
        assert_eq!(result[0].role, Role::User);
    }

    #[test]
    fn remove_empty_drops_blank_messages() {
        let processor = RemoveEmpty;
        let messages = vec![
            Message::new(Role::User, "hello"),
            Message::new(Role::Assistant, ""),
            Message::new(Role::User, "  "),
            Message::new(Role::Assistant, "response"),
        ];

        let result = processor.process(messages);
        assert_eq!(result.len(), 2);
    }

    #[test]
    fn keep_recent_limits_messages() {
        let processor = KeepRecent::new(3);
        let messages: Vec<Message> = (0..10)
            .map(|i| Message::new(Role::User, format!("msg{i}")))
            .collect();

        let result = processor.process(messages);
        assert_eq!(result.len(), 3);
        assert_eq!(result[0].content, "msg7");
    }

    #[test]
    fn pipeline_chains_processors() {
        let mut pipeline = HistoryPipeline::new();
        pipeline.add(Box::new(RemoveEmpty));
        pipeline.add(Box::new(KeepRecent::new(2)));

        let messages = vec![
            Message::new(Role::User, "a"),
            Message::new(Role::Assistant, ""),
            Message::new(Role::User, "b"),
            Message::new(Role::Assistant, "c"),
        ];

        let result = pipeline.process(messages);
        // RemoveEmpty: ["a", "b", "c"], then KeepRecent(2): ["b", "c"]
        assert_eq!(result.len(), 2);
        assert_eq!(result[0].content, "b");
        assert_eq!(result[1].content, "c");
    }

    #[test]
    fn empty_pipeline_passes_through() {
        let pipeline = HistoryPipeline::new();
        let messages = vec![Message::new(Role::User, "test")];
        let result = pipeline.process(messages);
        assert_eq!(result.len(), 1);
    }
}
