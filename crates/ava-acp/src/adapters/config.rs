//! Declarative agent configuration.
//!
//! Replaces the 20+ field `CLIAgentConfig` with a simple, protocol-aware config.

use serde::{Deserialize, Serialize};

pub const NESTING_GUARD_ENV_VARS: [&str; 4] = [
    "CLAUDECODE",
    "CLAUDE_CODE_ENTRYPOINT",
    "CLAUDE_CODE_SESSION",
    "CLAUDE_CODE_PARENT_SESSION",
];

/// Protocol spoken by the agent.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum AgentProtocol {
    /// Anthropic Agent SDK stream-json format (Claude Code).
    SdkV1,
    /// Codex JSONL event stream.
    CodexJsonl,
    /// OpenCode JSONL event stream.
    OpenCodeJsonl,
    /// Gemini CLI stream-json JSONL event stream.
    GeminiCliJsonl,
    /// Plain text output (Aider).
    PlainText,
}

/// Declarative configuration for an external agent.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct AgentConfig {
    /// Unique name (e.g., "claude-code", "codex").
    pub name: String,
    /// Binary to invoke (e.g., "claude", "codex").
    pub binary: String,
    /// Protocol the agent speaks.
    pub protocol: AgentProtocol,
    /// Arguments to pass for non-interactive headless mode.
    #[serde(default)]
    pub headless_args: Vec<String>,
    /// Extra arguments to append after prompt/model/session flags.
    #[serde(default)]
    pub trailing_args: Vec<String>,
    /// Flag to pass the prompt (e.g., "-p" or positioned as last arg).
    #[serde(default)]
    pub prompt_flag: Option<String>,
    /// Flag to set model (e.g., "--model").
    #[serde(default)]
    pub model_flag: Option<String>,
    /// Flag to set working directory (e.g., "--cwd").
    #[serde(default)]
    pub cwd_flag: Option<String>,
    /// Flag to resume or attach to a session (e.g. "--resume" or "--session").
    #[serde(default)]
    pub session_flag: Option<String>,
    /// Flag to set max turns (e.g., "--max-turns").
    #[serde(default)]
    pub max_turns_flag: Option<String>,
    /// Flag to set permission mode (e.g., "--permission-mode").
    #[serde(default)]
    pub permission_mode_flag: Option<String>,
    /// Flag to set cost budget (e.g. "--max-budget-usd").
    #[serde(default)]
    pub max_budget_flag: Option<String>,
    /// Command to check if the agent is installed (e.g., ["claude", "--version"]).
    #[serde(default)]
    pub version_command: Vec<String>,
}

/// Built-in agent configurations for known CLI agents.
pub fn builtin_agents() -> Vec<AgentConfig> {
    vec![
        AgentConfig {
            name: "claude-code".into(),
            binary: "claude".into(),
            protocol: AgentProtocol::SdkV1,
            headless_args: vec![
                "--output-format".into(),
                "stream-json".into(),
                "--verbose".into(),
            ],
            trailing_args: vec![],
            prompt_flag: Some("-p".into()),
            model_flag: Some("--model".into()),
            cwd_flag: None, // claude uses cwd of the process
            session_flag: Some("--resume".into()),
            max_turns_flag: Some("--max-turns".into()),
            permission_mode_flag: Some("--permission-mode".into()),
            max_budget_flag: Some("--max-budget-usd".into()),
            version_command: vec!["claude".into(), "--version".into()],
        },
        AgentConfig {
            name: "codex".into(),
            binary: "codex".into(),
            protocol: AgentProtocol::CodexJsonl,
            headless_args: vec!["exec".into(), "--json".into()],
            trailing_args: vec![],
            prompt_flag: None, // codex uses subcommand: codex exec "prompt"
            model_flag: Some("--model".into()),
            cwd_flag: None,
            session_flag: None,
            max_turns_flag: None,
            permission_mode_flag: None,
            max_budget_flag: None,
            version_command: vec!["codex".into(), "--version".into()],
        },
        AgentConfig {
            name: "aider".into(),
            binary: "aider".into(),
            protocol: AgentProtocol::PlainText,
            headless_args: vec!["--yes-always".into(), "--no-git".into()],
            trailing_args: vec![],
            prompt_flag: Some("--message".into()),
            model_flag: Some("--model".into()),
            cwd_flag: None,
            session_flag: None,
            max_turns_flag: None,
            permission_mode_flag: None,
            max_budget_flag: None,
            version_command: vec!["aider".into(), "--version".into()],
        },
        AgentConfig {
            name: "gemini-cli".into(),
            binary: "gemini".into(),
            protocol: AgentProtocol::GeminiCliJsonl,
            headless_args: vec!["--output-format".into(), "stream-json".into()],
            trailing_args: vec![],
            prompt_flag: Some("-p".into()),
            model_flag: Some("--model".into()),
            cwd_flag: None,
            session_flag: Some("--resume".into()),
            max_turns_flag: None,
            permission_mode_flag: Some("--approval-mode".into()),
            max_budget_flag: None,
            version_command: vec!["gemini".into(), "--version".into()],
        },
        AgentConfig {
            name: "opencode".into(),
            binary: "opencode".into(),
            protocol: AgentProtocol::OpenCodeJsonl,
            headless_args: vec!["run".into(), "--format".into(), "json".into()],
            trailing_args: vec![],
            prompt_flag: None, // opencode run "prompt"
            model_flag: Some("--model".into()),
            cwd_flag: Some("--dir".into()),
            session_flag: Some("--session".into()),
            max_turns_flag: None,
            permission_mode_flag: None,
            max_budget_flag: None,
            version_command: vec!["opencode".into(), "--version".into()],
        },
    ]
}

pub fn builtin_agent(name: &str) -> Option<AgentConfig> {
    builtin_agents()
        .into_iter()
        .find(|agent| agent.name == name)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn builtin_agents_are_valid() {
        let agents = builtin_agents();
        assert_eq!(agents.len(), 5);
        assert!(agents.iter().any(|a| a.name == "claude-code"));
        assert!(agents.iter().any(|a| a.name == "codex"));
        assert!(agents.iter().any(|a| a.name == "aider"));
    }

    #[test]
    fn agent_config_serde_roundtrip() {
        let config = &builtin_agents()[0];
        let json = serde_json::to_string(config).unwrap();
        let parsed: AgentConfig = serde_json::from_str(&json).unwrap();
        assert_eq!(parsed.name, "claude-code");
        assert_eq!(parsed.protocol, AgentProtocol::SdkV1);
    }
}
