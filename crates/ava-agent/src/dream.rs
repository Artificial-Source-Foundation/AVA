//! Dream Agent — post-session memory consolidation.
//!
//! After a session ends (or on compaction/manual trigger), the dream agent
//! summarizes the conversation, extracts actionable memories, and stores
//! them in the persistent memory system for future sessions.

use ava_memory::MemorySystem;
use ava_types::Message;
use serde::{Deserialize, Serialize};
use tracing::{debug, info, warn};

/// When the dream agent should run.
#[derive(Debug, Clone, Default, Serialize, Deserialize, PartialEq, Eq)]
#[serde(rename_all = "snake_case")]
pub enum DreamTrigger {
    /// Run automatically when a session ends.
    #[default]
    OnSessionEnd,
    /// Run automatically after context compaction.
    OnCompaction,
    /// Only run when explicitly requested.
    Manual,
}

/// Configuration for the dream agent.
#[derive(Debug, Clone, Default, Serialize, Deserialize)]
pub struct DreamConfig {
    /// Whether dreaming is enabled.
    #[serde(default)]
    pub enabled: bool,
    /// Optional model override for the dream agent (uses session model if None).
    #[serde(default)]
    pub model: Option<String>,
    /// When to trigger dreaming.
    #[serde(default)]
    pub trigger: DreamTrigger,
}

/// Result of a dream agent run.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct DreamResult {
    /// Number of memories created during this dream.
    pub memories_created: usize,
    /// Human-readable summary of the session.
    pub summary: String,
}

/// The dream agent: summarizes sessions and extracts persistent memories.
pub struct DreamAgent {
    config: DreamConfig,
}

impl DreamAgent {
    pub fn new(config: DreamConfig) -> Self {
        Self { config }
    }

    /// Whether the dream agent is enabled.
    pub fn enabled(&self) -> bool {
        self.config.enabled
    }

    /// The configured trigger for this dream agent.
    pub fn trigger(&self) -> &DreamTrigger {
        &self.config.trigger
    }

    /// Run the dream agent on a completed session's messages.
    ///
    /// Extracts key decisions, files touched, patterns learned, and
    /// actionable memories, then stores them in the memory system.
    pub async fn run_dream(
        &self,
        session_messages: &[Message],
        memory_system: &MemorySystem,
    ) -> ava_types::Result<DreamResult> {
        if !self.config.enabled {
            return Ok(DreamResult {
                memories_created: 0,
                summary: "Dream agent is disabled".to_string(),
            });
        }

        if session_messages.is_empty() {
            return Ok(DreamResult {
                memories_created: 0,
                summary: "No messages to dream about".to_string(),
            });
        }

        info!(
            messages = session_messages.len(),
            "dream agent starting session analysis"
        );

        // Extract key information from the session
        let summary = self.summarize_session(session_messages);
        let memories = self.extract_memories(session_messages);

        let mut created = 0;
        for (key, value) in &memories {
            match memory_system.remember(key, value) {
                Ok(_) => {
                    created += 1;
                    debug!(key, "dream: memory stored");
                }
                Err(e) => {
                    warn!(key, error = %e, "dream: failed to store memory");
                }
            }
        }

        info!(
            memories_created = created,
            summary_len = summary.len(),
            "dream agent completed"
        );

        Ok(DreamResult {
            memories_created: created,
            summary,
        })
    }

    /// Build a concise summary of the session.
    fn summarize_session(&self, messages: &[Message]) -> String {
        let mut parts = Vec::new();

        // Count message types
        let user_msgs = messages
            .iter()
            .filter(|m| m.role == ava_types::Role::User)
            .count();
        let assistant_msgs = messages
            .iter()
            .filter(|m| m.role == ava_types::Role::Assistant)
            .count();
        let tool_msgs = messages
            .iter()
            .filter(|m| m.role == ava_types::Role::Tool)
            .count();

        parts.push(format!(
            "Session: {user_msgs} user, {assistant_msgs} assistant, {tool_msgs} tool messages"
        ));

        // Extract the initial goal (first user message)
        if let Some(first_user) = messages.iter().find(|m| m.role == ava_types::Role::User) {
            let goal_preview = if first_user.content.len() > 200 {
                format!("{}...", &first_user.content[..200])
            } else {
                first_user.content.clone()
            };
            parts.push(format!("Goal: {goal_preview}"));
        }

        // Extract files touched (from tool calls mentioning file paths)
        let files = self.extract_files_touched(messages);
        if !files.is_empty() {
            let file_list = files.into_iter().take(10).collect::<Vec<_>>().join(", ");
            parts.push(format!("Files touched: {file_list}"));
        }

        // Final assistant message as outcome
        if let Some(last_assistant) = messages
            .iter()
            .rev()
            .find(|m| m.role == ava_types::Role::Assistant)
        {
            let outcome = if last_assistant.content.len() > 300 {
                format!("{}...", &last_assistant.content[..300])
            } else {
                last_assistant.content.clone()
            };
            if !outcome.is_empty() {
                parts.push(format!("Outcome: {outcome}"));
            }
        }

        parts.join("\n")
    }

