use std::path::Path;
use std::sync::Arc;
use std::time::Duration;

use async_trait::async_trait;
use ava_platform::Platform;
use ava_types::{AvaError, ToolResult};
use serde_json::{json, Value};

use crate::registry::Tool;

const DEFAULT_TIMEOUT_MS: u64 = 60_000;
const MAX_OUTPUT_BYTES: usize = 50 * 1024;

pub struct TestRunnerTool {
    platform: Arc<dyn Platform>,
}

impl TestRunnerTool {
    pub fn new(platform: Arc<dyn Platform>) -> Self {
        Self { platform }
    }
}

#[async_trait]
impl Tool for TestRunnerTool {
    fn name(&self) -> &str {
        "test_runner"
    }

    fn description(&self) -> &str {
        "Run project tests with auto-detection of test framework"
    }

    fn parameters(&self) -> Value {
        json!({
            "type": "object",
            "properties": {
                "command": { "type": "string", "description": "Custom test command (overrides auto-detection)" },
                "filter": { "type": "string", "description": "Test name filter pattern" },
                "timeout": { "type": "integer", "description": "Timeout in seconds (default 60)" }
            }
        })
    }

    async fn execute(&self, args: Value) -> ava_types::Result<ToolResult> {
        let custom_command = args.get("command").and_then(Value::as_str);
        let filter = args.get("filter").and_then(Value::as_str);
        let timeout_secs = args
            .get("timeout")
            .and_then(Value::as_u64)
            .unwrap_or(DEFAULT_TIMEOUT_MS / 1000);

        let base_command = if let Some(cmd) = custom_command {
            cmd.to_string()
        } else {
            detect_test_command(&*self.platform).await?
        };

        let command = if let Some(f) = filter {
            format!("{base_command} {f}")
        } else {
            base_command
        };

        let output = tokio::time::timeout(
            Duration::from_secs(timeout_secs),
            self.platform.execute(&command),
        )
        .await
        .map_err(|_| {
            AvaError::TimeoutError(format!("test command timed out after {timeout_secs}s"))
        })??;

        let mut combined = format!("{}\n{}", output.stdout, output.stderr);
        truncate_split(&mut combined, MAX_OUTPUT_BYTES);

        let passed = output.exit_code == 0;
        let result_json = json!({
            "passed": passed,
            "exit_code": output.exit_code,
            "output": combined,
        });

        Ok(ToolResult {
            call_id: String::new(),
            content: result_json.to_string(),
            is_error: !passed,
        })
    }
}

async fn detect_test_command(platform: &dyn Platform) -> ava_types::Result<String> {
    if platform.exists(Path::new("Cargo.toml")).await {
        return Ok("cargo test".to_string());
    }
    if platform.exists(Path::new("package.json")).await {
        return Ok("npm test".to_string());
    }
    if platform.exists(Path::new("pyproject.toml")).await || platform.exists(Path::new("pytest.ini")).await {
        return Ok("pytest".to_string());
    }
    if platform.exists(Path::new("go.mod")).await {
        return Ok("go test ./...".to_string());
    }
    Err(AvaError::ToolError(
        "Could not detect test framework. Provide a 'command' parameter.".to_string(),
    ))
}

fn truncate_split(content: &mut String, max_bytes: usize) {
    if content.len() <= max_bytes {
        return;
    }
    let half = max_bytes / 2;
    let mut start_end = half;
    while !content.is_char_boundary(start_end) {
        start_end -= 1;
    }
    let mut tail_start = content.len() - half;
    while !content.is_char_boundary(tail_start) {
        tail_start += 1;
    }
    let head = content[..start_end].to_string();
    let tail = content[tail_start..].to_string();
    *content = format!("{head}\n[...truncated...]\n{tail}");
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn truncate_split_preserves_small_content() {
        let mut s = "hello world".to_string();
        truncate_split(&mut s, 100);
        assert_eq!(s, "hello world");
    }

    #[test]
    fn truncate_split_inserts_marker() {
        let mut s = "a".repeat(200);
        truncate_split(&mut s, 100);
        assert!(s.contains("[...truncated...]"));
        assert!(s.len() < 200);
    }
}
