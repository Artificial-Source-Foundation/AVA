use super::events::HookEvent;
use serde::Deserialize;
use std::collections::HashMap;
use std::path::{Path, PathBuf};
use tracing::debug;

/// A single hook definition loaded from a TOML file.
#[derive(Debug, Clone, Deserialize)]
pub struct HookConfig {
    /// The event this hook listens for (e.g., "PostToolUse").
    pub event: String,
    /// Human-readable description of what this hook does.
    #[serde(default)]
    pub description: Option<String>,
    /// Regex/glob pattern for tool names (for tool lifecycle events).
    /// Matched against the tool name with `|` as OR separator.
    /// Example: "edit|write|multiedit|apply_patch"
    #[serde(default)]
    pub matcher: Option<String>,
    /// Glob pattern for file paths. If set, the hook only fires when the
    /// tool operates on a file matching this pattern.
    /// Example: "*.rs" or "src/**/*.ts"
    #[serde(default)]
    pub path_pattern: Option<String>,
    /// Priority (lower = runs first). Default: 100.
    #[serde(default = "default_priority")]
    pub priority: i32,
    /// Whether the hook is active. Default: true.
    #[serde(default = "default_enabled")]
    pub enabled: bool,
    /// The action to execute when this hook fires.
    pub action: HookAction,
    /// Source path of the TOML file (not part of the schema).
    #[serde(skip)]
    pub source: HookSource,
}

fn default_priority() -> i32 {
    100
}

fn default_enabled() -> bool {
    true
}

/// The action a hook performs when triggered.
#[derive(Debug, Clone, Deserialize)]
#[serde(tag = "type")]
pub enum HookAction {
    /// Run a shell command. Exit code 0 = success, exit code 2 = block
    /// (for PreToolUse), any other = error (logged, doesn't block).
    #[serde(rename = "command")]
    Command {
        /// The shell command to execute (via `sh -c`).
        command: String,
        /// Timeout in seconds. Default: 30.
        #[serde(default = "default_timeout")]
        timeout: u64,
        /// Working directory. Default: project root (cwd).
        #[serde(default)]
        cwd: Option<String>,
    },
    /// POST event context as JSON to an HTTP endpoint.
    #[serde(rename = "http")]
    Http {
        /// The URL to POST to.
        url: String,
        /// Optional HTTP headers.
        #[serde(default)]
        headers: HashMap<String, String>,
        /// Timeout in seconds. Default: 10.
        #[serde(default = "default_http_timeout")]
        timeout: u64,
    },
    /// Ask the LLM a yes/no question to decide whether to allow the action.
    #[serde(rename = "prompt")]
    Prompt {
        /// The prompt to send to the LLM.
        prompt: String,
    },
}

fn default_timeout() -> u64 {
    30
}

fn default_http_timeout() -> u64 {
    10
}

/// Where a hook was loaded from.
#[derive(Debug, Clone, Default, PartialEq, Eq)]
pub enum HookSource {
    /// Project-local: `.ava/hooks/`
    #[default]
    Project,
    /// User-global: `~/.ava/hooks/`
    Global,
}

impl HookSource {
    pub fn label(&self) -> &'static str {
        match self {
            Self::Project => "project",
            Self::Global => "global",
        }
    }
}

/// Registry of loaded hooks, providing lookup by event and matching logic.
#[derive(Debug, Clone, Default)]
pub struct HookRegistry {
    pub(crate) hooks: Vec<HookConfig>,
}

impl HookRegistry {
    /// Load hooks from both global (`~/.ava/hooks/`) and project (`.ava/hooks/`)
    /// directories. Project hooks take precedence on name collisions (by filename).
    pub fn load() -> Self {
        let mut hooks = Vec::new();

        // Load global hooks first
        if let Some(home) = dirs::home_dir() {
            let global_dir = home.join(".ava").join("hooks");
            Self::load_from_dir(&global_dir, HookSource::Global, &mut hooks);
        }

        // Load project hooks (can shadow globals with same filename)
        let project_dir = PathBuf::from(".ava").join("hooks");
        Self::load_from_dir(&project_dir, HookSource::Project, &mut hooks);

        debug!(count = hooks.len(), "loaded hooks");
        Self { hooks }
    }

