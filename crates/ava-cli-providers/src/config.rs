use serde::{Deserialize, Serialize};
use std::collections::HashMap;

/// Configuration for wrapping a coding agent CLI as an AVA provider.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct CLIAgentConfig {
    /// Unique provider name (e.g., "claude-code", "gemini-cli", "codex").
    pub name: String,
    /// CLI binary name or path (e.g., "claude", "gemini", "codex").
    pub binary: String,
    /// How to pass the prompt (e.g., "-p" for Claude, "exec" for Codex).
    pub prompt_flag: PromptMode,
    /// Flags for non-interactive mode.
    pub non_interactive_flags: Vec<String>,
    /// Flags to skip permission prompts.
    pub yolo_flags: Vec<String>,
    /// Flag for structured JSON output (if supported).
    pub output_format_flag: Option<String>,
    /// Flag to scope allowed tools (if supported).
    pub allowed_tools_flag: Option<String>,
    /// Flag to set working directory.
    pub cwd_flag: Option<String>,
    /// Flag to set model.
    pub model_flag: Option<String>,
    /// Flag to continue a session.
    pub session_flag: Option<String>,
    /// Whether this CLI supports structured JSON output.
    pub supports_stream_json: bool,
    /// Whether this CLI supports scoped tool permissions.
    pub supports_tool_scoping: bool,
    /// Default tool scoping per Praxis tier.
    pub tier_tool_scopes: Option<HashMap<String, Vec<String>>>,
    /// Command to detect if binary is installed.
    pub version_command: Vec<String>,

    // --- Agent SDK extensions ---
    /// Flag to set max turns (e.g., "--max-turns").
    #[serde(default)]
    pub max_turns_flag: Option<String>,
    /// Flag to set permission mode (e.g., "--permission-mode").
    #[serde(default)]
    pub permission_mode_flag: Option<String>,
    /// Flag to inject a system prompt (e.g., "--system-prompt").
    #[serde(default)]
    pub system_prompt_flag: Option<String>,
    /// Flag to resume a specific session (e.g., "--resume").
    #[serde(default)]
    pub resume_flag: Option<String>,
    /// Flag to continue the most recent session (e.g., "--continue").
    #[serde(default)]
    pub continue_flag: Option<String>,
    /// Flag to block specific tools (e.g., "--disallowedTools").
    #[serde(default)]
    pub disallowed_tools_flag: Option<String>,
    /// Whether this CLI emits Claude Agent SDK stream-json events (richer than generic).
    #[serde(default)]
    pub supports_agent_sdk_events: bool,
}

impl Default for CLIAgentConfig {
    fn default() -> Self {
        Self {
            name: String::new(),
            binary: String::new(),
            prompt_flag: PromptMode::Flag(String::new()),
            non_interactive_flags: vec![],
            yolo_flags: vec![],
            output_format_flag: None,
            allowed_tools_flag: None,
            cwd_flag: None,
            model_flag: None,
            session_flag: None,
            supports_stream_json: false,
            supports_tool_scoping: false,
            tier_tool_scopes: None,
            version_command: vec![],
            max_turns_flag: None,
            permission_mode_flag: None,
            system_prompt_flag: None,
            resume_flag: None,
            continue_flag: None,
            disallowed_tools_flag: None,
            supports_agent_sdk_events: false,
        }
    }
}

/// How the CLI accepts prompts.
#[derive(Debug, Clone, Serialize, Deserialize, PartialEq, Eq)]
pub enum PromptMode {
    /// Flag-based: binary <flag> "prompt" (e.g., claude -p "prompt").
    Flag(String),
    /// Subcommand-based: binary <subcmd> "prompt" (e.g., codex exec "prompt").
    Subcommand(String),
}

/// Result from a CLI agent execution.
#[derive(Debug, Clone)]
pub struct CLIAgentResult {
    pub success: bool,
    pub output: String,
    pub exit_code: i32,
    pub events: Vec<CLIAgentEvent>,
    pub tokens_used: Option<TokenUsage>,
    pub duration_ms: u64,
    /// Session ID returned by Agent SDK `result` event.
    pub session_id: Option<String>,
    /// Real cost in USD from Agent SDK `result` event.
    pub total_cost_usd: Option<f64>,
    /// Result subtype: "success", "error", "error_max_turns", "error_max_budget_usd".
    pub result_subtype: Option<String>,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct TokenUsage {
    pub input: u64,
    pub output: u64,
}

/// A content block within an Agent SDK `assistant` message.
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
        input: serde_json::Value,
    },
    #[serde(rename = "tool_result")]
    ToolResult {
        tool_use_id: String,
        #[serde(default)]
        content: String,
    },
    #[serde(rename = "thinking")]
    Thinking { thinking: String },
}

/// Token usage from an Agent SDK `result` message.
#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
pub struct SdkUsage {
    pub input_tokens: u64,
    pub output_tokens: u64,
    #[serde(default)]
    pub cache_creation_input_tokens: Option<u64>,
    #[serde(default)]
    pub cache_read_input_tokens: Option<u64>,
}

/// Parsed event from CLI agent stream-json output.
///
/// Supports both the generic stream-json format (text/tool_use/usage) and the
/// richer Claude Agent SDK format (assistant/result/system).
#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
#[serde(tag = "type")]
pub enum CLIAgentEvent {
    // --- Generic stream-json events (all agents) ---
    #[serde(rename = "text")]
    Text { content: String },
    #[serde(rename = "tool_use")]
    ToolUse {
        tool_name: String,
        #[serde(default)]
        tool_args: serde_json::Value,
    },
    #[serde(rename = "tool_result")]
    ToolResult { tool_name: String, result: String },
    #[serde(rename = "error")]
    Error { message: String },
    #[serde(rename = "usage")]
    Usage {
        input_tokens: u64,
        output_tokens: u64,
    },

