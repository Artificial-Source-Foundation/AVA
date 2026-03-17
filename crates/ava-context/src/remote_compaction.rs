//! Remote compaction via API (BG2-19, inspired by Codex CLI).
//!
//! Delegates context compaction to a remote LLM endpoint instead of local
//! summarization. Falls back to local strategies if the API is unavailable.

use ava_types::Message;

use crate::error::Result;
use crate::strategies::{AsyncCondensationStrategy, Summarizer};

/// Remote compaction strategy that uses an LLM API for summarization.
///
/// When context pressure is detected, sends the conversation history to
/// a remote summarization endpoint and receives compacted messages back.
pub struct RemoteCompactionStrategy {
    summarizer: Option<std::sync::Arc<dyn Summarizer>>,
    /// Maximum chars to send for summarization (avoids huge payloads).
    max_payload_chars: usize,
    /// Number of recent messages to protect from compaction.
    preserve_recent: usize,
}

impl RemoteCompactionStrategy {
    pub fn new(summarizer: Option<std::sync::Arc<dyn Summarizer>>, preserve_recent: usize) -> Self {
        Self {
            summarizer,
            max_payload_chars: 100_000,
            preserve_recent,
        }
    }

    /// Build a compact text representation of messages for summarization.
    fn messages_to_text(messages: &[Message]) -> String {
        let mut text = String::new();
        for msg in messages {
            let role = match msg.role {
                ava_types::Role::System => "system",
                ava_types::Role::User => "user",
                ava_types::Role::Assistant => "assistant",
                ava_types::Role::Tool => "tool",
            };
            if !msg.content.is_empty() {
                text.push_str(&format!("[{role}]: {}\n", msg.content));
            }
            for tc in &msg.tool_calls {
                text.push_str(&format!("[tool_call]: {}({})\n", tc.name, tc.id));
            }
            for tr in &msg.tool_results {
                let preview = if tr.content.len() > 200 {
                    format!("{}...", &tr.content[..200])
                } else {
                    tr.content.clone()
                };
                text.push_str(&format!("[tool_result]: {preview}\n"));
            }
        }
        text
    }
}

#[async_trait::async_trait]
impl AsyncCondensationStrategy for RemoteCompactionStrategy {
    fn name(&self) -> &'static str {
        "remote_compaction"
    }

    async fn condense(&self, messages: &[Message], _target_tokens: usize) -> Result<Vec<Message>> {
        let Some(summarizer) = &self.summarizer else {
            // No summarizer available — pass through unchanged
            return Ok(messages.to_vec());
        };

        if messages.len() <= self.preserve_recent + 1 {
            return Ok(messages.to_vec());
        }

        // Split into compactable prefix and protected suffix
        let boundary = messages.len().saturating_sub(self.preserve_recent);

        // Find system message to preserve
        let (system_msgs, compactable_refs): (Vec<_>, Vec<_>) = messages[..boundary]
            .iter()
            .partition(|m| m.role == ava_types::Role::System);

        let compactable: Vec<_> = compactable_refs.into_iter().cloned().collect();
        let text = Self::messages_to_text(&compactable);

        // Truncate if too large
        let payload = if text.len() > self.max_payload_chars {
            format!(
                "{}...\n[truncated from {} chars]",
                &text[..self.max_payload_chars],
                text.len()
            )
        } else {
            text
        };

        let prompt = format!(
            "Summarize this conversation history concisely, preserving key decisions, \
             file paths mentioned, and actions taken. Focus on what was accomplished \
             and any pending work:\n\n{payload}"
        );

        match summarizer.summarize(&prompt).await {
            Ok(summary) => {
                let mut result = Vec::new();
                // Keep system messages
                for msg in &system_msgs {
                    result.push((*msg).clone());
                }
                // Add summary as a system message
                result.push(Message::new(
                    ava_types::Role::System,
                    format!("[Conversation summary]: {summary}"),
                ));
                // Keep protected recent messages
                result.extend_from_slice(&messages[boundary..]);
                Ok(result)
            }
            Err(_) => {
                // Fallback: return messages unchanged on API failure
                tracing::warn!("Remote compaction failed, passing through unchanged");
                Ok(messages.to_vec())
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use ava_types::{Message, Role};

    #[tokio::test]
    async fn no_summarizer_passes_through() {
        let strategy = RemoteCompactionStrategy::new(None, 2);
        let messages = vec![
            Message::new(Role::User, "hello"),
            Message::new(Role::Assistant, "hi"),
            Message::new(Role::User, "bye"),
        ];
        let result = strategy.condense(&messages, 100).await.unwrap();
        assert_eq!(result.len(), 3);
    }

    #[tokio::test]
    async fn too_few_messages_passes_through() {
        let strategy = RemoteCompactionStrategy::new(None, 5);
        let messages = vec![
            Message::new(Role::User, "hello"),
            Message::new(Role::Assistant, "hi"),
        ];
        let result = strategy.condense(&messages, 100).await.unwrap();
        assert_eq!(result.len(), 2);
    }

    #[test]
    fn messages_to_text_formats_correctly() {
        let messages = vec![
            Message::new(Role::User, "Fix the bug"),
            Message::new(Role::Assistant, "I'll look at the code"),
        ];
        let text = RemoteCompactionStrategy::messages_to_text(&messages);
        assert!(text.contains("[user]: Fix the bug"));
        assert!(text.contains("[assistant]: I'll look at the code"));
    }

    struct MockSummarizer;

    #[async_trait::async_trait]
    impl Summarizer for MockSummarizer {
        async fn summarize(&self, _text: &str) -> std::result::Result<String, String> {
            Ok("Summary: user asked to fix a bug, assistant fixed it.".to_string())
        }
    }

    #[tokio::test]
    async fn summarizer_compacts_old_messages() {
        let summarizer = std::sync::Arc::new(MockSummarizer);
        let strategy = RemoteCompactionStrategy::new(Some(summarizer), 2);

        let messages = vec![
            Message::new(Role::System, "You are a helpful assistant"),
            Message::new(Role::User, "Fix the login bug"),
            Message::new(Role::Assistant, "I found the issue in auth.rs"),
            Message::new(Role::User, "Great, now add tests"),
            Message::new(Role::Assistant, "Done, all tests pass"),
        ];

        let result = strategy.condense(&messages, 100).await.unwrap();
        // System + summary + 2 protected recent = 4
        assert_eq!(result.len(), 4);
        assert_eq!(result[0].role, Role::System);
        assert!(result[1].content.contains("[Conversation summary]"));
        assert_eq!(result[2].content, "Great, now add tests");
        assert_eq!(result[3].content, "Done, all tests pass");
    }

    struct FailingSummarizer;

    #[async_trait::async_trait]
    impl Summarizer for FailingSummarizer {
        async fn summarize(&self, _text: &str) -> std::result::Result<String, String> {
            Err("API error".to_string())
        }
    }

    #[tokio::test]
    async fn fallback_on_api_failure() {
        let summarizer = std::sync::Arc::new(FailingSummarizer);
        let strategy = RemoteCompactionStrategy::new(Some(summarizer), 2);

        let messages = vec![
            Message::new(Role::User, "hello"),
            Message::new(Role::Assistant, "hi"),
            Message::new(Role::User, "test"),
            Message::new(Role::Assistant, "ok"),
        ];

        let result = strategy.condense(&messages, 100).await.unwrap();
        assert_eq!(result.len(), 4); // unchanged on failure
    }
}
