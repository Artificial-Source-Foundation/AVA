use crate::config::{CLIAgentConfig, PromptMode};
use std::collections::HashMap;

/// Get all built-in CLI agent configs.
pub fn builtin_configs() -> HashMap<String, CLIAgentConfig> {
    let mut configs = HashMap::new();
    configs.insert("claude-code".into(), claude_code_config());
    configs.insert("gemini-cli".into(), gemini_cli_config());
    configs.insert("codex".into(), codex_config());
    configs.insert("opencode".into(), opencode_config());
    configs.insert("aider".into(), aider_config());
    configs
}

fn claude_code_config() -> CLIAgentConfig {
    CLIAgentConfig {
        name: "claude-code".into(),
        binary: "claude".into(),
        prompt_flag: PromptMode::Flag("-p".into()),
        non_interactive_flags: vec!["--no-user-prompt".into()],
        yolo_flags: vec!["--dangerously-skip-permissions".into()],
        output_format_flag: Some("--output-format".into()),
        allowed_tools_flag: Some("--allowedTools".into()),
        cwd_flag: Some("--cwd".into()),
        model_flag: Some("--model".into()),
        session_flag: Some("--session-id".into()),
        supports_stream_json: true,
        supports_tool_scoping: true,
        tier_tool_scopes: Some(HashMap::from([
            (
                "engineer".into(),
                ["Edit", "Write", "Bash", "Read", "Glob", "Grep"]
                    .into_iter()
                    .map(String::from)
                    .collect(),
            ),
            (
                "reviewer".into(),
                ["Read", "Bash", "Glob", "Grep"]
                    .into_iter()
                    .map(String::from)
                    .collect(),
            ),
            (
                "subagent".into(),
                ["Read", "Glob", "Grep"]
                    .into_iter()
                    .map(String::from)
                    .collect(),
            ),
        ])),
        version_command: vec!["claude".into(), "--version".into()],
    }
}

fn gemini_cli_config() -> CLIAgentConfig {
    CLIAgentConfig {
        name: "gemini-cli".into(),
        binary: "gemini".into(),
        prompt_flag: PromptMode::Flag("-p".into()),
        non_interactive_flags: vec![],
        yolo_flags: vec!["--yolo".into()],
        output_format_flag: None,
        allowed_tools_flag: None,
        cwd_flag: Some("--cwd".into()),
        model_flag: Some("--model".into()),
        session_flag: None,
        supports_stream_json: false,
        supports_tool_scoping: false,
        tier_tool_scopes: None,
        version_command: vec!["gemini".into(), "--version".into()],
    }
}

fn codex_config() -> CLIAgentConfig {
    CLIAgentConfig {
        name: "codex".into(),
        binary: "codex".into(),
        prompt_flag: PromptMode::Subcommand("exec".into()),
        non_interactive_flags: vec![],
        yolo_flags: vec!["--full-auto".into()],
        output_format_flag: Some("--json".into()),
        allowed_tools_flag: None,
        cwd_flag: Some("--cwd".into()),
        model_flag: Some("--model".into()),
        session_flag: Some("--session".into()),
        supports_stream_json: true,
        supports_tool_scoping: false,
        tier_tool_scopes: None,
        version_command: vec!["codex".into(), "--version".into()],
    }
}

fn opencode_config() -> CLIAgentConfig {
    CLIAgentConfig {
        name: "opencode".into(),
        binary: "opencode".into(),
        prompt_flag: PromptMode::Subcommand("run".into()),
        non_interactive_flags: vec![],
        yolo_flags: vec![],
        output_format_flag: None,
        allowed_tools_flag: None,
        cwd_flag: Some("--cwd".into()),
        model_flag: Some("--model".into()),
        session_flag: Some("--session".into()),
        supports_stream_json: false,
        supports_tool_scoping: false,
        tier_tool_scopes: None,
        version_command: vec!["opencode".into(), "--version".into()],
    }
}

fn aider_config() -> CLIAgentConfig {
    CLIAgentConfig {
        name: "aider".into(),
        binary: "aider".into(),
        prompt_flag: PromptMode::Flag("--message".into()),
        non_interactive_flags: vec![],
        yolo_flags: vec!["--yes-always".into()],
        output_format_flag: None,
        allowed_tools_flag: None,
        cwd_flag: Some("--cwd".into()),
        model_flag: Some("--model".into()),
        session_flag: None,
        supports_stream_json: false,
        supports_tool_scoping: false,
        tier_tool_scopes: None,
        version_command: vec!["aider".into(), "--version".into()],
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn all_configs_have_required_fields() {
        let configs = builtin_configs();
        assert_eq!(configs.len(), 5);
        for config in configs.values() {
            assert!(!config.name.is_empty());
            assert!(!config.binary.is_empty());
            assert!(!config.version_command.is_empty());
        }
    }

    #[test]
    fn binaries_match_expected() {
        let configs = builtin_configs();
        assert_eq!(configs["claude-code"].binary, "claude");
        assert_eq!(configs["gemini-cli"].binary, "gemini");
        assert_eq!(configs["codex"].binary, "codex");
        assert_eq!(configs["opencode"].binary, "opencode");
        assert_eq!(configs["aider"].binary, "aider");
    }

    #[test]
    fn claude_supports_stream_json_and_scoping() {
        let configs = builtin_configs();
        let claude = &configs["claude-code"];
        assert!(claude.supports_stream_json);
        assert!(claude.supports_tool_scoping);
    }

    #[test]
    fn codex_uses_subcommand_prompt_mode() {
        let configs = builtin_configs();
        assert!(matches!(
            configs["codex"].prompt_flag,
            PromptMode::Subcommand(_)
        ));
    }

    #[test]
    fn aider_uses_message_flag_prompt_mode() {
        let configs = builtin_configs();
        assert_eq!(
            configs["aider"].prompt_flag,
            PromptMode::Flag("--message".to_string())
        );
    }
}
