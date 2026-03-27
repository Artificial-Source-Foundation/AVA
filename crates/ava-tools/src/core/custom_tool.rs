use std::path::{Path, PathBuf};
use std::sync::Arc;

use async_trait::async_trait;
use ava_permissions::classifier::classify_bash_command;
use ava_permissions::tags::RiskLevel;
use ava_plugin::{HookEvent, PluginManager};
use ava_types::{AvaError, Result, ToolResult};
use serde::Deserialize;
use serde_json::Value;
use tracing::{info, warn};

use crate::registry::{Tool, ToolRegistry, ToolSource};

const MAX_OUTPUT_BYTES: usize = 100 * 1024;

/// A tool defined via a TOML file.
#[derive(Debug, Clone, Deserialize)]
pub struct CustomToolDef {
    pub name: String,
    pub description: String,
    #[serde(default)]
    pub params: Vec<ParamDef>,
    pub execution: ExecutionDef,
}

#[derive(Debug, Clone, Deserialize)]
pub struct ParamDef {
    pub name: String,
    #[serde(rename = "type", default = "default_param_type")]
    pub param_type: String,
    #[serde(default)]
    pub required: bool,
    #[serde(default)]
    pub description: String,
}

fn default_param_type() -> String {
    "string".to_string()
}

#[derive(Debug, Clone, Deserialize)]
#[serde(tag = "type")]
pub enum ExecutionDef {
    #[serde(rename = "shell")]
    Shell {
        command: String,
        #[serde(default)]
        timeout_secs: Option<u64>,
    },
    #[serde(rename = "script")]
    Script {
        interpreter: String,
        script: String,
        #[serde(default)]
        timeout_secs: Option<u64>,
    },
}

/// Runtime wrapper implementing the `Tool` trait.
pub struct CustomTool {
    def: CustomToolDef,
    source_path: String,
    plugin_manager: Option<Arc<tokio::sync::Mutex<PluginManager>>>,
}

impl CustomTool {
    pub fn new(
        def: CustomToolDef,
        source_path: String,
        plugin_manager: Option<Arc<tokio::sync::Mutex<PluginManager>>>,
    ) -> Self {
        Self {
            def,
            source_path,
            plugin_manager,
        }
    }

    fn build_parameters_schema(&self) -> Value {
        let mut properties = serde_json::Map::new();
        let mut required = Vec::new();

        for param in &self.def.params {
            let mut prop = serde_json::Map::new();
            prop.insert("type".to_string(), Value::String(param.param_type.clone()));
            if !param.description.is_empty() {
                prop.insert(
                    "description".to_string(),
                    Value::String(param.description.clone()),
                );
            }
            properties.insert(param.name.clone(), Value::Object(prop));
            if param.required {
                required.push(Value::String(param.name.clone()));
            }
        }

        let mut schema = serde_json::Map::new();
        schema.insert("type".to_string(), Value::String("object".to_string()));
        schema.insert("properties".to_string(), Value::Object(properties));
        if !required.is_empty() {
            schema.insert("required".to_string(), Value::Array(required));
        }
        Value::Object(schema)
    }

    /// Shell-escape a value by single-quoting it, escaping any internal single quotes.
    fn shell_escape(s: &str) -> String {
        format!("'{}'", s.replace('\'', "'\\''"))
    }

    /// Substitute `{{param_name}}` placeholders in a command string with argument values.
    ///
    /// All substituted values are shell-escaped to prevent command injection.
    fn substitute_args(template: &str, args: &Value) -> String {
        let mut result = template.to_string();
        if let Some(obj) = args.as_object() {
            for (key, value) in obj {
                let placeholder = format!("{{{{{key}}}}}");
                let raw = match value {
                    Value::String(s) => s.clone(),
                    other => other.to_string(),
                };
                let escaped = Self::shell_escape(&raw);
                result = result.replace(&placeholder, &escaped);
            }
        }
        result
    }

    /// Source path for this tool definition file.
    pub fn source_path(&self) -> &str {
        &self.source_path
    }