    fn load_from_dir(dir: &Path, source: HookSource, hooks: &mut Vec<HookConfig>) {
        let Ok(entries) = std::fs::read_dir(dir) else {
            return; // Directory doesn't exist — that's fine
        };

        for entry in entries.flatten() {
            let path = entry.path();
            if path.extension().and_then(|e| e.to_str()) != Some("toml") {
                continue;
            }

            match std::fs::read_to_string(&path) {
                Ok(content) => match toml::from_str::<HookConfig>(&content) {
                    Ok(mut hook) => {
                        // Validate the event field
                        if HookEvent::from_str_loose(&hook.event).is_none() {
                            debug!(
                                path = %path.display(),
                                event = %hook.event,
                                "skipping hook with unrecognized event"
                            );
                            continue;
                        }
                        hook.source = source.clone();
                        debug!(
                            event = %hook.event,
                            source = source.label(),
                            path = %path.display(),
                            priority = hook.priority,
                            "loaded hook"
                        );
                        hooks.push(hook);
                    }
                    Err(err) => {
                        debug!(path = %path.display(), error = %err, "failed to parse hook file");
                    }
                },
                Err(err) => {
                    debug!(path = %path.display(), error = %err, "failed to read hook file");
                }
            }
        }
    }

    /// Reload all hooks from disk.
    pub fn reload(&mut self) {
        *self = Self::load();
    }

    /// Return all enabled hooks for a given event, sorted by priority (ascending).
    pub fn hooks_for_event(&self, event: &HookEvent) -> Vec<&HookConfig> {
        let event_label = event.label();
        let mut matching: Vec<&HookConfig> = self
            .hooks
            .iter()
            .filter(|h| {
                h.enabled
                    && HookEvent::from_str_loose(&h.event)
                        .map(|e| e == *event)
                        .unwrap_or_else(|| h.event.eq_ignore_ascii_case(event_label))
            })
            .collect();
        matching.sort_by_key(|h| h.priority);
        matching
    }

    /// Check whether a hook matches the given context based on its
    /// `matcher` (tool name pattern) and `path_pattern` (file path glob).
    pub fn matches(hook: &HookConfig, context: &super::events::HookContext) -> bool {
        // Check tool name matcher
        if let Some(ref matcher) = hook.matcher {
            let Some(tool_name) = context.tool_name.as_deref() else {
                return false; // matcher set but no tool name — skip
            };
            let patterns: Vec<&str> = matcher.split('|').collect();
            if !patterns.iter().any(|p| {
                let p = p.trim();
                if p.contains('*') || p.contains('?') {
                    // Simple glob matching
                    glob_match(p, tool_name)
                } else {
                    p.eq_ignore_ascii_case(tool_name)
                }
            }) {
                return false;
            }
        }

        // Check file path pattern
        if let Some(ref pattern) = hook.path_pattern {
            let Some(file_path) = context.file_path.as_deref() else {
                return false; // path_pattern set but no file_path — skip
            };
            if !glob_match(pattern, file_path) {
                return false;
            }
        }

        true
    }

    /// Total number of loaded hooks (enabled and disabled).
    pub fn len(&self) -> usize {
        self.hooks.len()
    }

    /// Whether the registry has no hooks.
    pub fn is_empty(&self) -> bool {
        self.hooks.is_empty()
    }

    /// Iterate over all hooks.
    pub fn iter(&self) -> impl Iterator<Item = &HookConfig> {
        self.hooks.iter()
    }

