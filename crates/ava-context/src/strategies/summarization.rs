use std::sync::Arc;

use async_trait::async_trait;
use ava_types::{Message, Role};

use super::{AsyncCondensationStrategy, Summarizer};
use crate::error::Result;
use crate::token_tracker::estimate_tokens_for_message;

pub struct SummarizationStrategy {
    summarizer: Option<Arc<dyn Summarizer>>,
    batch_size: usize,
    preserve_recent: usize,
}

impl SummarizationStrategy {
    pub fn new(
        summarizer: Option<Arc<dyn Summarizer>>,
        batch_size: usize,
        preserve_recent: usize,
    ) -> Self {
        Self {
            summarizer,
            batch_size,
            preserve_recent,
        }
    }

    /// Extract key information from messages without an LLM call.
    fn heuristic_summary(messages: &[Message]) -> String {
        let mut file_paths = Vec::new();
        let mut tool_names = Vec::new();
        let mut errors = Vec::new();
        let mut decisions = Vec::new();

        for msg in messages {
            // Extract file paths (common patterns: /path/to/file, ./relative)
            for word in msg.content.split_whitespace() {
                let trimmed = word.trim_matches(|c: char| {
                    !c.is_alphanumeric() && c != '/' && c != '.' && c != '_' && c != '-'
                });
                if (trimmed.starts_with('/') || trimmed.starts_with("./"))
                    && trimmed.len() > 2
                    && !file_paths.contains(&trimmed.to_string())
                {
                    file_paths.push(trimmed.to_string());
                }
            }

            // Extract tool names from tool results
            for result in &msg.tool_results {
                if !result.call_id.is_empty() {
                    let name = result.call_id.split('_').next().unwrap_or(&result.call_id);
                    if !tool_names.contains(&name.to_string()) {
                        tool_names.push(name.to_string());
                    }
                }
                if result.is_error && !result.content.is_empty() {
                    let snippet: String = result.content.chars().take(120).collect();
                    errors.push(snippet);
                }
            }

            // Extract tool call names
            for call in &msg.tool_calls {
                if !tool_names.contains(&call.name) {
                    tool_names.push(call.name.clone());
                }
            }

            // Capture short assistant messages as potential decisions
            if msg.role == Role::Assistant
                && !msg.content.trim().is_empty()
                && msg.content.len() < 300
            {
                decisions.push(msg.content.trim().to_string());
            }
        }

        let mut lines = Vec::new();
        lines.push(format!("[Summary of {} previous messages]", messages.len()));

        if !file_paths.is_empty() {
            let paths: Vec<_> = file_paths.iter().take(20).cloned().collect();
            lines.push(format!("- Files mentioned: {}", paths.join(", ")));
        }
        if !tool_names.is_empty() {
            let names: Vec<_> = tool_names.iter().take(15).cloned().collect();
            lines.push(format!("- Tools used: {}", names.join(", ")));
        }
        if !errors.is_empty() {
            let errs: Vec<_> = errors.iter().take(5).cloned().collect();
            lines.push(format!("- Errors encountered: {}", errs.join("; ")));
        }
        if !decisions.is_empty() {
            let decs: Vec<_> = decisions.iter().take(5).cloned().collect();
            lines.push("- Key points:".to_string());
            for dec in decs {
                lines.push(format!("  - {dec}"));
            }
        }

        lines.join("\n")
    }

    /// Format messages into text for the LLM summarization prompt.
    fn format_messages_for_prompt(messages: &[Message]) -> String {
        let mut parts = Vec::new();
        for msg in messages {
            let role = match msg.role {
                Role::System => "system",
                Role::User => "user",
                Role::Assistant => "assistant",
                Role::Tool => "tool",
            };
            parts.push(format!("[{role}]: {}", msg.content));
            for result in &msg.tool_results {
                let status = if result.is_error { "error" } else { "ok" };
                let snippet: String = result.content.chars().take(500).collect();
                parts.push(format!("  tool_result({status}): {snippet}"));
            }
        }
        parts.join("\n")
    }