    async fn plugin_env_vars(&self) -> Vec<(String, String)> {
        let Some(pm) = &self.plugin_manager else {
            return Vec::new();
        };
        let responses = pm
            .lock()
            .await
            .trigger_hook(HookEvent::ShellEnv, serde_json::json!({}))
            .await;
        let mut vars = Vec::new();
        for response in responses {
            if response.error.is_some() {
                warn!(
                    plugin = response.plugin_name,
                    "shell.env hook error for custom tool: {:?}", response.error
                );
                continue;
            }
            if let Some(map) = response.result.as_object() {
                for (key, value) in map {
                    if let Some(value) = value.as_str() {
                        vars.push((key.clone(), value.to_string()));
                    }
                }
            }
        }
        vars
    }
}

#[async_trait]
impl Tool for CustomTool {
    fn name(&self) -> &str {
        &self.def.name
    }

    fn description(&self) -> &str {
        &self.def.description
    }

    fn parameters(&self) -> Value {
        self.build_parameters_schema()
    }

    async fn execute(&self, args: Value) -> Result<ToolResult> {
        let timeout = match &self.def.execution {
            ExecutionDef::Shell { timeout_secs, .. } => timeout_secs.unwrap_or(30),
            ExecutionDef::Script { timeout_secs, .. } => timeout_secs.unwrap_or(30),
        };

        let (cmd, cmd_args) = match &self.def.execution {
            ExecutionDef::Shell { command, .. } => {
                let expanded = Self::substitute_args(command, &args);
                ("sh".to_string(), vec!["-c".to_string(), expanded])
            }
            ExecutionDef::Script {
                interpreter,
                script,
                ..
            } => {
                let expanded = Self::substitute_args(script, &args);
                (interpreter.clone(), vec!["-c".to_string(), expanded])
            }
        };

        // SEC-1: Route custom tool commands through the bash command classifier.
        // Custom tools execute arbitrary shell commands and must be subject to
        // the same blocked-command checks as the built-in bash tool.
        let effective_command = cmd_args.last().cloned().unwrap_or_default();
        let classification = classify_bash_command(&effective_command);
        if classification.blocked {
            let reason = classification
                .reason
                .unwrap_or_else(|| "Blocked command".to_string());
            warn!(
                tool = %self.def.name,
                command = %effective_command,
                reason = %reason,
                "Custom tool command blocked by permission classifier"
            );
            return Err(AvaError::PermissionDenied(format!(
                "Custom tool '{}' blocked: {reason}",
                self.def.name
            )));
        }
        if classification.risk_level >= RiskLevel::Critical {
            warn!(
                tool = %self.def.name,
                command = %effective_command,
                risk = ?classification.risk_level,
                "Custom tool command has critical risk level"
            );
            return Err(AvaError::PermissionDenied(format!(
                "Custom tool '{}' denied: command has critical risk level",
                self.def.name
            )));
        }

        let mut env = super::bash::filtered_env();
        env.extend(self.plugin_env_vars().await);

        let output = tokio::time::timeout(std::time::Duration::from_secs(timeout), async {
            let mut command = tokio::process::Command::new(&cmd);
            command
                .args(&cmd_args)
                .kill_on_drop(true)
                .env_clear()
                .envs(env);
            if let Ok(workspace_root) = super::path_guard::workspace_root() {
                command.current_dir(workspace_root);
            }
            command.output().await
        })
        .await
        .map_err(|_| {
            AvaError::ToolError(format!(
                "Custom tool '{}' timed out after {timeout}s",
                self.def.name
            ))
        })?
        .map_err(|e| AvaError::ToolError(format!("Failed to execute '{}': {e}", self.def.name)))?;

        let stdout = String::from_utf8_lossy(&output.stdout);
        let stderr = String::from_utf8_lossy(&output.stderr);
        let content = format!(
            "stdout:\n{}\n\nstderr:\n{}\n\nexit_code: {}",
            stdout,
            stderr,
            output.status.code().unwrap_or(-1)
        );
        let content = super::output_fallback::save_tool_output_fallback_tail(
            &format!("custom-{}", self.def.name),
            &content,
            MAX_OUTPUT_BYTES,
        );
        let content = super::secret_redaction::redact_secrets(&content);

        Ok(ToolResult {
            call_id: format!("custom-{}", self.def.name),
            content,
            is_error: !output.status.success(),
        })
    }
}

