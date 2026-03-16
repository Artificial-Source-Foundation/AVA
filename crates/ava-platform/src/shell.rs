//! Shell execution abstraction

use async_trait::async_trait;
use ava_types::{AvaError, Result};
use futures::stream::{BoxStream, StreamExt};
use std::process::Stdio;
use std::sync::Arc;
use std::time::Duration;
use tokio::io::{AsyncBufReadExt, BufReader};
use tokio::process::Command;
use tokio::sync::{mpsc, oneshot, Mutex};

/// Command execution output
#[derive(Debug, Clone)]
pub struct CommandOutput {
    pub stdout: String,
    pub stderr: String,
    pub exit_code: i32,
    pub duration: Duration,
}

/// Options for command execution
#[derive(Debug, Clone)]
pub struct ExecuteOptions {
    pub timeout: Option<Duration>,
    pub working_dir: Option<std::path::PathBuf>,
    pub env_vars: Vec<(String, String)>,
}

impl Default for ExecuteOptions {
    fn default() -> Self {
        Self {
            timeout: Some(Duration::from_secs(300)),
            working_dir: None,
            env_vars: Vec::new(),
        }
    }
}

/// Shell trait for command execution
#[async_trait]
pub trait Shell: Send + Sync {
    /// Execute a command and return output
    async fn execute(&self, command: &str, options: ExecuteOptions) -> Result<CommandOutput>;

    /// Execute with streaming output (lines from stdout and stderr)
    async fn execute_streaming(
        &self,
        command: &str,
        options: ExecuteOptions,
    ) -> Result<BoxStream<'static, Result<String>>>;
}

/// Local shell implementation using tokio::process
#[derive(Debug, Default)]
pub struct LocalShell;

impl LocalShell {
    pub fn new() -> Self {
        Self
    }
}

#[async_trait]
impl Shell for LocalShell {
    async fn execute(&self, command: &str, options: ExecuteOptions) -> Result<CommandOutput> {
        tracing::debug!("Shell command: {command}");
        let start = tokio::time::Instant::now();

        let mut cmd = Command::new("sh");
        cmd.arg("-c").arg(command);
        cmd.stdout(Stdio::piped()).stderr(Stdio::piped());

        if let Some(dir) = options.working_dir {
            cmd.current_dir(dir);
        }

        for (key, value) in options.env_vars {
            cmd.env(key, value);
        }

        // Spawn the child so we retain a handle for killing on timeout
        let child = cmd.spawn().map_err(|e| AvaError::IoError(e.to_string()))?;

        if let Some(timeout) = options.timeout {
            // Pin the future so we can cancel it and still kill the child on timeout.
            let mut child = child;
            let stdout = child.stdout.take();
            let stderr = child.stderr.take();

            let wait_fut = async {
                let status = child.wait().await?;
                let mut stdout_buf = Vec::new();
                let mut stderr_buf = Vec::new();
                if let Some(mut r) = stdout {
                    tokio::io::AsyncReadExt::read_to_end(&mut r, &mut stdout_buf).await?;
                }
                if let Some(mut r) = stderr {
                    tokio::io::AsyncReadExt::read_to_end(&mut r, &mut stderr_buf).await?;
                }
                Ok::<_, std::io::Error>((status, stdout_buf, stderr_buf))
            };

            match tokio::time::timeout(timeout, wait_fut).await {
                Ok(Ok((status, stdout_buf, stderr_buf))) => Ok(CommandOutput {
                    stdout: String::from_utf8_lossy(&stdout_buf).to_string(),
                    stderr: String::from_utf8_lossy(&stderr_buf).to_string(),
                    exit_code: status.code().unwrap_or(-1),
                    duration: start.elapsed(),
                }),
                Ok(Err(e)) => Err(AvaError::IoError(e.to_string())),
                Err(_) => {
                    // Kill the child process to avoid orphans
                    child.kill().await.ok();
                    child.wait().await.ok(); // Reap to avoid zombies
                    Err(AvaError::TimeoutError(format!(
                        "Command timed out after {timeout:?}"
                    )))
                }
            }
        } else {
            let output = child
                .wait_with_output()
                .await
                .map_err(|e| AvaError::IoError(e.to_string()))?;
            Ok(CommandOutput {
                stdout: String::from_utf8_lossy(&output.stdout).to_string(),
                stderr: String::from_utf8_lossy(&output.stderr).to_string(),
                exit_code: output.status.code().unwrap_or(-1),
                duration: start.elapsed(),
            })
        }
    }

    async fn execute_streaming(
        &self,
        command: &str,
        options: ExecuteOptions,
    ) -> Result<BoxStream<'static, Result<String>>> {
        let mut cmd = Command::new("sh");
        cmd.arg("-c").arg(command);
        cmd.stdout(Stdio::piped()).stderr(Stdio::piped());

        if let Some(dir) = options.working_dir {
            cmd.current_dir(dir);
        }

        for (key, value) in options.env_vars {
            cmd.env(key, value);
        }

        let mut child = cmd.spawn().map_err(|e| {
            AvaError::PlatformError(format!("Failed to spawn 'sh -c {command}': {e}"))
        })?;

