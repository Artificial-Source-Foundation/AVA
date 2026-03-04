use serde::Deserialize;
use thiserror::Error;
use tokio::process::Command;

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum GitAction {
    Commit(Vec<String>),
    Branch(Vec<String>),
    Checkout(Vec<String>),
    Status(Vec<String>),
    Diff(Vec<String>),
    Log(Vec<String>),
    Pr(Vec<String>),
}

impl GitAction {
    pub fn from_json(payload: &str) -> Result<Self, GitToolError> {
        let raw: RawGitAction = serde_json::from_str(payload)
            .map_err(|err| GitToolError::InvalidActionPayload(err.to_string()))?;

        raw.into_action()
    }
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct ToolResult {
    pub program: String,
    pub args: Vec<String>,
    pub stdout: String,
    pub stderr: String,
    pub exit_code: i32,
}

#[derive(Debug, Default)]
pub struct GitTool;

impl GitTool {
    pub const fn new() -> Self {
        Self
    }

    pub fn dispatch(action: &GitAction) -> (&'static str, Vec<String>) {
        let (program, command, extra) = match action {
            GitAction::Commit(extra) => ("git", "commit", extra),
            GitAction::Branch(extra) => ("git", "branch", extra),
            GitAction::Checkout(extra) => ("git", "checkout", extra),
            GitAction::Status(extra) => ("git", "status", extra),
            GitAction::Diff(extra) => ("git", "diff", extra),
            GitAction::Log(extra) => ("git", "log", extra),
            GitAction::Pr(extra) => ("gh", "pr", extra),
        };

        let mut args = vec![command.to_string()];
        args.extend(extra.iter().cloned());
        (program, args)
    }

    pub async fn run(&self, action: GitAction) -> Result<ToolResult, GitToolError> {
        let (program, args) = Self::dispatch(&action);
        self.run_command(program, args).await
    }

    pub async fn run_from_json(&self, payload: &str) -> Result<ToolResult, GitToolError> {
        let action = GitAction::from_json(payload)?;
        self.run(action).await
    }

    async fn run_command(
        &self,
        program: &'static str,
        args: Vec<String>,
    ) -> Result<ToolResult, GitToolError> {
        let output = Command::new(program)
            .args(&args)
            .output()
            .await
            .map_err(|err| GitToolError::ExecutionFailed {
                program: program.to_string(),
                source: err,
            })?;

        let exit_code = output.status.code().unwrap_or(-1);
        let stdout = String::from_utf8_lossy(&output.stdout).into_owned();
        let stderr = String::from_utf8_lossy(&output.stderr).into_owned();

        if !output.status.success() {
            return Err(GitToolError::CommandFailed {
                program: program.to_string(),
                args,
                exit_code,
                stdout,
                stderr,
            });
        }

        Ok(ToolResult {
            program: program.to_string(),
            args,
            stdout,
            stderr,
            exit_code,
        })
    }
}

#[derive(Debug, Error)]
pub enum GitToolError {
    #[error("invalid git action payload: {0}")]
    InvalidActionPayload(String),
    #[error("unsupported git action: {0}")]
    UnsupportedAction(String),
    #[error("failed to execute {program}: {source}")]
    ExecutionFailed {
        program: String,
        #[source]
        source: std::io::Error,
    },
    #[error("{program} exited with code {exit_code}: {stderr}")]
    CommandFailed {
        program: String,
        args: Vec<String>,
        exit_code: i32,
        stdout: String,
        stderr: String,
    },
}

#[derive(Debug, Deserialize)]
struct RawGitAction {
    action: String,
    #[serde(default)]
    args: Vec<String>,
}

impl RawGitAction {
    fn into_action(self) -> Result<GitAction, GitToolError> {
        match self.action.as_str() {
            "commit" => Ok(GitAction::Commit(self.args)),
            "branch" => Ok(GitAction::Branch(self.args)),
            "checkout" => Ok(GitAction::Checkout(self.args)),
            "status" => Ok(GitAction::Status(self.args)),
            "diff" => Ok(GitAction::Diff(self.args)),
            "log" => Ok(GitAction::Log(self.args)),
            "pr" => Ok(GitAction::Pr(self.args)),
            _ => Err(GitToolError::UnsupportedAction(self.action)),
        }
    }
}

#[cfg(test)]
mod tests;