/// Load custom tool definitions from a directory of `.toml` files.
pub fn load_custom_tools(dir: &Path) -> Vec<(CustomToolDef, String)> {
    let mut tools = Vec::new();
    let Ok(entries) = std::fs::read_dir(dir) else {
        return tools;
    };

    for entry in entries.flatten() {
        let path = entry.path();
        if path.extension().is_some_and(|ext| ext == "toml") {
            match load_tool_file(&path) {
                Ok(def) => {
                    info!(tool = %def.name, path = %path.display(), "Loaded custom tool");
                    tools.push((def, path.display().to_string()));
                }
                Err(e) => {
                    warn!(path = %path.display(), error = %e, "Failed to load custom tool");
                }
            }
        }
    }

    tools
}

fn load_tool_file(path: &Path) -> Result<CustomToolDef> {
    let contents = std::fs::read_to_string(path)
        .map_err(|e| AvaError::IoError(format!("failed to read {}: {e}", path.display())))?;
    let def: CustomToolDef = toml::from_str(&contents)
        .map_err(|e| AvaError::ConfigError(format!("invalid tool TOML {}: {e}", path.display())))?;
    Ok(def)
}

/// Register all custom tools from a directory into the registry.
///
/// Skips tools whose name collides with an already-registered tool to prevent
/// shadowing built-in, MCP, or previously loaded custom tools.
pub fn register_custom_tools(registry: &mut ToolRegistry, dirs: &[PathBuf]) {
    register_custom_tools_with_plugins(registry, dirs, None);
}

pub fn register_custom_tools_with_plugins(
    registry: &mut ToolRegistry,
    dirs: &[PathBuf],
    plugin_manager: Option<Arc<tokio::sync::Mutex<PluginManager>>>,
) {
    for dir in dirs {
        let tools = load_custom_tools(dir);
        for (def, path) in tools {
            let tool_name = &def.name;
            if registry.tool_source(tool_name).is_some() {
                warn!(
                    tool = %tool_name,
                    path = %path,
                    "Custom tool would shadow existing tool — skipping"
                );
                continue;
            }
            let source = ToolSource::Custom { path: path.clone() };
            registry
                .register_with_source(CustomTool::new(def, path, plugin_manager.clone()), source);
        }
    }
}

/// Create template TOML tool files in the given directory.
pub fn create_tool_templates(dir: &Path) -> Result<Vec<PathBuf>> {
    std::fs::create_dir_all(dir)
        .map_err(|e| AvaError::IoError(format!("failed to create {}: {e}", dir.display())))?;

    let templates = [
        ("hello.toml", TEMPLATE_HELLO),
        ("git-stats.toml", TEMPLATE_GIT_STATS),
        ("file-count.toml", TEMPLATE_FILE_COUNT),
    ];

    let mut created = Vec::new();
    for (filename, content) in &templates {
        let path = dir.join(filename);
        if !path.exists() {
            std::fs::write(&path, content).map_err(|e| {
                AvaError::IoError(format!("failed to write {}: {e}", path.display()))
            })?;
            created.push(path);
        }
    }

    Ok(created)
}

const TEMPLATE_HELLO: &str = r#"name = "hello"
description = "A simple greeting tool — edit this template!"

[[params]]
name = "name"
type = "string"
required = true
description = "Name to greet"

[execution]
type = "shell"
command = "echo 'Hello, {{name}}!'"
timeout_secs = 5
"#;

const TEMPLATE_GIT_STATS: &str = r#"name = "git_stats"
description = "Show git repository statistics"

[execution]
type = "shell"
command = "echo '=== Commits ===' && git log --oneline -10 && echo '\n=== Status ===' && git status --short"
timeout_secs = 10
"#;

const TEMPLATE_FILE_COUNT: &str = r#"name = "file_count"
description = "Count files matching a pattern"

[[params]]
name = "pattern"
type = "string"
required = true
description = "Glob pattern to match (e.g. '*.rs')"

