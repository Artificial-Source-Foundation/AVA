//! Shell execution abstraction

use async_trait::async_trait;
use ava_types::{AvaError, Result};
use futures::stream::{BoxStream, StreamExt};
use std::process::Stdio;
use std::sync::Arc;
use std::time::Duration;
use tokio::io::{AsyncBufReadExt, BufReader};
use tokio::process::Command;
use tokio::sync::{mpsc, Mutex};

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
    /// When true, clear the inherited environment before applying `env_vars`.
    /// A safe default PATH is always injected when scrubbing.
    /// Use this for sandboxed command execution to prevent leaking credentials
    /// or other sensitive host environment variables.
    pub scrub_env: bool,
}

impl Default for ExecuteOptions {
    fn default() -> Self {
        Self {
            timeout: Some(Duration::from_secs(300)),
            working_dir: None,
            env_vars: Vec::new(),
            scrub_env: false,
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

        if options.scrub_env {
            cmd.env_clear();
            cmd.env("PATH", "/usr/bin:/bin:/usr/sbin:/sbin");
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
                let read_stdout = async {
                    let mut stdout_buf = Vec::new();
                    if let Some(mut r) = stdout {
                        tokio::io::AsyncReadExt::read_to_end(&mut r, &mut stdout_buf).await?;
                    }
                    Ok::<_, std::io::Error>(stdout_buf)
                };

                let read_stderr = async {
                    let mut stderr_buf = Vec::new();
                    if let Some(mut r) = stderr {
                        tokio::io::AsyncReadExt::read_to_end(&mut r, &mut stderr_buf).await?;
                    }
                    Ok::<_, std::io::Error>(stderr_buf)
                };

                let (status, stdout_buf, stderr_buf) =
                    tokio::try_join!(child.wait(), read_stdout, read_stderr)?;
                Ok::<_, std::io::Error>((status, stdout_buf, stderr_buf))
            };

            match tokio::time::timeout(timeout, wait_fut).await {
                Ok(Ok((status, stdout_buf, stderr_buf))) => Ok(CommandOutput {
                    stdout: String::from_utf8_lossy(&stdout_buf).to_string(),
                    stderr: String::from_utf8_lossy(&stderr_buf).to_string(),
                    // infallible: code() returns None only when killed by signal; -1 is conventional
                    exit_code: status.code().unwrap_or(-1),
                    duration: start.elapsed(),
                }),
                Ok(Err(e)) => Err(AvaError::IoError(e.to_string())),
                Err(_) => {
                    // Kill the child process to avoid orphans; errors are non-fatal
                    // (process may have already exited between timeout and kill)
                    if let Err(e) = child.kill().await {
                        tracing::trace!("kill after timeout (non-fatal): {e}");
                    }
                    if let Err(e) = child.wait().await {
                        tracing::trace!("reap after timeout (non-fatal): {e}");
                    }
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
                // infallible: code() returns None only when killed by signal; -1 is conventional
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
        let timeout = options.timeout;

        let mut cmd = Command::new("sh");
        cmd.arg("-c").arg(command);
        cmd.stdout(Stdio::piped()).stderr(Stdio::piped());

        if let Some(dir) = options.working_dir {
            cmd.current_dir(dir);
        }

        if options.scrub_env {
            cmd.env_clear();
            cmd.env("PATH", "/usr/bin:/bin:/usr/sbin:/sbin");
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
                // Send failure means the stream consumer disconnected — stop reading
                if tx_stdout.send(Ok(format!("[stdout] {line}"))).is_err() {
                    break;
                }
            }
        });

        // Spawn task to read stderr
        let tx_stderr = tx.clone();
        tokio::spawn(async move {
            let mut lines = stderr_reader.lines();
            while let Ok(Some(line)) = lines.next_line().await {
                // Send failure means the stream consumer disconnected — stop reading
                if tx_stderr.send(Ok(format!("[stderr] {line}"))).is_err() {
                    break;
                }
            }
        });

        // Spawn task to wait for process and report exit code
        tokio::spawn(async move {
            // Take the child out of the shared slot (may be None if timeout killed it)
            let Some(mut ch) = child.lock().await.take() else {
                return;
            };
            let wait_result = if let Some(timeout) = timeout {
                match tokio::time::timeout(timeout, ch.wait()).await {
                    Ok(result) => result,
                    Err(_) => {
                        if let Err(e) = ch.kill().await {
                            tracing::trace!("streaming kill after timeout (non-fatal): {e}");
                        }
                        if let Err(e) = ch.wait().await {
                            tracing::trace!("streaming reap after timeout (non-fatal): {e}");
                        }
                        let _ = tx.send(Err(AvaError::TimeoutError(format!(
                            "Command timed out after {timeout:?}"
                        ))));
                        return;
                    }
                }
            } else {
                ch.wait().await
            };

            match wait_result {
                Ok(status) => {
                    // infallible: code() returns None only when killed by signal; -1 is conventional
                    // Send failure means stream consumer disconnected — acceptable
                    let _ = tx.send(Ok(format!("[exit] {}", status.code().unwrap_or(-1))));
                }
                Err(e) => {
                    // Send failure means stream consumer disconnected — acceptable
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
    #[cfg(unix)]
    async fn test_execute_large_output_with_timeout_does_not_deadlock() {
        let shell = LocalShell::new();
        let options = ExecuteOptions {
            timeout: Some(Duration::from_secs(2)),
            ..Default::default()
        };

        let command = "dd if=/dev/zero bs=1024 count=1024 2>/dev/null | tr '\\0' 'o'; dd if=/dev/zero bs=1024 count=1024 2>/dev/null | tr '\\0' 'e' >&2";

        let output = tokio::time::timeout(Duration::from_secs(5), shell.execute(command, options))
            .await
            .expect("execute should not hang")
            .expect("command should complete before timeout");

        assert_eq!(output.exit_code, 0);
        assert!(output.stdout.len() >= 1024 * 1024);
        assert!(output.stderr.len() >= 1024 * 1024);
    }

    #[tokio::test]
    #[cfg(unix)]
    async fn test_execute_timeout_with_large_output_returns_promptly() {
        let shell = LocalShell::new();
        let options = ExecuteOptions {
            timeout: Some(Duration::from_millis(100)),
            ..Default::default()
        };

        let command = "{ yes o | head -c 262144; yes e | head -c 262144 >&2; sleep 5; }";

        let result = tokio::time::timeout(Duration::from_secs(3), shell.execute(command, options))
            .await
            .expect("execute should not hang");

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

    #[tokio::test]
    async fn test_execute_streaming_timeout_reports_once() {
        let shell = LocalShell::new();
        let mut stream = shell
            .execute_streaming(
                "sleep 5",
                ExecuteOptions {
                    timeout: Some(Duration::from_millis(100)),
                    ..Default::default()
                },
            )
            .await
            .unwrap();

        let first_event = tokio::time::timeout(Duration::from_secs(2), stream.next())
            .await
            .expect("stream should produce a timeout event")
            .expect("stream should not close before timeout");

        assert!(matches!(first_event, Err(AvaError::TimeoutError(_))));

        let next_event = tokio::time::timeout(Duration::from_millis(250), stream.next()).await;
        assert!(
            !matches!(next_event, Ok(Some(Ok(line))) if line.starts_with("[exit]")),
            "stream should not report a successful exit after timeout"
        );
    }
}