    /// Create sample hook files in `.ava/hooks/`.
    pub fn create_templates() -> Result<String, String> {
        let dir = PathBuf::from(".ava").join("hooks");
        std::fs::create_dir_all(&dir).map_err(|e| format!("Failed to create .ava/hooks/: {e}"))?;

        let mut created = Vec::new();

        // auto-format hook
        let format_path = dir.join("auto-format.toml");
        if !format_path.exists() {
            let content = r#"event = "PostToolUse"
description = "Auto-format Rust code after edits"

# Only trigger for file-writing tools
matcher = "edit|write|multiedit|apply_patch"

# Only for Rust files
path_pattern = "*.rs"

# Run before other PostToolUse hooks
priority = 50

enabled = true

[action]
type = "command"
command = "cargo fmt"
timeout = 10
"#;
            std::fs::write(&format_path, content)
                .map_err(|e| format!("Failed to write auto-format.toml: {e}"))?;
            created.push(format_path.display().to_string());
        }

        // pre-commit lint hook
        let lint_path = dir.join("pre-commit-lint.toml");
        if !lint_path.exists() {
            let content = r#"event = "Stop"
description = "Run clippy after agent finishes"

priority = 100
enabled = false  # Enable when ready

[action]
type = "command"
command = "cargo clippy --workspace --quiet 2>&1 | head -20"
timeout = 60
"#;
            std::fs::write(&lint_path, content)
                .map_err(|e| format!("Failed to write pre-commit-lint.toml: {e}"))?;
            created.push(lint_path.display().to_string());
        }

        // dangerous command blocker hook
        let blocker_path = dir.join("block-dangerous.toml");
        if !blocker_path.exists() {
            let content = r#"event = "PreToolUse"
description = "Block dangerous bash commands"

matcher = "bash"
priority = 10

enabled = true

[action]
type = "command"
# This script receives the tool context as JSON on stdin.
# Exit code 2 = block the tool call, 0 = allow.
command = """
input=$(cat)
cmd=$(echo "$input" | grep -o '"command":"[^"]*"' | head -1 | cut -d'"' -f4)
case "$cmd" in
  *rm\ -rf\ /*)  echo "BLOCKED: recursive delete from root" >&2; exit 2 ;;
  *dd\ if=*)     echo "BLOCKED: raw disk write" >&2; exit 2 ;;
  *mkfs*)        echo "BLOCKED: filesystem format" >&2; exit 2 ;;
  *)             exit 0 ;;
esac
"""
timeout = 5
"#;
            std::fs::write(&blocker_path, content)
                .map_err(|e| format!("Failed to write block-dangerous.toml: {e}"))?;
            created.push(blocker_path.display().to_string());
        }

        // webhook notification hook
        let webhook_path = dir.join("webhook-notify.toml");
        if !webhook_path.exists() {
            let content = r#"event = "Stop"
description = "Send a webhook notification when the agent finishes"

priority = 200
enabled = false  # Set your URL and enable

[action]
type = "http"
url = "https://hooks.example.com/ava"
timeout = 5
# headers = { Authorization = "Bearer YOUR_TOKEN" }
"#;
            std::fs::write(&webhook_path, content)
                .map_err(|e| format!("Failed to write webhook-notify.toml: {e}"))?;
            created.push(webhook_path.display().to_string());
        }

        if created.is_empty() {
            Err("Hook templates already exist in .ava/hooks/".to_string())
        } else {
            Ok(format!(
                "Created {} hook templates:\n  {}",
                created.len(),
                created.join("\n  ")
            ))
        }
    }
}

/// Simple glob matching supporting `*` (any sequence) and `?` (any single char).
/// Matches against the filename component for path patterns, or full string otherwise.
fn glob_match(pattern: &str, text: &str) -> bool {
    // For path patterns, try matching against the filename component
    let text_to_match = if pattern.contains('/') || pattern.contains("**") {
        text
    } else {
        // Match against filename only
        text.rsplit('/').next().unwrap_or(text)
    };

    glob_match_impl(pattern, text_to_match)
}