        let stdout = child.stdout.take().ok_or_else(|| {
            AvaError::PlatformError(format!("Failed to capture stdout for command: {command}"))
        })?;

        let stderr = child.stderr.take().ok_or_else(|| {
            AvaError::PlatformError(format!("Failed to capture stderr for command: {command}"))
        })?;

        // Wrap child in Arc<Mutex> so both timeout and wait tasks can access it
        let child = Arc::new(Mutex::new(Some(child)));

        let stdout_reader = BufReader::new(stdout);
        let stderr_reader = BufReader::new(stderr);

        let (tx, rx) = mpsc::unbounded_channel::<Result<String>>();

        // Spawn task to read stdout
        let tx_stdout = tx.clone();
        tokio::spawn(async move {
            let mut lines = stdout_reader.lines();
            while let Ok(Some(line)) = lines.next_line().await {
                let _ = tx_stdout.send(Ok(format!("[stdout] {line}")));
            }
        });

        // Spawn task to read stderr
        let tx_stderr = tx.clone();
        tokio::spawn(async move {
            let mut lines = stderr_reader.lines();
            while let Ok(Some(line)) = lines.next_line().await {
                let _ = tx_stderr.send(Ok(format!("[stderr] {line}")));
            }
        });

        // Apply timeout if specified
        let mut completion_tx = None;
        if let Some(timeout) = options.timeout {
            let tx_timeout = tx.clone();
            let (done_tx, done_rx) = oneshot::channel::<()>();
            completion_tx = Some(done_tx);
            let child_timeout = Arc::clone(&child);

            tokio::spawn(async move {
                tokio::select! {
                    _ = tokio::time::sleep(timeout) => {
                        // Kill the child process to avoid orphans
                        if let Some(mut ch) = child_timeout.lock().await.take() {
                            ch.kill().await.ok();
                            ch.wait().await.ok(); // Reap to avoid zombies
                        }
                        let _ = tx_timeout.send(Err(AvaError::TimeoutError(format!(
                            "Command timed out after {timeout:?}"
                        ))));
                    }
                    _ = done_rx => {}
                }
            });
        }

        // Spawn task to wait for process and report exit code
        tokio::spawn(async move {
            // Take the child out of the shared slot (may be None if timeout killed it)
            let Some(mut ch) = child.lock().await.take() else {
                return;
            };
            match ch.wait().await {
                Ok(status) => {
                    let _ = completion_tx.and_then(|tx_done| tx_done.send(()).ok());
                    let _ = tx.send(Ok(format!("[exit] {}", status.code().unwrap_or(-1))));
                }
                Err(e) => {
                    let _ = completion_tx.and_then(|tx_done| tx_done.send(()).ok());
                    let _ = tx.send(Err(AvaError::PlatformError(format!("Process error: {e}"))));
                }
            }
        });

        let stream = tokio_stream::wrappers::UnboundedReceiverStream::new(rx);
        Ok(stream.boxed())
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::time::Duration;

    #[tokio::test]
    async fn test_execute_echo() {
        let shell = LocalShell::new();
        let output = shell
            .execute("echo hello", ExecuteOptions::default())
            .await
            .unwrap();
        assert!(output.stdout.contains("hello"));
        assert_eq!(output.exit_code, 0);
    }

    #[tokio::test]
    async fn test_execute_with_working_dir() {
        let shell = LocalShell::new();
        let options = ExecuteOptions {
            working_dir: Some(std::path::PathBuf::from("/tmp")),
            ..Default::default()
        };
        let output = shell.execute("pwd", options).await.unwrap();
        assert!(output.stdout.contains("/tmp"));
    }

    #[tokio::test]
    async fn test_execute_with_env_var() {
        let shell = LocalShell::new();
        let options = ExecuteOptions {
            env_vars: vec![("TEST_VAR".to_string(), "test_value".to_string())],
            ..Default::default()
        };
        let output = shell.execute("echo $TEST_VAR", options).await.unwrap();
        assert!(output.stdout.contains("test_value"));
    }

    #[tokio::test]
    async fn test_execute_timeout() {
        let shell = LocalShell::new();
        let options = ExecuteOptions {
            timeout: Some(Duration::from_millis(100)),
            ..Default::default()
        };
        let result = shell.execute("sleep 5", options).await;
        assert!(matches!(result, Err(AvaError::TimeoutError(_))));
    }

    #[tokio::test]
    async fn test_execute_streaming() {
        let shell = LocalShell::new();
        let stream = shell
            .execute_streaming(
                "echo 'line1' && echo 'line2' >&2",
                ExecuteOptions::default(),
            )
            .await
            .unwrap();

        let lines: Vec<String> = stream
            .filter_map(|result| async move { result.ok() })
            .collect()
            .await;

        // Should have stdout line, stderr line, and exit code
        assert!(lines.iter().any(|l| l.contains("[stdout] line1")));
        assert!(lines.iter().any(|l| l.contains("[stderr] line2")));
        assert!(lines.iter().any(|l| l.contains("[exit]")));
    }
}