    /// Split messages into (system, old_batch, recent) preserving system prompt and recent messages.
    fn partition_messages(
        messages: &[Message],
        batch_size: usize,
        preserve_recent: usize,
    ) -> (Vec<Message>, Vec<Message>, Vec<Message>) {
        let mut system_msgs = Vec::new();
        let mut non_system: Vec<Message> = Vec::new();

        for msg in messages {
            if msg.role == Role::System {
                system_msgs.push(msg.clone());
            } else {
                non_system.push(msg.clone());
            }
        }

        let preserve = preserve_recent.min(non_system.len());
        let available = non_system.len().saturating_sub(preserve);
        let batch = batch_size.min(available);

        if batch == 0 {
            return (system_msgs, Vec::new(), non_system);
        }

        let old_batch = non_system[..batch].to_vec();
        let recent = non_system[batch..].to_vec();

        (system_msgs, old_batch, recent)
    }
}

#[async_trait]
impl AsyncCondensationStrategy for SummarizationStrategy {
    fn name(&self) -> &'static str {
        "summarization"
    }

    async fn condense(&self, messages: &[Message], max_tokens: usize) -> Result<Vec<Message>> {
        let (system_msgs, old_batch, recent) =
            Self::partition_messages(messages, self.batch_size, self.preserve_recent);

        // Nothing to summarize
        if old_batch.is_empty() {
            return Ok(messages.to_vec());
        }

        // Try LLM summarization, fall back to heuristic
        let summary_text = if let Some(summarizer) = &self.summarizer {
            let formatted = Self::format_messages_for_prompt(&old_batch);
            let prompt = format!(
                "Summarize the following conversation concisely, preserving key decisions, \
                 file paths mentioned, and tool outcomes:\n\n{formatted}"
            );
            match summarizer.summarize(&prompt).await {
                Ok(summary) => summary,
                Err(_) => Self::heuristic_summary(&old_batch),
            }
        } else {
            Self::heuristic_summary(&old_batch)
        };

        let summary_msg = Message::new(Role::System, summary_text);

        // Rebuild: system messages + summary + recent
        let mut result = system_msgs;
        result.push(summary_msg);
        result.extend(recent);

        // Check if we're under budget; if not, return as-is (sliding window will handle the rest)
        let total: usize = result.iter().map(estimate_tokens_for_message).sum();
        if total <= max_tokens {
            return Ok(result);
        }

        // Still over budget — return what we have (the condenser pipeline will try the next strategy)
        Ok(result)
    }
}

#[cfg(test)]
mod tests {
    use ava_types::{Message, Role, ToolCall, ToolResult};

    use super::*;

    #[tokio::test]
    async fn heuristic_summary_extracts_file_paths() {
        let messages = vec![
            Message::new(Role::User, "Please read /src/main.rs"),
            Message::new(Role::Assistant, "I'll read that file"),
            Message::new(Role::User, "Also check ./tests/test.rs"),
        ];
        let summary = SummarizationStrategy::heuristic_summary(&messages);
        assert!(summary.contains("/src/main.rs"));
        assert!(summary.contains("./tests/test.rs"));
    }

    #[tokio::test]
    async fn heuristic_summary_extracts_tool_names() {
        let mut msg = Message::new(Role::Assistant, "reading file");
        msg.tool_calls.push(ToolCall {
            id: "call_1".to_string(),
            name: "read".to_string(),
            arguments: serde_json::Value::Null,
        });
        let messages = vec![msg];
        let summary = SummarizationStrategy::heuristic_summary(&messages);
        assert!(summary.contains("read"));
    }

    #[tokio::test]
    async fn heuristic_summary_extracts_errors() {
        let mut msg = Message::new(Role::Tool, "error output");
        msg.tool_results.push(ToolResult {
            call_id: "call_1".to_string(),
            content: "file not found: /tmp/missing".to_string(),
            is_error: true,
        });
        let messages = vec![msg];
        let summary = SummarizationStrategy::heuristic_summary(&messages);
        assert!(summary.contains("file not found"));
    }

