use serde::Deserialize;
use std::collections::HashMap;
use std::path::{Path, PathBuf};
use tracing::debug;

/// A user-defined slash command loaded from a TOML file.
#[derive(Debug, Clone, Deserialize)]
pub struct CustomCommand {
    pub name: String,
    #[serde(default)]
    pub description: String,
    pub prompt: String,
    #[serde(default)]
    pub allowed_tools: Vec<String>,
    #[serde(default)]
    pub params: Vec<CommandParam>,
    /// Where the command was loaded from (not part of the TOML schema).
    #[serde(skip)]
    pub source: CommandSource,
}

/// A parameter that users can pass to a custom command.
#[derive(Debug, Clone, Deserialize)]
pub struct CommandParam {
    pub name: String,
    #[serde(default)]
    pub description: String,
    #[serde(default)]
    pub required: bool,
    #[serde(default)]
    pub default: Option<String>,
}

/// Indicates where a custom command was loaded from.
#[derive(Debug, Clone, Default, PartialEq, Eq)]
pub enum CommandSource {
    /// Project-local: `.ava/commands/`
    #[default]
    Project,
    /// User-global: `~/.ava/commands/`
    Global,
}

impl CommandSource {
    pub fn label(&self) -> &'static str {
        match self {
            Self::Project => "project",
            Self::Global => "global",
        }
    }
}

/// Registry of user-defined slash commands loaded from TOML files.
#[derive(Debug, Clone, Default)]
pub struct CustomCommandRegistry {
    pub commands: Vec<CustomCommand>,
}

impl CustomCommandRegistry {
    /// Load custom commands from both global (`~/.ava/commands/`) and project
    /// (`.ava/commands/`) directories. Project commands override global ones
    /// with the same name.
    pub fn load() -> Self {
        let mut commands = Vec::new();

        // Load from ~/.ava/commands/*.toml (global)
        if let Some(home) = dirs::home_dir() {
            let global_dir = home.join(".ava").join("commands");
            Self::load_from_dir(&global_dir, CommandSource::Global, &mut commands);
        }

        // Load from .ava/commands/*.toml (project — overrides global)
        let project_dir = PathBuf::from(".ava").join("commands");
        Self::load_from_dir(&project_dir, CommandSource::Project, &mut commands);

        debug!(count = commands.len(), "loaded custom commands");
        Self { commands }
    }

    fn load_from_dir(dir: &Path, source: CommandSource, commands: &mut Vec<CustomCommand>) {
        let entries = match std::fs::read_dir(dir) {
            Ok(entries) => entries,
            Err(_) => return, // Directory doesn't exist — that's fine
        };

        for entry in entries.flatten() {
            let path = entry.path();
            if path.extension().and_then(|e| e.to_str()) != Some("toml") {
                continue;
            }

            match std::fs::read_to_string(&path) {
                Ok(content) => match toml::from_str::<CustomCommand>(&content) {
                    Ok(mut cmd) => {
                        cmd.source = source.clone();
                        // Sanitize name: strip leading '/' if present, lowercase
                        cmd.name = cmd.name.trim_start_matches('/').to_lowercase();
                        if cmd.name.is_empty() {
                            debug!(path = %path.display(), "skipping command with empty name");
                            continue;
                        }
                        // Project commands override global ones with same name
                        if source == CommandSource::Project {
                            commands.retain(|c| c.name != cmd.name);
                        } else if commands.iter().any(|c| c.name == cmd.name) {
                            // Global command shadowed by existing project command — skip
                            continue;
                        }
                        debug!(name = %cmd.name, source = source.label(), path = %path.display(), "loaded custom command");
                        commands.push(cmd);
                    }
                    Err(err) => {
                        debug!(path = %path.display(), error = %err, "failed to parse custom command");
                    }
                },
                Err(err) => {
                    debug!(path = %path.display(), error = %err, "failed to read custom command file");
                }
            }
        }
    }

    /// Look up a command by name (case-insensitive).
    pub fn find(&self, name: &str) -> Option<&CustomCommand> {
        let name_lower = name.to_lowercase();
        self.commands.iter().find(|c| c.name == name_lower)
    }

    /// Resolve a command's prompt template with the given argument string.
    ///
    /// Arguments are parsed as `key=value` pairs separated by whitespace.
    /// Bare words without `=` are treated as positional arguments, mapped to
    /// parameters in declaration order.
    ///
    /// `{{param_name}}` placeholders in the prompt are replaced with values.
    /// Missing required parameters return an error string.
    pub fn resolve_prompt(cmd: &CustomCommand, args: &str) -> Result<String, String> {
        let parsed = Self::parse_args(args, &cmd.params);
        let mut prompt = cmd.prompt.clone();

        // Check for missing required parameters
        for param in &cmd.params {
            let value = parsed.get(&param.name);
            if param.required && value.is_none() && param.default.is_none() {
                return Err(format!(
                    "Missing required parameter: {} ({})",
                    param.name, param.description
                ));
            }
        }

        // Replace {{param}} placeholders
        for param in &cmd.params {
            let placeholder = format!("{{{{{}}}}}", param.name);
            let value = parsed
                .get(&param.name)
                .cloned()
                .or_else(|| param.default.clone())
                .unwrap_or_default();
            prompt = prompt.replace(&placeholder, &value);
        }

        Ok(prompt)
    }

    /// Parse an argument string into a name→value map.
    ///
    /// Supports `key=value` pairs and positional args (mapped to params in order).
    fn parse_args(args: &str, params: &[CommandParam]) -> HashMap<String, String> {
        let mut map = HashMap::new();
        let mut positional_index = 0;

        if args.trim().is_empty() {
            return map;
        }

        // Split on whitespace, but respect quoted strings
        for token in Self::tokenize(args) {
            if let Some((key, value)) = token.split_once('=') {
                map.insert(key.to_lowercase(), value.to_string());
            } else if positional_index < params.len() {
                map.insert(params[positional_index].name.clone(), token);
                positional_index += 1;
            }
        }

        map
    }

