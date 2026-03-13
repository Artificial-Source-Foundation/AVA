use std::process::Stdio;
use std::time::Duration;

use tokio::process::Command;

use crate::error::SandboxError;
use crate::types::SandboxPlan;

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct SandboxOutput {
    pub stdout: String,
    pub stderr: String,
    pub exit_code: i32,
}

pub async fn execute_plan(
    plan: &SandboxPlan,
    timeout: Duration,
) -> Result<SandboxOutput, SandboxError> {
    let result = tokio::time::timeout(timeout, async {
        let mut command = Command::new(&plan.program);
        command
            .args(&plan.args)
            .stdout(Stdio::piped())
            .stderr(Stdio::piped())
            .kill_on_drop(true);

        let output = command
            .spawn()
            .map_err(|e| SandboxError::ExecutionFailed(e.to_string()))?
            .wait_with_output()
            .await
            .map_err(|e| SandboxError::ExecutionFailed(e.to_string()))?;

        Ok::<SandboxOutput, SandboxError>(SandboxOutput {
            stdout: String::from_utf8_lossy(&output.stdout).to_string(),
            stderr: String::from_utf8_lossy(&output.stderr).to_string(),
            exit_code: output.status.code().unwrap_or(-1),
        })
    })
    .await;

    match result {
        Ok(value) => value,
        Err(_) => Err(SandboxError::Timeout),
    }
}

#[cfg(test)]
mod tests {
    use std::time::Duration;

    use super::execute_plan;
    use crate::types::SandboxPlan;

    #[tokio::test]
    async fn execute_plan_runs_simple_echo() {
        let plan = SandboxPlan {
            program: "sh".to_string(),
            args: vec!["-c".to_string(), "echo sandbox-ok".to_string()],
        };

        let output = execute_plan(&plan, Duration::from_secs(2))
            .await
            .expect("plan execution should succeed");

        assert_eq!(output.exit_code, 0);
        assert!(output.stdout.contains("sandbox-ok"));
    }
}