    // --- Agent SDK events (Claude Code) ---
    /// An assistant message with structured content blocks.
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
        #[serde(default)]
        session_id: Option<String>,
        #[serde(default)]
        total_cost_usd: Option<f64>,
        #[serde(default)]
        usage: Option<SdkUsage>,
        #[serde(default)]
        subtype: Option<String>,
    },
    /// System-level message (init, MCP status, etc.).
    #[serde(rename = "system")]
    System {
        #[serde(default)]
        message: String,
        #[serde(default)]
        session_id: Option<String>,
    },

    #[serde(other)]
    Unknown,
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn config_serializes_and_deserializes() {
        let cfg = CLIAgentConfig {
            name: "claude-code".to_string(),
            binary: "claude".to_string(),
            prompt_flag: PromptMode::Flag("-p".to_string()),
            non_interactive_flags: vec!["--no-user-prompt".to_string()],
            yolo_flags: vec!["--dangerously-skip-permissions".to_string()],
            output_format_flag: Some("--output-format".to_string()),
            allowed_tools_flag: Some("--allowedTools".to_string()),
            cwd_flag: Some("--cwd".to_string()),
            model_flag: Some("--model".to_string()),
            session_flag: Some("--session-id".to_string()),
            supports_stream_json: true,
            supports_tool_scoping: true,
            tier_tool_scopes: Some(HashMap::from([(
                "engineer".to_string(),
                vec!["Read".to_string(), "Write".to_string()],
            )])),
            version_command: vec!["claude".to_string(), "--version".to_string()],
            ..Default::default()
        };

        let json = serde_json::to_string(&cfg).expect("serialize config");
        let back: CLIAgentConfig = serde_json::from_str(&json).expect("deserialize config");

        assert_eq!(back.name, "claude-code");
        assert!(back.supports_stream_json);
        assert_eq!(back.prompt_flag, PromptMode::Flag("-p".to_string()));
    }

    #[test]
    fn prompt_mode_variants() {
        let flag = PromptMode::Flag("-p".to_string());
        let sub = PromptMode::Subcommand("exec".to_string());

        assert_eq!(flag, PromptMode::Flag("-p".to_string()));
        assert_eq!(sub, PromptMode::Subcommand("exec".to_string()));
    }

    #[test]
    fn event_parses_from_json() {
        let text = r#"{"type":"text","content":"hello"}"#;
        let event: CLIAgentEvent = serde_json::from_str(text).expect("parse text event");
        assert_eq!(
            event,
            CLIAgentEvent::Text {
                content: "hello".to_string()
            }
        );

        let usage = r#"{"type":"usage","input_tokens":12,"output_tokens":7}"#;
        let event: CLIAgentEvent = serde_json::from_str(usage).expect("parse usage event");
        assert_eq!(
            event,
            CLIAgentEvent::Usage {
                input_tokens: 12,
                output_tokens: 7
            }
        );
    }

    #[test]
    fn sdk_assistant_event_parses() {
        let json = r#"{"type":"assistant","content":[{"type":"text","text":"Working on it..."}],"session_id":"sess-123"}"#;
        let event: CLIAgentEvent = serde_json::from_str(json).expect("parse assistant event");
        match event {
            CLIAgentEvent::Assistant {
                content,
                session_id,
            } => {
                assert_eq!(session_id, Some("sess-123".to_string()));
                assert_eq!(content.len(), 1);
                assert!(
                    matches!(&content[0], ContentBlock::Text { text } if text == "Working on it...")
                );
            }
            _ => panic!("expected Assistant event"),
        }
    }

    #[test]
    fn sdk_result_event_parses() {
        let json = r#"{"type":"result","result":"Done.","session_id":"sess-123","total_cost_usd":0.05,"usage":{"input_tokens":100,"output_tokens":50},"subtype":"success"}"#;
        let event: CLIAgentEvent = serde_json::from_str(json).expect("parse result event");
        match event {
            CLIAgentEvent::Result {
                result,
                session_id,
                total_cost_usd,
                usage,
                subtype,
            } => {
                assert_eq!(result, "Done.");
                assert_eq!(session_id, Some("sess-123".to_string()));
                assert_eq!(total_cost_usd, Some(0.05));
                assert_eq!(subtype, Some("success".to_string()));
                let u = usage.unwrap();
                assert_eq!(u.input_tokens, 100);
                assert_eq!(u.output_tokens, 50);
            }
            _ => panic!("expected Result event"),
        }
    }

    #[test]
    fn sdk_system_event_parses() {
        let json = r#"{"type":"system","message":"Initializing...","session_id":"sess-456"}"#;
        let event: CLIAgentEvent = serde_json::from_str(json).expect("parse system event");
        assert!(matches!(
            event,
            CLIAgentEvent::System {
                message,
                session_id
            } if message == "Initializing..." && session_id == Some("sess-456".to_string())
        ));
    }

    #[test]
    fn existing_events_still_parse() {
        // Backward compatibility: old events work unchanged
        let tool = r#"{"type":"tool_use","tool_name":"Read","tool_args":{"path":"x"}}"#;
        let event: CLIAgentEvent = serde_json::from_str(tool).expect("parse tool_use");
        assert!(matches!(event, CLIAgentEvent::ToolUse { tool_name, .. } if tool_name == "Read"));

        let err = r#"{"type":"error","message":"oops"}"#;
        let event: CLIAgentEvent = serde_json::from_str(err).expect("parse error");
        assert!(matches!(event, CLIAgentEvent::Error { message } if message == "oops"));
    }
}