[execution]
type = "shell"
command = "find . -name '{{pattern}}' -type f | wc -l"
timeout_secs = 10
"#;

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn parse_shell_tool() {
        let toml_str = r#"
name = "test_tool"
description = "A test tool"

[[params]]
name = "input"
type = "string"
required = true
description = "Input value"

[execution]
type = "shell"
command = "echo {{input}}"
timeout_secs = 5
"#;
        let def: CustomToolDef = toml::from_str(toml_str).unwrap();
        assert_eq!(def.name, "test_tool");
        assert_eq!(def.params.len(), 1);
        assert!(def.params[0].required);
        match &def.execution {
            ExecutionDef::Shell {
                command,
                timeout_secs,
            } => {
                assert_eq!(command, "echo {{input}}");
                assert_eq!(*timeout_secs, Some(5));
            }
            _ => panic!("expected shell execution"),
        }
    }

    #[test]
    fn parse_script_tool() {
        let toml_str = r#"
name = "script_tool"
description = "A script tool"

[execution]
type = "script"
interpreter = "python3"
script = "print('hello')"
"#;
        let def: CustomToolDef = toml::from_str(toml_str).unwrap();
        match &def.execution {
            ExecutionDef::Script {
                interpreter,
                script,
                timeout_secs,
            } => {
                assert_eq!(interpreter, "python3");
                assert_eq!(script, "print('hello')");
                assert!(timeout_secs.is_none());
            }
            _ => panic!("expected script execution"),
        }
    }

    #[test]
    fn substitute_args_replaces_placeholders() {
        let template = "echo {{name}} is {{age}} years old";
        let args = serde_json::json!({"name": "Alice", "age": 30});
        let result = CustomTool::substitute_args(template, &args);
        assert_eq!(result, "echo 'Alice' is '30' years old");
    }

    #[test]
    fn substitute_args_escapes_injection() {
        let template = "echo {{input}}";
        let args = serde_json::json!({"input": "'; rm -rf / #"});
        let result = CustomTool::substitute_args(template, &args);
        // The single quote in the input gets escaped, making the value safe
        // shell_escape("'; rm -rf / #") wraps in single quotes with internal ' escaped
        let escaped_value = CustomTool::shell_escape("'; rm -rf / #");
        assert_eq!(result, format!("echo {escaped_value}"));
        // Verify the escaped value does NOT allow breaking out of quotes
        assert_ne!(escaped_value, "'; rm -rf / #");
        assert!(escaped_value.starts_with('\''));
        assert!(escaped_value.ends_with('\''));
    }

    #[test]
    fn shell_escape_handles_single_quotes() {
        assert_eq!(CustomTool::shell_escape("hello"), "'hello'");
        assert_eq!(CustomTool::shell_escape("it's"), "'it'\\''s'");
        assert_eq!(CustomTool::shell_escape(""), "''");
    }

    #[tokio::test]
    async fn custom_tool_executes_shell() {
        let def = CustomToolDef {
            name: "echo_test".to_string(),
            description: "test".to_string(),
            params: vec![ParamDef {
                name: "msg".to_string(),
                param_type: "string".to_string(),
                required: true,
                description: String::new(),
            }],
            execution: ExecutionDef::Shell {
                command: "echo {{msg}}".to_string(),
                timeout_secs: Some(5),
            },
        };
        let tool = CustomTool::new(def, "test.toml".to_string(), None);
        let result = tool
            .execute(serde_json::json!({"msg": "hello"}))
            .await
            .unwrap();
        assert!(result.content.contains("stdout:\nhello"));
        assert!(result.content.contains("exit_code: 0"));
        assert!(!result.is_error);
    }

    #[tokio::test]
    async fn custom_tool_formats_output_and_redacts_secrets() {
        let def = CustomToolDef {
            name: "secret_test".to_string(),
            description: "test".to_string(),
            params: vec![],
            execution: ExecutionDef::Shell {
                command:
                    "printf 'sk-proj-abcdefghijklmnopqrstuvwxyz123456'; printf 'warn' >&2; exit 7"
                        .to_string(),
                timeout_secs: Some(5),
            },
        };
        let tool = CustomTool::new(def, "secret.toml".to_string(), None);
        let result = tool.execute(serde_json::json!({})).await.unwrap();

        assert!(result.is_error);
        assert!(result.content.contains("stdout:"));
        assert!(result.content.contains("stderr:\nwarn"));
        assert!(result.content.contains("exit_code: 7"));
        assert!(result.content.contains("[REDACTED]"));
        assert!(!result
            .content
            .contains("sk-proj-abcdefghijklmnopqrstuvwxyz123456"));
    }

    #[tokio::test]
    async fn custom_tool_large_output_spills_to_disk() {
        let def = CustomToolDef {
            name: "large_output".to_string(),
            description: "test".to_string(),
            params: vec![],
            execution: ExecutionDef::Shell {
                command: "printf '%*s' 120000 '' | tr ' ' x".to_string(),
                timeout_secs: Some(5),
            },
        };
        let tool = CustomTool::new(def, "large.toml".to_string(), None);
        let result = tool.execute(serde_json::json!({})).await.unwrap();

        assert!(result.content.contains("full output saved to"));
    }

    #[test]
    fn create_templates_in_temp_dir() {
        let dir = tempfile::tempdir().unwrap();
        let created = create_tool_templates(dir.path()).unwrap();
        assert_eq!(created.len(), 3);

        // Second call should create nothing (files exist)
        let created2 = create_tool_templates(dir.path()).unwrap();
        assert!(created2.is_empty());
    }

    #[tokio::test]
    async fn custom_tool_blocks_dangerous_commands() {
        // A custom tool that tries to run `sudo rm -rf /` should be blocked
        let def = CustomToolDef {
            name: "evil_tool".to_string(),
            description: "test".to_string(),
            params: vec![],
            execution: ExecutionDef::Shell {
                command: "sudo rm -rf /".to_string(),
                timeout_secs: Some(5),
            },
        };
        let tool = CustomTool::new(def, "evil.toml".to_string(), None);
        let result = tool.execute(serde_json::json!({})).await;
        assert!(result.is_err(), "dangerous command should be blocked");
        let err = result.unwrap_err().to_string();
        assert!(
            err.contains("blocked") || err.contains("denied"),
            "error should mention blocked/denied: {err}"
        );
    }

    #[tokio::test]
    async fn custom_tool_blocks_fork_bomb() {
        let def = CustomToolDef {
            name: "fork_bomb".to_string(),
            description: "test".to_string(),
            params: vec![],
            execution: ExecutionDef::Shell {
                command: ":(){ :|:& };:".to_string(),
                timeout_secs: Some(5),
            },
        };
        let tool = CustomTool::new(def, "bomb.toml".to_string(), None);
        let result = tool.execute(serde_json::json!({})).await;
        assert!(result.is_err(), "fork bomb should be blocked");
    }

    #[tokio::test]
    async fn custom_tool_blocks_injected_dangerous_args() {
        // Even with shell-escaped args, the underlying command template could be dangerous
        let def = CustomToolDef {
            name: "rm_tool".to_string(),
            description: "test".to_string(),
            params: vec![ParamDef {
                name: "target".to_string(),
                param_type: "string".to_string(),
                required: true,
                description: String::new(),
            }],
            execution: ExecutionDef::Shell {
                command: "sudo rm -rf {{target}}".to_string(),
                timeout_secs: Some(5),
            },
        };
        let tool = CustomTool::new(def, "rm.toml".to_string(), None);
        let result = tool.execute(serde_json::json!({"target": "/tmp"})).await;
        assert!(result.is_err(), "sudo rm -rf should be blocked");
    }

    #[test]
    fn missing_execution_returns_config_error() {
        let toml_str = r#"
name = "broken_tool"
description = "Missing execution section"

[[params]]
name = "input"
type = "string"
required = true
"#;
        let result = toml::from_str::<CustomToolDef>(toml_str);
        assert!(
            result.is_err(),
            "TOML without [execution] should fail to parse"
        );
    }

    #[test]
    fn load_custom_tools_from_dir() {
        let dir = tempfile::tempdir().unwrap();
        std::fs::write(
            dir.path().join("test.toml"),
            r#"
name = "test"
description = "test tool"
[execution]
type = "shell"
command = "echo hi"
"#,
        )
        .unwrap();

        let tools = load_custom_tools(dir.path());
        assert_eq!(tools.len(), 1);
        assert_eq!(tools[0].0.name, "test");
    }
}
