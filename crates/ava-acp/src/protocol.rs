//! Agent SDK protocol types.
//!
//! These types model the Anthropic Agent SDK streaming event format:
//! - `AgentQuery`: parameters for starting an agent task
//! - `AgentMessage`: streaming events emitted during execution
//! - `AgentResult`: final result with cost/usage data
//!
//! Compatible with Claude Agent SDK stream-json output format.

use serde::{Deserialize, Serialize};
use serde_json::Value;

// ---------------------------------------------------------------------------
// Query (client → agent)
// ---------------------------------------------------------------------------

/// Parameters for starting an agent task.
#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct AgentQuery {
    pub prompt: String,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub system_prompt: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub working_directory: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub max_turns: Option<usize>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub permission_mode: Option<PermissionMode>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub allowed_tools: Option<Vec<String>>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub disallowed_tools: Option<Vec<String>>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub session_id: Option<String>,
    #[serde(default)]
    pub resume: bool,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub model: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub max_budget_usd: Option<f64>,
}

/// Permission mode for agent execution.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub enum PermissionMode {
    Default,
    AcceptEdits,
    Plan,
    BypassPermissions,
}

impl std::fmt::Display for PermissionMode {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Self::Default => write!(f, "default"),
            Self::AcceptEdits => write!(f, "acceptEdits"),
            Self::Plan => write!(f, "plan"),
            Self::BypassPermissions => write!(f, "bypassPermissions"),
        }
    }
}

// ---------------------------------------------------------------------------
// Messages (agent → client, streamed)
// ---------------------------------------------------------------------------

/// A streaming event from an agent.
///
/// Maps to the Anthropic Agent SDK stream-json event format.
#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
#[serde(tag = "type")]
pub enum AgentMessage {
    /// System-level message (init, session info, MCP status).
    #[serde(rename = "system")]
    System {
        #[serde(default)]
        message: String,
        #[serde(default)]
        session_id: Option<String>,
    },

    /// Assistant text content.
    #[serde(rename = "assistant")]
    Assistant {
        #[serde(default)]
        content: Vec<ContentBlock>,
        #[serde(default)]
        session_id: Option<String>,
    },

    /// Final result with cost and usage data.
    #[serde(rename = "result")]
    Result {
        result: String,
        #[serde(flatten)]
        details: AgentResultDetails,
    },

    /// Error from the agent.
    #[serde(rename = "error")]
    Error {
        message: String,
        #[serde(default)]
        code: Option<i32>,
    },

    /// Unknown event type (forward compatibility).
    #[serde(other)]
    Unknown,
}

/// A content block within an assistant message.
#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
#[serde(tag = "type")]
pub enum ContentBlock {
    #[serde(rename = "text")]
    Text { text: String },

    #[serde(rename = "tool_use")]
    ToolUse {
        id: String,
        name: String,
        #[serde(default)]
        input: Value,
    },

    #[serde(rename = "tool_result")]
    ToolResult {
        tool_use_id: String,
        #[serde(default)]
        content: String,
        #[serde(default)]
        is_error: bool,
    },

    #[serde(rename = "thinking")]
    Thinking { thinking: String },
}

/// Details included in a result message.
#[derive(Debug, Clone, Default, Serialize, Deserialize, PartialEq)]
#[serde(rename_all = "camelCase")]
pub struct AgentResultDetails {
    #[serde(default)]
    pub session_id: Option<String>,
    #[serde(default)]
    pub total_cost_usd: Option<f64>,
    #[serde(default)]
    pub usage: Option<AgentUsage>,
    /// "success", "error", "error_max_turns", "error_max_budget_usd"
    #[serde(default)]
    pub subtype: Option<String>,
}

/// Token usage from an agent execution.
#[derive(Debug, Clone, Default, Serialize, Deserialize, PartialEq)]
#[serde(rename_all = "camelCase")]
pub struct AgentUsage {
    #[serde(default)]
    pub input_tokens: u64,
    #[serde(default)]
    pub output_tokens: u64,
    #[serde(default)]
    pub cache_creation_input_tokens: Option<u64>,
    #[serde(default)]
    pub cache_read_input_tokens: Option<u64>,
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

impl AgentMessage {
    /// Extract text content from an assistant message.
    pub fn text(&self) -> Option<&str> {
        match self {
            Self::Assistant { content, .. } => content.iter().find_map(|b| match b {
                ContentBlock::Text { text } => Some(text.as_str()),
                _ => None,
            }),
            Self::Result { result, .. } => Some(result.as_str()),
            _ => None,
        }
    }