    #[tokio::test]
    async fn partition_preserves_system_and_recent() {
        let messages = vec![
            Message::new(Role::System, "system prompt"),
            Message::new(Role::User, "msg1"),
            Message::new(Role::User, "msg2"),
            Message::new(Role::User, "msg3"),
            Message::new(Role::User, "msg4"),
            Message::new(Role::User, "msg5"),
        ];
        let (system, old, recent) = SummarizationStrategy::partition_messages(&messages, 3, 2);
        assert_eq!(system.len(), 1);
        assert_eq!(system[0].content, "system prompt");
        assert_eq!(old.len(), 3);
        assert_eq!(recent.len(), 2);
        assert_eq!(recent[0].content, "msg4");
        assert_eq!(recent[1].content, "msg5");
    }

    #[tokio::test]
    async fn condense_without_summarizer_uses_heuristic() {
        let strategy = SummarizationStrategy::new(None, 5, 2);
        let messages = vec![
            Message::new(Role::System, "system prompt"),
            Message::new(Role::User, "read /src/lib.rs"),
            Message::new(Role::Assistant, "done"),
            Message::new(Role::User, "read /src/main.rs"),
            Message::new(Role::Assistant, "done"),
            Message::new(Role::User, "what now?"),
            Message::new(Role::Assistant, "all done"),
        ];
        let result = strategy.condense(&messages, 10_000).await.unwrap();
        // Should have: system prompt + summary + 2 recent messages
        assert!(result.len() < messages.len());
        assert_eq!(result[0].content, "system prompt");
        // Second message should be the summary
        assert!(result[1].content.contains("[Summary of"));
    }

    #[tokio::test]
    async fn condense_noop_when_nothing_to_summarize() {
        let strategy = SummarizationStrategy::new(None, 5, 10);
        let messages = vec![
            Message::new(Role::System, "system prompt"),
            Message::new(Role::User, "hello"),
            Message::new(Role::Assistant, "hi"),
        ];
        // preserve_recent=10 but only 2 non-system messages → nothing to summarize
        let result = strategy.condense(&messages, 10_000).await.unwrap();
        assert_eq!(result.len(), messages.len());
    }

    struct MockSummarizer;

    #[async_trait]
    impl Summarizer for MockSummarizer {
        async fn summarize(&self, _text: &str) -> std::result::Result<String, String> {
            Ok("LLM summary: user asked to read files and got results".to_string())
        }
    }

    #[tokio::test]
    async fn condense_with_llm_summarizer() {
        let summarizer = Arc::new(MockSummarizer);
        let strategy = SummarizationStrategy::new(Some(summarizer), 3, 1);
        let messages = vec![
            Message::new(Role::System, "system prompt"),
            Message::new(Role::User, "read /src/lib.rs"),
            Message::new(Role::Assistant, "file contents..."),
            Message::new(Role::User, "read /src/main.rs"),
            Message::new(Role::Assistant, "more contents..."),
            Message::new(Role::User, "what now?"),
        ];
        let result = strategy.condense(&messages, 10_000).await.unwrap();
        // system + LLM summary + 1 recent
        assert!(result.len() < messages.len());
        assert!(result[1].content.contains("LLM summary"));
    }

    struct FailingSummarizer;

    #[async_trait]
    impl Summarizer for FailingSummarizer {
        async fn summarize(&self, _text: &str) -> std::result::Result<String, String> {
            Err("network error".to_string())
        }
    }

    #[tokio::test]
    async fn condense_falls_back_to_heuristic_on_llm_failure() {
        let summarizer = Arc::new(FailingSummarizer);
        let strategy = SummarizationStrategy::new(Some(summarizer), 3, 1);
        let messages = vec![
            Message::new(Role::System, "system prompt"),
            Message::new(Role::User, "read /src/lib.rs"),
            Message::new(Role::Assistant, "ok"),
            Message::new(Role::User, "read /src/main.rs"),
            Message::new(Role::Assistant, "ok"),
            Message::new(Role::User, "done?"),
        ];
        let result = strategy.condense(&messages, 10_000).await.unwrap();
        assert!(result.len() < messages.len());
        // Should use heuristic, not LLM
        assert!(result[1].content.contains("[Summary of"));
    }
}
