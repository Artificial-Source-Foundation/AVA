use ava_tools::git::{GitAction, GitTool, GitToolError, ToolResult};
use serde::Serialize;

#[derive(Debug, Clone, PartialEq, Eq, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct GitCommandOutput {
    pub program: String,
    pub args: Vec<String>,
    pub stdout: String,
    pub stderr: String,
    pub exit_code: i32,
}

impl From<ToolResult> for GitCommandOutput {
    fn from(value: ToolResult) -> Self {
        Self {
            program: value.program,
            args: value.args,
            stdout: value.stdout,
            stderr: value.stderr,
            exit_code: value.exit_code,
        }
    }
}

fn parse_action(payload: &str) -> Result<GitAction, String> {
    GitAction::from_json(payload).map_err(format_git_error)
}

#[cfg(test)]
fn dispatch_preview(action: &GitAction) -> (String, Vec<String>) {
    let (program, args) = GitTool::dispatch(action);
    (program.to_string(), args)
}

fn format_git_error(error: GitToolError) -> String {
    match error {
        GitToolError::InvalidActionPayload(msg) => format!("invalid git payload: {msg}"),
        GitToolError::UnsupportedAction(action) => format!("unsupported git action: {action}"),
        GitToolError::ExecutionFailed { program, source } => {
            format!("failed to execute {program}: {source}")
        }
        GitToolError::CommandFailed {
            program,
            exit_code,
            stderr,
            ..
        } => format!("{program} exited with code {exit_code}: {stderr}"),
    }
}

#[tauri::command]
pub async fn execute_git_tool(payload: String) -> Result<GitCommandOutput, String> {
    let action = parse_action(&payload)?;
    let result = GitTool::new().run(action).await.map_err(format_git_error)?;
    Ok(result.into())
}

#[cfg(test)]
mod tests {
    use super::{dispatch_preview, parse_action, GitCommandOutput};
    use ava_tools::git::ToolResult;

    #[test]
    fn git_payload_maps_to_expected_program_and_args() {
        let action =
            parse_action(r#"{"action":"status","args":["--short"]}"#).expect("valid payload");
        let (program, args) = dispatch_preview(&action);

        assert_eq!(program, "git");
        assert_eq!(args, vec!["status".to_string(), "--short".to_string()]);
    }

    #[test]
    fn git_command_output_is_serializable_with_stable_shape() {
        let output = GitCommandOutput::from(ToolResult {
            program: "git".to_string(),
            args: vec!["status".to_string()],
            stdout: "M src/main.rs".to_string(),
            stderr: String::new(),
            exit_code: 0,
        });
        let json_value = serde_json::to_value(&output).expect("output should serialize");

        assert_eq!(json_value["program"], "git");
        assert_eq!(json_value["args"][0], "status");
        assert_eq!(json_value["exitCode"], 0);
    }
}