    /// Returns true if this is the final result message.
    pub fn is_result(&self) -> bool {
        matches!(self, Self::Result { .. })
    }

    /// Returns true if this is an error.
    pub fn is_error(&self) -> bool {
        matches!(self, Self::Error { .. })
    }
}

impl AgentQuery {
    /// Create a simple query with just a prompt.
    pub fn simple(prompt: impl Into<String>) -> Self {
        Self {
            prompt: prompt.into(),
            system_prompt: None,
            working_directory: None,
            max_turns: None,
            permission_mode: None,
            allowed_tools: None,
            disallowed_tools: None,
            session_id: None,
            resume: false,
            model: None,
            max_budget_usd: None,
        }
    }
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn agent_query_serde_roundtrip() {
        let query = AgentQuery {
            prompt: "fix the bug".into(),
            system_prompt: Some("you are helpful".into()),
            working_directory: Some("/home/user/project".into()),
            max_turns: Some(10),
            permission_mode: Some(PermissionMode::AcceptEdits),
            allowed_tools: Some(vec!["Read".into(), "Edit".into()]),
            disallowed_tools: None,
            session_id: None,
            resume: false,
            model: Some("opus".into()),
            max_budget_usd: Some(1.5),
        };
        let json = serde_json::to_string(&query).unwrap();
        let parsed: AgentQuery = serde_json::from_str(&json).unwrap();
        assert_eq!(parsed.prompt, "fix the bug");
        assert_eq!(parsed.max_turns, Some(10));
        assert_eq!(parsed.permission_mode, Some(PermissionMode::AcceptEdits));
        assert_eq!(parsed.max_budget_usd, Some(1.5));
    }

    #[test]
    fn agent_message_assistant_serde() {
        let json = r#"{"type":"assistant","content":[{"type":"text","text":"hello"},{"type":"thinking","thinking":"hmm"}]}"#;
        let msg: AgentMessage = serde_json::from_str(json).unwrap();
        assert_eq!(msg.text(), Some("hello"));
        if let AgentMessage::Assistant { content, .. } = &msg {
            assert_eq!(content.len(), 2);
            assert!(
                matches!(&content[1], ContentBlock::Thinking { thinking } if thinking == "hmm")
            );
        } else {
            panic!("expected Assistant");
        }
    }

    #[test]
    fn agent_message_result_serde() {
        let json = r#"{"type":"result","result":"done","sessionId":"abc","totalCostUsd":0.05,"subtype":"success"}"#;
        let msg: AgentMessage = serde_json::from_str(json).unwrap();
        assert!(msg.is_result());
        assert_eq!(msg.text(), Some("done"));
        if let AgentMessage::Result { details, .. } = &msg {
            assert_eq!(details.session_id.as_deref(), Some("abc"));
            assert_eq!(details.total_cost_usd, Some(0.05));
            assert_eq!(details.subtype.as_deref(), Some("success"));
        }
    }

    #[test]
    fn agent_message_tool_use_serde() {
        let json = r#"{"type":"assistant","content":[{"type":"tool_use","id":"t1","name":"Read","input":{"path":"/tmp/foo"}}]}"#;
        let msg: AgentMessage = serde_json::from_str(json).unwrap();
        if let AgentMessage::Assistant { content, .. } = &msg {
            assert!(matches!(&content[0], ContentBlock::ToolUse { name, .. } if name == "Read"));
        }
    }

    #[test]
    fn agent_message_error_serde() {
        let json = r#"{"type":"error","message":"rate limited","code":429}"#;
        let msg: AgentMessage = serde_json::from_str(json).unwrap();
        assert!(msg.is_error());
    }

    #[test]
    fn agent_message_unknown_is_forward_compatible() {
        let json = r#"{"type":"some_future_event","data":"whatever"}"#;
        let msg: AgentMessage = serde_json::from_str(json).unwrap();
        assert!(matches!(msg, AgentMessage::Unknown));
    }

    #[test]
    fn permission_mode_display() {
        assert_eq!(PermissionMode::AcceptEdits.to_string(), "acceptEdits");
        assert_eq!(
            PermissionMode::BypassPermissions.to_string(),
            "bypassPermissions"
        );
    }
}