    /// Simple tokenizer that splits on whitespace but respects double-quoted strings.
    fn tokenize(input: &str) -> Vec<String> {
        let mut tokens = Vec::new();
        let mut current = String::new();
        let mut in_quotes = false;

        for ch in input.chars() {
            match ch {
                '"' => {
                    in_quotes = !in_quotes;
                }
                ' ' | '\t' if !in_quotes => {
                    if !current.is_empty() {
                        tokens.push(std::mem::take(&mut current));
                    }
                }
                _ => {
                    current.push(ch);
                }
            }
        }

        if !current.is_empty() {
            tokens.push(current);
        }

        tokens
    }

    /// Create sample command files in `.ava/commands/`.
    pub fn create_templates() -> Result<String, String> {
        let dir = PathBuf::from(".ava").join("commands");
        std::fs::create_dir_all(&dir)
            .map_err(|e| format!("Failed to create .ava/commands/: {e}"))?;

        let example_path = dir.join("example.toml");
        if example_path.exists() {
            return Err("File already exists: .ava/commands/example.toml".to_string());
        }

        let content = r#"name = "review"
description = "Review code changes for issues"
prompt = """
Review the current git diff for:
- Security vulnerabilities
- Performance issues
- Code style violations
- Missing error handling

Focus area: {{focus}}

Provide a structured report with severity levels.
"""

# Optional: restrict which tools the agent can use
# allowed_tools = ["read", "glob", "grep", "bash"]

# Optional: parameters that users can pass
[[params]]
name = "focus"
description = "Area to focus the review on"
required = false
default = "all"
"#;

        std::fs::write(&example_path, content)
            .map_err(|e| format!("Failed to write example command: {e}"))?;

        Ok(format!("Created {}", example_path.display()))
    }

    /// Reload all commands from disk.
    pub fn reload(&mut self) {
        *self = Self::load();
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn sample_command() -> CustomCommand {
        CustomCommand {
            name: "review".to_string(),
            description: "Review code".to_string(),
            prompt: "Review code with focus on {{focus}} at severity {{severity}}".to_string(),
            allowed_tools: vec![],
            params: vec![
                CommandParam {
                    name: "focus".to_string(),
                    description: "Focus area".to_string(),
                    required: false,
                    default: Some("all".to_string()),
                },
                CommandParam {
                    name: "severity".to_string(),
                    description: "Min severity".to_string(),
                    required: false,
                    default: Some("low".to_string()),
                },
            ],
            source: CommandSource::Project,
        }
    }

    #[test]
    fn resolve_prompt_with_defaults() {
        let cmd = sample_command();
        let result = CustomCommandRegistry::resolve_prompt(&cmd, "").unwrap();
        assert_eq!(result, "Review code with focus on all at severity low");
    }

    #[test]
    fn resolve_prompt_with_named_args() {
        let cmd = sample_command();
        let result =
            CustomCommandRegistry::resolve_prompt(&cmd, "focus=security severity=high").unwrap();
        assert_eq!(
            result,
            "Review code with focus on security at severity high"
        );
    }

    #[test]
    fn resolve_prompt_with_positional_args() {
        let cmd = sample_command();
        let result =
            CustomCommandRegistry::resolve_prompt(&cmd, "security high").unwrap();
        assert_eq!(
            result,
            "Review code with focus on security at severity high"
        );
    }

    #[test]
    fn resolve_prompt_mixed_args() {
        let cmd = sample_command();
        let result =
            CustomCommandRegistry::resolve_prompt(&cmd, "security severity=critical").unwrap();
        assert_eq!(
            result,
            "Review code with focus on security at severity critical"
        );
    }

    #[test]
    fn resolve_prompt_missing_required() {
        let cmd = CustomCommand {
            name: "deploy".to_string(),
            description: "Deploy".to_string(),
            prompt: "Deploy to {{target}}".to_string(),
            allowed_tools: vec![],
            params: vec![CommandParam {
                name: "target".to_string(),
                description: "Deploy target".to_string(),
                required: true,
                default: None,
            }],
            source: CommandSource::Project,
        };
        let result = CustomCommandRegistry::resolve_prompt(&cmd, "");
        assert!(result.is_err());
        assert!(result.unwrap_err().contains("target"));
    }

    #[test]
    fn resolve_prompt_no_params() {
        let cmd = CustomCommand {
            name: "lint".to_string(),
            description: "Lint code".to_string(),
            prompt: "Run linting on the current project".to_string(),
            allowed_tools: vec![],
            params: vec![],
            source: CommandSource::Global,
        };
        let result = CustomCommandRegistry::resolve_prompt(&cmd, "").unwrap();
        assert_eq!(result, "Run linting on the current project");
    }

    #[test]
    fn tokenize_quoted_strings() {
        let tokens = CustomCommandRegistry::tokenize(r#"focus="security and performance" severity=high"#);
        assert_eq!(tokens, vec!["focus=security and performance", "severity=high"]);
    }

    #[test]
    fn find_case_insensitive() {
        let registry = CustomCommandRegistry {
            commands: vec![sample_command()],
        };
        assert!(registry.find("review").is_some());
        assert!(registry.find("Review").is_some());
        assert!(registry.find("REVIEW").is_some());
        assert!(registry.find("nonexistent").is_none());
    }

    #[test]
    fn source_label() {
        assert_eq!(CommandSource::Project.label(), "project");
        assert_eq!(CommandSource::Global.label(), "global");
    }
}
