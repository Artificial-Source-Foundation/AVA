//! Pre/post tool-use hooks — runs shell scripts before and after tool execution.
//!
//! Scripts in `.ava/hooks/pre-tool/` run before tool execution; scripts in
//! `.ava/hooks/post-tool/` run after. Pre-hook scripts returning exit code 1
//! cancel the tool call.

use std::path::{Path, PathBuf};

use async_trait::async_trait;
use ava_types::{AvaError, Result, ToolCall, ToolResult};

use crate::registry::Middleware;

/// Middleware that runs shell-script hooks before and after tool execution.
pub struct HookMiddleware {
    /// Root directory containing `.ava/hooks/`.
    hooks_root: PathBuf,
}

impl HookMiddleware {
    /// Create a new hook middleware rooted at the given workspace directory.
    ///
    /// Hooks are expected at:
    /// - `{hooks_root}/.ava/hooks/pre-tool/*.sh`
    /// - `{hooks_root}/.ava/hooks/post-tool/*.sh`
    pub fn new(hooks_root: impl Into<PathBuf>) -> Self {
        Self {
            hooks_root: hooks_root.into(),
        }
    }

    /// Collect and sort hook scripts from a directory.
    fn collect_scripts(dir: &Path) -> Vec<PathBuf> {
        let mut scripts = Vec::new();
        if let Ok(entries) = std::fs::read_dir(dir) {
            for entry in entries.flatten() {
                let path = entry.path();
                if path.is_file() {
                    if let Some(ext) = path.extension() {
                        if ext == "sh" {
                            scripts.push(path);
                        }
                    }
                }
            }
        }
        scripts.sort();
        scripts
    }

    /// Run a set of hook scripts with the given environment variables.
    /// Returns an error if any script exits with code 1 (cancel).
    async fn run_hooks(
        scripts: &[PathBuf],
        tool_name: &str,
        tool_args: &str,
        phase: &str,
    ) -> Result<()> {
        for script in scripts {
            let output = tokio::process::Command::new("sh")
                .arg(script)
                .env("AVA_TOOL_NAME", tool_name)
                .env("AVA_TOOL_ARGS", tool_args)
                .env("AVA_HOOK_PHASE", phase)
                .output()
                .await
                .map_err(|e| {
                    AvaError::ToolError(format!("Failed to run hook {}: {}", script.display(), e))
                })?;

            if output.status.code() == Some(1) {
                let stderr = String::from_utf8_lossy(&output.stderr);
                return Err(AvaError::ToolError(format!(
                    "Pre-hook {} cancelled tool call: {}",
                    script.display(),
                    stderr.trim()
                )));
            }
        }
        Ok(())
    }
}

#[async_trait]
impl Middleware for HookMiddleware {
    async fn before(&self, tool_call: &ToolCall) -> Result<()> {
        let pre_dir = self.hooks_root.join(".ava/hooks/pre-tool");
        let scripts = Self::collect_scripts(&pre_dir);
        if scripts.is_empty() {
            return Ok(());
        }
        let args_str = tool_call.arguments.to_string();
        Self::run_hooks(&scripts, &tool_call.name, &args_str, "pre").await
    }

    async fn after(&self, tool_call: &ToolCall, result: &ToolResult) -> Result<ToolResult> {
        let post_dir = self.hooks_root.join(".ava/hooks/post-tool");
        let scripts = Self::collect_scripts(&post_dir);
        if scripts.is_empty() {
            return Ok(result.clone());
        }
        let args_str = tool_call.arguments.to_string();
        // Post-hooks are fire-and-forget — errors are logged but don't fail the tool call
        if let Err(e) = Self::run_hooks(&scripts, &tool_call.name, &args_str, "post").await {
            tracing::warn!("Post-hook error: {}", e);
        }
        Ok(result.clone())
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use serde_json::json;
    use std::io::Write;
    use tempfile::TempDir;

    fn make_tool_call(name: &str) -> ToolCall {
        ToolCall {
            id: "test_1".to_string(),
            name: name.to_string(),
            arguments: json!({"command": "ls"}),
        }
    }

    #[test]
    fn collect_scripts_sorts_alphabetically() {
        let dir = TempDir::new().unwrap();
        let hook_dir = dir.path().join(".ava/hooks/pre-tool");
        std::fs::create_dir_all(&hook_dir).unwrap();

        for name in &["02_second.sh", "01_first.sh", "03_third.sh"] {
            std::fs::File::create(hook_dir.join(name)).unwrap();
        }

        let scripts = HookMiddleware::collect_scripts(&hook_dir);
        assert_eq!(scripts.len(), 3);
        assert!(scripts[0].to_string_lossy().contains("01_first"));
        assert!(scripts[2].to_string_lossy().contains("03_third"));
    }

    #[tokio::test]
    async fn no_hooks_directory_succeeds() {
        let dir = TempDir::new().unwrap();
        let middleware = HookMiddleware::new(dir.path());
        let call = make_tool_call("bash");
        middleware.before(&call).await.unwrap();
    }

    #[tokio::test]
    async fn pre_hook_exit_1_cancels_tool() {
        let dir = TempDir::new().unwrap();
        let hook_dir = dir.path().join(".ava/hooks/pre-tool");
        std::fs::create_dir_all(&hook_dir).unwrap();

        let script_path = hook_dir.join("cancel.sh");
        let mut f = std::fs::File::create(&script_path).unwrap();
        writeln!(f, "#!/bin/sh\nexit 1").unwrap();

        #[cfg(unix)]
        {
            use std::os::unix::fs::PermissionsExt;
            std::fs::set_permissions(&script_path, std::fs::Permissions::from_mode(0o755)).unwrap();
        }

        let middleware = HookMiddleware::new(dir.path());
        let call = make_tool_call("bash");
        let result = middleware.before(&call).await;
        assert!(result.is_err());
    }
}
