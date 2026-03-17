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
    /// Previous summary to build upon incrementally.
    previous_summary: Option<String>,
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
            previous_summary: None,
        }
    }

    /// Set a previous summary to build upon incrementally.
    pub fn set_previous_summary(&mut self, summary: Option<String>) {
        self.previous_summary = summary;
    }

    /// Get the last summary produced (stored after each condense call).
    pub fn previous_summary(&self) -> Option<&str> {
        self.previous_summary.as_deref()
    }

    /// Extract key information from messages without an LLM call.
    fn heuristic_summary(messages: &[Message], previous_summary: Option<&str>) -> String {
        let mut files_read = Vec::new();
        let mut files_modified = Vec::new();
        let mut tool_names = Vec::new();
        let mut errors = Vec::new();
        let mut decisions = Vec::new();

        // Seed from previous summary's file tracking lines
        if let Some(prev) = previous_summary {
            for line in prev.lines() {
                if let Some(rest) = line.strip_prefix("Files read: ") {
                    for p in rest.split(", ") {
                        let p = p.trim().to_string();
                        if !p.is_empty() && !files_read.contains(&p) {
                            files_read.push(p);
                        }
                    }
                } else if let Some(rest) = line.strip_prefix("Files modified: ") {
                    for p in rest.split(", ") {
                        let p = p.trim().to_string();
                        if !p.is_empty() && !files_modified.contains(&p) {
                            files_modified.push(p);
                        }
                    }
                }
            }
        }

        for msg in messages {
            // Extract file paths (common patterns: /path/to/file, ./relative)
            for word in msg.content.split_whitespace() {
                let trimmed = word.trim_matches(|c: char| {
                    !c.is_alphanumeric() && c != '/' && c != '.' && c != '_' && c != '-'
                });
                if (trimmed.starts_with('/') || trimmed.starts_with("./"))
                    && trimmed.len() > 2
                    && !files_read.contains(&trimmed.to_string())
                    && !files_modified.contains(&trimmed.to_string())
                {
                    files_read.push(trimmed.to_string());
                }
            }

            // Classify files as read vs modified based on tool calls
            for call in &msg.tool_calls {
                if !tool_names.contains(&call.name) {
                    tool_names.push(call.name.clone());
                }
                // Extract file path from tool arguments if present
                let path = call
                    .arguments
                    .get("file_path")
                    .or_else(|| call.arguments.get("path"))
                    .and_then(|v| v.as_str())
                    .map(|s| s.to_string());
                if let Some(p) = path {
                    let is_write = matches!(
                        call.name.as_str(),
                        "write" | "edit" | "apply_patch" | "multiedit"
                    );
                    if is_write {
                        if !files_modified.contains(&p) {
                            files_modified.push(p.clone());
                        }
                        // Remove from files_read if it was there
                        files_read.retain(|f| f != &p);
                    } else if !files_read.contains(&p) && !files_modified.contains(&p) {
                        files_read.push(p);
                    }
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

            // Capture short assistant messages as potential decisions
            if msg.role == Role::Assistant
                && !msg.content.trim().is_empty()
                && msg.content.len() < 300
            {
                decisions.push(msg.content.trim().to_string());
            }
        }

        let mut lines = Vec::new();

        if previous_summary.is_some() {
            lines.push(format!(
                "[Updated summary — {} new messages]",
                messages.len()
            ));
        } else {
            lines.push(format!("[Summary of {} previous messages]", messages.len()));
        }

        if !files_read.is_empty() {
            let paths: Vec<_> = files_read.iter().take(20).cloned().collect();
            lines.push(format!("Files read: {}", paths.join(", ")));
        }
        if !files_modified.is_empty() {
            let paths: Vec<_> = files_modified.iter().take(20).cloned().collect();
            lines.push(format!("Files modified: {}", paths.join(", ")));
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
    /// Never splits between a tool call (assistant with tool_calls) and its tool results.
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

        // Find a safe cut point that doesn't split tool call/result pairs
        let cut = Self::find_safe_cut_point(&non_system, batch);

        if cut == 0 {
            return (system_msgs, Vec::new(), non_system);
        }

        let old_batch = non_system[..cut].to_vec();
        let recent = non_system[cut..].to_vec();

        (system_msgs, old_batch, recent)
    }

    /// Find a safe cut point that never splits between a tool call and its results.
    /// Starting from `target_index`, adjusts backward or forward to avoid orphaned
    /// tool calls or tool results.
    fn find_safe_cut_point(messages: &[Message], target_index: usize) -> usize {
        let target = target_index.min(messages.len());

        // If cutting right at a tool result, walk back to include the assistant tool call
        if target < messages.len() && messages[target].role == Role::Tool {
            let mut idx = target;
            // Walk back past all consecutive tool results
            while idx > 0 && messages[idx].role == Role::Tool {
                idx -= 1;
            }
            // If the message before the tool results is an assistant with tool_calls,
            // we must not cut between it and its results — cut before the assistant
            if messages[idx].role == Role::Assistant && !messages[idx].tool_calls.is_empty() {
                return idx;
            }
            // Otherwise just use the original target
        }

        // If the message just before the cut is an assistant with tool_calls,
        // its tool results would be in the "recent" side — include them in old_batch instead
        if target > 0
            && target < messages.len()
            && messages[target - 1].role == Role::Assistant
            && !messages[target - 1].tool_calls.is_empty()
        {
            let mut end = target;
            while end < messages.len() && messages[end].role == Role::Tool {
                end += 1;
            }
            return end;
        }

        target
    }
}

#[async_trait]
impl AsyncCondensationStrategy for SummarizationStrategy {
    fn name(&self) -> &'static str {
        "summarization"
    }

    fn set_previous_summary(&mut self, summary: Option<String>) {
        self.previous_summary = summary;
    }

    async fn condense(&self, messages: &[Message], max_tokens: usize) -> Result<Vec<Message>> {
        let (system_msgs, old_batch, recent) =
            Self::partition_messages(messages, self.batch_size, self.preserve_recent);

        // Nothing to summarize
        if old_batch.is_empty() {
            return Ok(messages.to_vec());
        }

        let prev = self.previous_summary.as_deref();

        // Try LLM summarization, fall back to heuristic
        let summary_text = if let Some(summarizer) = &self.summarizer {
            let formatted = Self::format_messages_for_prompt(&old_batch);
            let prompt = if let Some(previous) = prev {
                format!(
                    "You are summarizing a conversation. Here is the previous summary:\n\n\
                     {previous}\n\n\
                     Here are the new messages since that summary:\n\n\
                     {formatted}\n\n\
                     Update the summary to include the new information. Format:\n\
                     - Goal: what the user is trying to accomplish\n\
                     - Progress: what has been done so far (files read, changes made, tools used)\n\
                     - Current state: where things stand now\n\
                     Files read: <comma-separated list>\n\
                     Files modified: <comma-separated list>"
                )
            } else {
                format!(
                    "Summarize the following conversation concisely, preserving key decisions, \
                     file paths mentioned, and tool outcomes. Format:\n\
                     - Goal: what the user is trying to accomplish\n\
                     - Progress: what has been done so far (files read, changes made, tools used)\n\
                     - Current state: where things stand now\n\
                     Files read: <comma-separated list>\n\
                     Files modified: <comma-separated list>\n\n\
                     {formatted}"
                )
            };
            match summarizer.summarize(&prompt).await {
                Ok(summary) => summary,
                Err(_) => Self::heuristic_summary(&old_batch, prev),
            }
        } else {
            Self::heuristic_summary(&old_batch, prev)
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
        let summary = SummarizationStrategy::heuristic_summary(&messages, None);
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
        let summary = SummarizationStrategy::heuristic_summary(&messages, None);
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
        let summary = SummarizationStrategy::heuristic_summary(&messages, None);
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

    #[tokio::test]
    async fn iterative_heuristic_summary_builds_on_previous() {
        let previous =
            "Files read: /src/old.rs\nFiles modified: /src/changed.rs\n- Tools used: read";
        let messages = vec![
            Message::new(Role::User, "Now read /src/new.rs"),
            Message::new(Role::Assistant, "reading"),
        ];
        let summary = SummarizationStrategy::heuristic_summary(&messages, Some(previous));
        // Should contain files from both previous and new
        assert!(
            summary.contains("/src/old.rs"),
            "should preserve previous files_read"
        );
        assert!(
            summary.contains("/src/changed.rs"),
            "should preserve previous files_modified"
        );
        assert!(summary.contains("/src/new.rs"), "should include new file");
        assert!(
            summary.contains("[Updated summary"),
            "should indicate incremental update"
        );
    }

    #[tokio::test]
    async fn heuristic_summary_tracks_read_vs_modified() {
        let mut read_msg = Message::new(Role::Assistant, "reading file");
        read_msg.tool_calls.push(ToolCall {
            id: "call_1".to_string(),
            name: "read".to_string(),
            arguments: serde_json::json!({"file_path": "/src/lib.rs"}),
        });
        let mut write_msg = Message::new(Role::Assistant, "writing file");
        write_msg.tool_calls.push(ToolCall {
            id: "call_2".to_string(),
            name: "edit".to_string(),
            arguments: serde_json::json!({"file_path": "/src/main.rs"}),
        });
        let messages = vec![read_msg, write_msg];
        let summary = SummarizationStrategy::heuristic_summary(&messages, None);
        assert!(
            summary.contains("Files read: /src/lib.rs"),
            "read file should be in Files read"
        );
        assert!(
            summary.contains("Files modified: /src/main.rs"),
            "edited file should be in Files modified"
        );
    }

    #[tokio::test]
    async fn partition_never_splits_tool_call_from_result() {
        let tc = ToolCall {
            id: "call_1".to_string(),
            name: "read".to_string(),
            arguments: serde_json::json!({"path": "/tmp/file"}),
        };
        let messages = vec![
            Message::new(Role::System, "system"),
            Message::new(Role::User, "msg1"),
            Message::new(Role::User, "msg2"),
            Message::new(Role::Assistant, "calling tool").with_tool_calls(vec![tc]),
            Message::new(Role::Tool, "tool result").with_tool_call_id("call_1"),
            Message::new(Role::User, "msg5"),
        ];
        // batch_size=3 would normally cut at index 3 (non-system), which is the assistant
        // with tool_calls. The safe cut should include its tool result.
        let (_system, old, _recent) = SummarizationStrategy::partition_messages(&messages, 3, 1);
        // The old batch should include the assistant + tool result pair
        let has_assistant = old.iter().any(|m| m.role == Role::Assistant);
        let has_tool = old.iter().any(|m| m.role == Role::Tool);
        assert_eq!(
            has_assistant, has_tool,
            "assistant and tool must stay together"
        );
    }

    #[tokio::test]
    async fn partition_does_not_cut_at_orphaned_tool_result() {
        let tc = ToolCall {
            id: "call_1".to_string(),
            name: "read".to_string(),
            arguments: serde_json::json!({"path": "/tmp/file"}),
        };
        let messages = vec![
            Message::new(Role::System, "system"),
            Message::new(Role::User, "msg1"),
            Message::new(Role::Assistant, "calling tool").with_tool_calls(vec![tc]),
            Message::new(Role::Tool, "tool result").with_tool_call_id("call_1"),
            Message::new(Role::User, "msg4"),
            Message::new(Role::User, "msg5"),
        ];
        // batch_size=2 in non-system would target index 2, which is a Tool result.
        // Safe cut should walk back to before the assistant.
        let (_system, old, _recent) = SummarizationStrategy::partition_messages(&messages, 2, 2);
        // The tool result should not be orphaned from its assistant
        let has_assistant = old.iter().any(|m| m.role == Role::Assistant);
        let has_tool = old.iter().any(|m| m.role == Role::Tool);
        if has_tool {
            assert!(
                has_assistant,
                "tool result should not be separated from its assistant call"
            );
        }
    }

    #[tokio::test]
    async fn iterative_condense_with_previous_summary() {
        let mut strategy = SummarizationStrategy::new(None, 3, 1);
        strategy.set_previous_summary(Some(
            "Files read: /old/file.rs\n- Tools used: read".to_string(),
        ));
        let messages = vec![
            Message::new(Role::System, "system prompt"),
            Message::new(Role::User, "read /src/lib.rs"),
            Message::new(Role::Assistant, "done"),
            Message::new(Role::User, "read /src/main.rs"),
            Message::new(Role::Assistant, "done"),
            Message::new(Role::User, "what now?"),
        ];
        let result = strategy.condense(&messages, 10_000).await.unwrap();
        assert!(result.len() < messages.len());
        // Summary should be an updated summary building on the previous one
        let summary = &result[1].content;
        assert!(
            summary.contains("[Updated summary"),
            "should be an incremental update"
        );
        assert!(
            summary.contains("/old/file.rs"),
            "should preserve previous file tracking"
        );
    }
}
