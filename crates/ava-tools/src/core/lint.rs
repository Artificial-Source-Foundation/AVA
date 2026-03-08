use std::path::Path;
use std::sync::Arc;

use async_trait::async_trait;
use ava_platform::Platform;
use ava_types::{AvaError, ToolResult};
use regex::Regex;
use serde_json::{json, Value};

use crate::registry::Tool;

pub struct LintTool {
    platform: Arc<dyn Platform>,
}

impl LintTool {
    pub fn new(platform: Arc<dyn Platform>) -> Self {
        Self { platform }
    }
}

#[async_trait]
impl Tool for LintTool {
    fn name(&self) -> &str {
        "lint"
    }

    fn description(&self) -> &str {
        "Run project linter with auto-detection"
    }

    fn parameters(&self) -> Value {
        json!({
            "type": "object",
            "properties": {
                "command": { "type": "string", "description": "Custom lint command (overrides auto-detection)" },
                "fix": { "type": "boolean", "description": "Apply auto-fixes if supported" },
                "path": { "type": "string", "description": "Scope lint to this path" }
            }
        })
    }

    async fn execute(&self, args: Value) -> ava_types::Result<ToolResult> {
        let custom_command = args.get("command").and_then(Value::as_str);
        let fix = args.get("fix").and_then(Value::as_bool).unwrap_or(false);
        let scope_path = args.get("path").and_then(Value::as_str);

        let command = if let Some(cmd) = custom_command {
            let mut cmd = cmd.to_string();
            if fix && !cmd.contains("--fix") {
                cmd.push_str(" --fix");
            }
            if let Some(p) = scope_path {
                cmd.push(' ');
                cmd.push_str(p);
            }
            cmd
        } else {
            detect_lint_command(&*self.platform, fix, scope_path).await?
        };

        let output = self.platform.execute(&command).await?;
        let combined = format!("{}\n{}", output.stdout, output.stderr);

        let (warnings, errors) = count_diagnostics(&combined);

        let result_json = json!({
            "warnings": warnings,
            "errors": errors,
            "output": combined,
            "fixed": fix && output.exit_code == 0,
        });

        Ok(ToolResult {
            call_id: String::new(),
            content: result_json.to_string(),
            is_error: errors > 0,
        })
    }
}

async fn detect_lint_command(
    platform: &dyn Platform,
    fix: bool,
    scope_path: Option<&str>,
) -> ava_types::Result<String> {
    let path_suffix = scope_path.map(|p| format!(" {p}")).unwrap_or_default();

    if platform.exists(Path::new("Cargo.toml")).await {
        return if fix {
            Ok(format!("cargo clippy --fix --allow-dirty{path_suffix}"))
        } else {
            Ok(format!("cargo clippy{path_suffix}"))
        };
    }

    if platform.exists(Path::new("package.json")).await {
        return if fix {
            Ok(format!("npx eslint . --fix{path_suffix}"))
        } else {
            Ok(format!("npx eslint .{path_suffix}"))
        };
    }

    if platform.exists(Path::new("pyproject.toml")).await {
        return if fix {
            Ok(format!("ruff check . --fix{path_suffix}"))
        } else {
            Ok(format!("ruff check .{path_suffix}"))
        };
    }

    Err(AvaError::ToolError(
        "Could not detect linter. Provide a 'command' parameter.".to_string(),
    ))
}

fn count_diagnostics(output: &str) -> (usize, usize) {
    let mut warnings = 0usize;
    let mut errors = 0usize;

    // Rust/clippy pattern: "warning: ..." / "error: ..." or "N warnings"
    // ESLint pattern: "N problems (X errors, Y warnings)"
    // Try structured counts first
    let re_eslint = Regex::new(r"(\d+) problems? \((\d+) errors?, (\d+) warnings?\)").ok();
    if let Some(re) = &re_eslint {
        if let Some(caps) = re.captures(output) {
            errors = caps.get(2).and_then(|m| m.as_str().parse().ok()).unwrap_or(0);
            warnings = caps.get(3).and_then(|m| m.as_str().parse().ok()).unwrap_or(0);
            return (warnings, errors);
        }
    }

    // Rust summary: "warning: ... generated N warnings" / "error[E...]:"
    let re_rust_warn = Regex::new(r"generated (\d+) warnings?").ok();
    if let Some(re) = &re_rust_warn {
        for caps in re.captures_iter(output) {
            warnings += caps.get(1).and_then(|m| m.as_str().parse::<usize>().ok()).unwrap_or(0);
        }
    }

    // Count individual error lines
    for line in output.lines() {
        let trimmed = line.trim_start();
        if trimmed.starts_with("error[") || trimmed.starts_with("error:") {
            errors += 1;
        }
    }

    (warnings, errors)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn count_eslint_diagnostics() {
        let output = "src/main.ts:1:1 error ...\n✖ 5 problems (3 errors, 2 warnings)";
        let (w, e) = count_diagnostics(output);
        assert_eq!(w, 2);
        assert_eq!(e, 3);
    }

    #[test]
    fn count_rust_diagnostics() {
        let output = "warning: unused variable\nerror[E0308]: mismatch\nwarning: ... generated 2 warnings";
        let (w, e) = count_diagnostics(output);
        assert_eq!(w, 2);
        assert_eq!(e, 1);
    }
}