    /// Extract actionable memories from the session.
    ///
    /// Returns (key, value) pairs to store in the memory system.
    fn extract_memories(&self, messages: &[Message]) -> Vec<(String, String)> {
        let mut memories = Vec::new();

        // Memory 1: Session goal
        if let Some(first_user) = messages.iter().find(|m| m.role == ava_types::Role::User) {
            let goal = if first_user.content.len() > 500 {
                format!("{}...", &first_user.content[..500])
            } else {
                first_user.content.clone()
            };
            memories.push(("dream.last_goal".to_string(), goal));
        }

        // Memory 2: Files modified
        let files = self.extract_files_touched(messages);
        if !files.is_empty() {
            memories.push((
                "dream.files_touched".to_string(),
                files.into_iter().collect::<Vec<_>>().join(", "),
            ));
        }

        // Memory 3: Tool usage patterns
        let tool_names = self.extract_tool_usage(messages);
        if !tool_names.is_empty() {
            memories.push(("dream.tool_usage".to_string(), tool_names.join(", ")));
        }

        // Memory 4: Errors encountered
        let errors = self.extract_errors(messages);
        if !errors.is_empty() {
            let error_summary = errors.into_iter().take(5).collect::<Vec<_>>().join("; ");
            memories.push(("dream.errors_encountered".to_string(), error_summary));
        }

        memories
    }

    /// Extract file paths mentioned in tool call arguments.
    fn extract_files_touched(&self, messages: &[Message]) -> std::collections::BTreeSet<String> {
        let mut files = std::collections::BTreeSet::new();
        for msg in messages {
            for tc in &msg.tool_calls {
                // Look for file_path or path in tool arguments
                if let Some(obj) = tc.arguments.as_object() {
                    for key in &["file_path", "path", "filename"] {
                        if let Some(serde_json::Value::String(p)) = obj.get(*key) {
                            files.insert(p.clone());
                        }
                    }
                }
            }
        }
        files
    }

    /// Extract unique tool names used in the session.
    fn extract_tool_usage(&self, messages: &[Message]) -> Vec<String> {
        let mut names: std::collections::BTreeSet<String> = std::collections::BTreeSet::new();
        for msg in messages {
            for tc in &msg.tool_calls {
                names.insert(tc.name.clone());
            }
        }
        names.into_iter().collect()
    }