fn glob_match_impl(pattern: &str, text: &str) -> bool {
    // Simple iterative glob matching with backtracking
    let pat_bytes = pattern.as_bytes();
    let text_bytes = text.as_bytes();
    let mut pi = 0;
    let mut ti = 0;
    let mut star_pi = usize::MAX;
    let mut star_ti = 0;

    while ti < text_bytes.len() {
        if pi < pat_bytes.len() && (pat_bytes[pi] == b'?' || pat_bytes[pi] == text_bytes[ti]) {
            pi += 1;
            ti += 1;
        } else if pi < pat_bytes.len() && pat_bytes[pi] == b'*' {
            // Handle ** as matching path separators too
            star_pi = pi;
            star_ti = ti;
            pi += 1;
        } else if star_pi != usize::MAX {
            pi = star_pi + 1;
            star_ti += 1;
            ti = star_ti;
        } else {
            return false;
        }
    }

    // Consume remaining stars in pattern
    while pi < pat_bytes.len() && pat_bytes[pi] == b'*' {
        pi += 1;
    }

    pi == pat_bytes.len()
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn glob_match_simple() {
        assert!(glob_match("*.rs", "main.rs"));
        assert!(glob_match("*.rs", "/home/user/src/main.rs"));
        assert!(!glob_match("*.rs", "main.py"));
    }

    #[test]
    fn glob_match_question_mark() {
        assert!(glob_match("?.rs", "a.rs"));
        assert!(!glob_match("?.rs", "ab.rs"));
    }

    #[test]
    fn glob_match_star_star() {
        assert!(glob_match("src/**/*.rs", "src/foo/bar.rs"));
        // ** matches zero or more segments including the trailing slash
        assert!(glob_match("src/*.rs", "src/bar.rs"));
        assert!(glob_match("**/*.rs", "src/foo/bar.rs"));
    }

    #[test]
    fn glob_match_exact() {
        assert!(glob_match("edit", "edit"));
        assert!(!glob_match("edit", "edits"));
    }

    #[test]
    fn parse_hook_config() {
        let toml_str = r#"
event = "PostToolUse"
description = "Auto-format code after edits"
matcher = "edit|write"
path_pattern = "*.rs"
priority = 50
enabled = true

[action]
type = "command"
command = "cargo fmt"
timeout = 10
"#;
        let config: HookConfig = toml::from_str(toml_str).unwrap();
        assert_eq!(config.event, "PostToolUse");
        assert_eq!(config.matcher, Some("edit|write".to_string()));
        assert_eq!(config.path_pattern, Some("*.rs".to_string()));
        assert_eq!(config.priority, 50);
        assert!(config.enabled);
        assert!(matches!(config.action, HookAction::Command { .. }));

        if let HookAction::Command {
            command,
            timeout,
            cwd,
        } = &config.action
        {
            assert_eq!(command, "cargo fmt");
            assert_eq!(*timeout, 10);
            assert!(cwd.is_none());
        }
    }

    #[test]
    fn parse_http_action() {
        let toml_str = r#"
event = "Stop"

[action]
type = "http"
url = "https://hooks.example.com/ava"
timeout = 5

[action.headers]
Authorization = "Bearer token"
"#;
        let config: HookConfig = toml::from_str(toml_str).unwrap();
        assert!(matches!(config.action, HookAction::Http { .. }));

        if let HookAction::Http {
            url,
            headers,
            timeout,
        } = &config.action
        {
            assert_eq!(url, "https://hooks.example.com/ava");
            assert_eq!(*timeout, 5);
            assert_eq!(
                headers.get("Authorization"),
                Some(&"Bearer token".to_string())
            );
        }
    }

    #[test]
    fn parse_prompt_action() {
        let toml_str = r#"
event = "PreToolUse"
matcher = "bash"

[action]
type = "prompt"
prompt = "Should this command be allowed?"
"#;
        let config: HookConfig = toml::from_str(toml_str).unwrap();
        assert!(matches!(config.action, HookAction::Prompt { .. }));
    }

    #[test]
    fn defaults_applied() {
        let toml_str = r#"
event = "Stop"

[action]
type = "command"
command = "echo done"
"#;
        let config: HookConfig = toml::from_str(toml_str).unwrap();
        assert_eq!(config.priority, 100);
        assert!(config.enabled);
        assert!(config.description.is_none());
        assert!(config.matcher.is_none());
        assert!(config.path_pattern.is_none());

        if let HookAction::Command { timeout, .. } = &config.action {
            assert_eq!(*timeout, 30);
        }
    }

    #[test]
    fn matches_tool_name() {
        use super::super::events::HookContext;

        let hook = HookConfig {
            event: "PostToolUse".to_string(),
            description: None,
            matcher: Some("edit|write|multiedit".to_string()),
            path_pattern: None,
            priority: 100,
            enabled: true,
            action: HookAction::Command {
                command: "echo ok".to_string(),
                timeout: 30,
                cwd: None,
            },
            source: HookSource::Project,
        };

        let mut ctx = HookContext::default();
        ctx.tool_name = Some("edit".to_string());
        assert!(HookRegistry::matches(&hook, &ctx));

        ctx.tool_name = Some("write".to_string());
        assert!(HookRegistry::matches(&hook, &ctx));

        ctx.tool_name = Some("bash".to_string());
        assert!(!HookRegistry::matches(&hook, &ctx));

        ctx.tool_name = None;
        assert!(!HookRegistry::matches(&hook, &ctx));
    }

    #[test]
    fn matches_path_pattern() {
        use super::super::events::HookContext;

        let hook = HookConfig {
            event: "PostToolUse".to_string(),
            description: None,
            matcher: None,
            path_pattern: Some("*.rs".to_string()),
            priority: 100,
            enabled: true,
            action: HookAction::Command {
                command: "echo ok".to_string(),
                timeout: 30,
                cwd: None,
            },
            source: HookSource::Project,
        };

        let mut ctx = HookContext::default();
        ctx.file_path = Some("src/main.rs".to_string());
        assert!(HookRegistry::matches(&hook, &ctx));

        ctx.file_path = Some("src/main.py".to_string());
        assert!(!HookRegistry::matches(&hook, &ctx));

        ctx.file_path = None;
        assert!(!HookRegistry::matches(&hook, &ctx));
    }

    #[test]
    fn hooks_sorted_by_priority() {
        let registry = HookRegistry {
            hooks: vec![
                HookConfig {
                    event: "Stop".to_string(),
                    description: None,
                    matcher: None,
                    path_pattern: None,
                    priority: 200,
                    enabled: true,
                    action: HookAction::Command {
                        command: "echo last".to_string(),
                        timeout: 30,
                        cwd: None,
                    },
                    source: HookSource::Project,
                },
                HookConfig {
                    event: "Stop".to_string(),
                    description: None,
                    matcher: None,
                    path_pattern: None,
                    priority: 10,
                    enabled: true,
                    action: HookAction::Command {
                        command: "echo first".to_string(),
                        timeout: 30,
                        cwd: None,
                    },
                    source: HookSource::Global,
                },
                HookConfig {
                    event: "Stop".to_string(),
                    description: None,
                    matcher: None,
                    path_pattern: None,
                    priority: 100,
                    enabled: false, // disabled
                    action: HookAction::Command {
                        command: "echo disabled".to_string(),
                        timeout: 30,
                        cwd: None,
                    },
                    source: HookSource::Project,
                },
            ],
        };

        let hooks = registry.hooks_for_event(&HookEvent::Stop);
        assert_eq!(hooks.len(), 2); // disabled one excluded
        if let HookAction::Command { command, .. } = &hooks[0].action {
            assert_eq!(command, "echo first");
        }
        if let HookAction::Command { command, .. } = &hooks[1].action {
            assert_eq!(command, "echo last");
        }
    }

    #[test]
    fn no_hooks_for_unmatched_event() {
        let registry = HookRegistry {
            hooks: vec![HookConfig {
                event: "Stop".to_string(),
                description: None,
                matcher: None,
                path_pattern: None,
                priority: 100,
                enabled: true,
                action: HookAction::Command {
                    command: "echo ok".to_string(),
                    timeout: 30,
                    cwd: None,
                },
                source: HookSource::Project,
            }],
        };

        let hooks = registry.hooks_for_event(&HookEvent::SessionStart);
        assert!(hooks.is_empty());
    }
}
