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
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct TokenUsage {
    pub input: u64,
    pub output: u64,
}

/// Parsed event from CLI agent stream-json output.
#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
#[serde(tag = "type")]
pub enum CLIAgentEvent {
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
}