    /// Extract error messages from tool results.
    fn extract_errors(&self, messages: &[Message]) -> Vec<String> {
        let mut errors = Vec::new();
        for msg in messages {
            for result in &msg.tool_results {
                if result.is_error {
                    let preview = if result.content.len() > 200 {
                        format!("{}...", &result.content[..200])
                    } else {
                        result.content.clone()
                    };
                    errors.push(preview);
                }
            }
        }
        errors
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use ava_types::{Message, Role, ToolCall, ToolResult};
    use tempfile::TempDir;

    fn make_memory_system(dir: &TempDir) -> MemorySystem {
        let db = dir.path().join("dream_test.sqlite3");
        MemorySystem::new(&db).expect("memory system should initialize")
    }

    fn config_enabled() -> DreamConfig {
        DreamConfig {
            enabled: true,
            model: None,
            trigger: DreamTrigger::OnSessionEnd,
        }
    }

    fn config_disabled() -> DreamConfig {
        DreamConfig {
            enabled: false,
            model: None,
            trigger: DreamTrigger::Manual,
        }
    }

    #[tokio::test]
    async fn dream_extracts_summary() {
        let dir = TempDir::new().unwrap();
        let memory = make_memory_system(&dir);
        let agent = DreamAgent::new(config_enabled());

        let messages = vec![
            Message::new(Role::User, "Implement the login feature".to_string()),
            Message::new(
                Role::Assistant,
                "I'll implement the login feature for you.".to_string(),
            ),
            Message::new(Role::Assistant, "Done! Login is working.".to_string()),
        ];

        let result = agent.run_dream(&messages, &memory).await.unwrap();
        assert!(result.summary.contains("Goal: Implement the login feature"));
        assert!(result.summary.contains("Outcome: Done! Login is working."));
        assert!(result.memories_created > 0);
    }

    #[tokio::test]
    async fn config_controls_enablement() {
        let dir = TempDir::new().unwrap();
        let memory = make_memory_system(&dir);
        let agent = DreamAgent::new(config_disabled());

        let messages = vec![Message::new(Role::User, "Do something".to_string())];

        let result = agent.run_dream(&messages, &memory).await.unwrap();
        assert_eq!(result.memories_created, 0);
        assert!(result.summary.contains("disabled"));
    }

    #[tokio::test]
    async fn manual_trigger_works() {
        let dir = TempDir::new().unwrap();
        let memory = make_memory_system(&dir);
        let config = DreamConfig {
            enabled: true,
            model: Some("claude-haiku".to_string()),
            trigger: DreamTrigger::Manual,
        };
        let agent = DreamAgent::new(config);
        assert_eq!(agent.trigger(), &DreamTrigger::Manual);

        let messages = vec![
            Message::new(Role::User, "Fix the bug in auth.rs".to_string()),
            Message::new(Role::Assistant, "Fixed.".to_string()),
        ];

        let result = agent.run_dream(&messages, &memory).await.unwrap();
        assert!(result.memories_created > 0);

        // Verify memories were stored
        let recalled = memory.recall("dream.last_goal").unwrap();
        assert!(recalled.is_some());
        assert!(recalled.unwrap().value.contains("Fix the bug"));
    }

    #[tokio::test]
    async fn dream_extracts_tool_usage() {
        let dir = TempDir::new().unwrap();
        let memory = make_memory_system(&dir);
        let agent = DreamAgent::new(config_enabled());

        let mut msg = Message::new(Role::Assistant, "reading files".to_string());
        msg.tool_calls = vec![
            ToolCall {
                id: "1".to_string(),
                name: "read".to_string(),
                arguments: serde_json::json!({"file_path": "/src/main.rs"}),
            },
            ToolCall {
                id: "2".to_string(),
                name: "edit".to_string(),
                arguments: serde_json::json!({"file_path": "/src/lib.rs"}),
            },
        ];

        let messages = vec![Message::new(Role::User, "refactor".to_string()), msg];

        let result = agent.run_dream(&messages, &memory).await.unwrap();
        assert!(result.memories_created > 0);

        let tools = memory.recall("dream.tool_usage").unwrap().unwrap();
        assert!(tools.value.contains("edit"));
        assert!(tools.value.contains("read"));

        let files = memory.recall("dream.files_touched").unwrap().unwrap();
        assert!(files.value.contains("/src/main.rs"));
        assert!(files.value.contains("/src/lib.rs"));
    }

    #[tokio::test]
    async fn dream_extracts_errors() {
        let dir = TempDir::new().unwrap();
        let memory = make_memory_system(&dir);
        let agent = DreamAgent::new(config_enabled());

        let mut msg = Message::new(Role::Tool, "compilation failed".to_string());
        msg.tool_results = vec![ToolResult {
            call_id: "1".to_string(),
            content: "error[E0308]: mismatched types".to_string(),
            is_error: true,
        }];

        let messages = vec![Message::new(Role::User, "build".to_string()), msg];

        let _result = agent.run_dream(&messages, &memory).await.unwrap();
        let errors = memory.recall("dream.errors_encountered").unwrap();
        assert!(errors.is_some());
        assert!(errors.unwrap().value.contains("E0308"));
    }

    #[tokio::test]
    async fn empty_session_returns_zero_memories() {
        let dir = TempDir::new().unwrap();
        let memory = make_memory_system(&dir);
        let agent = DreamAgent::new(config_enabled());

        let result = agent.run_dream(&[], &memory).await.unwrap();
        assert_eq!(result.memories_created, 0);
        assert!(result.summary.contains("No messages"));
    }
}
