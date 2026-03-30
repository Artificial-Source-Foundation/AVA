//! Stdio agent transport — spawns a subprocess and communicates via NDJSON.
//!
//! This is the low-level transport used by agent adapters. It handles:
//! - Spawning the child process with configured args/env
//! - Reading NDJSON lines from stdout and parsing as `AgentMessage`
//! - Writing to stdin for interrupt/cancel signals
//! - Process lifecycle (kill on cancel, timeout)

use std::collections::HashMap;
use std::sync::Arc;

use ava_types::{AvaError, Result};
use futures::Stream;
use tokio::io::{AsyncBufReadExt, AsyncWriteExt, BufReader};
use tokio::process::{Child, Command};
use tokio::sync::Mutex;
use tokio_util::sync::CancellationToken;
use tracing::{debug, warn};

use crate::protocol::AgentMessage;

/// A running agent subprocess communicating via NDJSON over stdio.
///
/// Stdout is single-consumer: use either [`Self::message_stream`] or
/// [`Self::take_stdout`], but not both for the same process.
pub struct StdioProcess {
    child: Arc<Mutex<Child>>,
    cancel: CancellationToken,
    name: String,
}

/// Configuration for spawning a stdio agent process.
#[derive(Debug, Clone)]
pub struct StdioConfig {
    pub binary: String,
    pub args: Vec<String>,
    pub env: HashMap<String, String>,
    pub env_remove: Vec<String>,
    pub cwd: Option<String>,
    pub name: String,
}

impl StdioProcess {
    /// Spawn a new agent subprocess.
    pub fn spawn(config: &StdioConfig) -> Result<Self> {
        let mut cmd = Command::new(&config.binary);
        cmd.args(&config.args)
            .stdin(std::process::Stdio::piped())
            .stdout(std::process::Stdio::piped())
            .stderr(std::process::Stdio::piped())
            .kill_on_drop(true);

        if let Some(cwd) = &config.cwd {
            cmd.current_dir(cwd);
        }

        for key in &config.env_remove {
            cmd.env_remove(key);
        }

        for (k, v) in noninteractive_env_defaults() {
            cmd.env(k, v);
        }

        // Apply environment variables
        for (k, v) in &config.env {
            cmd.env(k, v);
        }

        let mut child = cmd.spawn().map_err(|e| {
            AvaError::PlatformError(format!(
                "failed to spawn agent '{}' ({}): {e}",
                config.name, config.binary
            ))
        })?;

        drain_stderr(child.stderr.take(), config.name.clone());

        debug!(agent = %config.name, binary = %config.binary, "spawned agent process");

        Ok(Self {
            child: Arc::new(Mutex::new(child)),
            cancel: CancellationToken::new(),
            name: config.name.clone(),
        })
    }

    /// Get a stream of `AgentMessage` from the process stdout.
    ///
    /// Each line of stdout is parsed as JSON. Lines that fail to parse are
    /// logged and skipped (forward compatibility with unknown event types).
    pub async fn message_stream(
        &self,
    ) -> Result<impl Stream<Item = AgentMessage> + Send + 'static> {
        // Take stdout from the child — this can only be called once.
        let stdout = {
            let mut child = self.child.lock().await;
            child
                .stdout
                .take()
                .ok_or_else(|| AvaError::PlatformError("agent stdout already taken".into()))?
        };

        let reader = BufReader::new(stdout);
        let cancel = self.cancel.clone();
        let name = self.name.clone();

        let stream = async_stream::stream! {
            let mut lines = reader.lines();
            loop {
                tokio::select! {
                    _ = cancel.cancelled() => {
                        debug!(agent = %name, "agent stream cancelled");
                        break;
                    }
                    line = lines.next_line() => {
                        match line {
                            Ok(Some(text)) => {
                                let trimmed = text.trim();
                                if trimmed.is_empty() {
                                    continue;
                                }
                                match serde_json::from_str::<AgentMessage>(trimmed) {
                                    Ok(msg) => yield msg,
                                    Err(e) => {
                                        // Try to be forward-compatible: skip unknown lines
                                        debug!(agent = %name, line = %trimmed, error = %e, "skipping unparseable agent output");
                                    }
                                }
                            }
                            Ok(None) => {
                                debug!(agent = %name, "agent stdout EOF");
                                break;
                            }
                            Err(e) => {
                                warn!(agent = %name, error = %e, "error reading agent stdout");
                                break;
                            }
                        }
                    }
                }
            }
        };

        Ok(stream)
    }

    /// Write a line to the process stdin (for interrupt/cancel signals).
    pub async fn write_stdin(&self, data: &str) -> Result<()> {
        let mut child = self.child.lock().await;
        if let Some(stdin) = child.stdin.as_mut() {
            stdin
                .write_all(data.as_bytes())
                .await
                .map_err(AvaError::from)?;
            stdin.write_all(b"\n").await.map_err(AvaError::from)?;
            stdin.flush().await.map_err(AvaError::from)?;
            Ok(())
        } else {
            Err(AvaError::PlatformError("agent stdin not available".into()))
        }
    }

    /// Send cancellation signal and kill the process.
    pub async fn kill(&self) {
        self.cancel.cancel();
        let mut child = self.child.lock().await;
        if let Err(error) = child.kill().await {
            warn!(agent = %self.name, error = %error, "failed to kill agent process");
        }
    }

    /// Wait for the process to exit and return the exit code.
    pub async fn wait(&self) -> Result<i32> {
        let mut child = self.child.lock().await;
        let status = child.wait().await.map_err(AvaError::from)?;
        Ok(status.code().unwrap_or(-1))
    }

    /// Take stdout from the child process (for custom parsing).
    pub async fn take_stdout(&self) -> Option<tokio::process::ChildStdout> {
        let mut child = self.child.lock().await;
        child.stdout.take()
    }

    /// Get the agent name.
    pub fn name(&self) -> &str {
        &self.name
    }
}

fn noninteractive_env_defaults() -> [(&'static str, &'static str); 9] {
    [
        ("CI", "true"),
        ("GIT_TERMINAL_PROMPT", "0"),
        ("GIT_EDITOR", "true"),
        ("GIT_PAGER", "cat"),
        ("PAGER", "cat"),
        ("GCM_INTERACTIVE", "never"),
        ("npm_config_yes", "true"),
        ("PIP_NO_INPUT", "1"),
        ("DEBIAN_FRONTEND", "noninteractive"),
    ]
}

fn drain_stderr(stderr: Option<tokio::process::ChildStderr>, name: String) {
    let Some(stderr) = stderr else {
        return;
    };

    tokio::spawn(async move {
        let mut lines = BufReader::new(stderr).lines();
        loop {
            match lines.next_line().await {
                Ok(Some(line)) => {
                    let trimmed = line.trim();
                    if !trimmed.is_empty() {
                        warn!(agent = %name, stderr = %trimmed, "agent stderr output");
                    }
                }
                Ok(None) => break,
                Err(error) => {
                    warn!(agent = %name, error = %error, "error reading agent stderr");
                    break;
                }
            }
        }
    });
}

impl Drop for StdioProcess {
    fn drop(&mut self) {
        self.cancel.cancel();
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use futures::StreamExt;

    #[tokio::test]
    async fn spawn_echo_agent() {
        // Spawn a simple echo script that outputs NDJSON
        let config = StdioConfig {
            binary: "sh".into(),
            args: vec![
                "-c".into(),
                r#"echo '{"type":"system","message":"init"}'; echo '{"type":"assistant","content":[{"type":"text","text":"hello"}]}'; echo '{"type":"result","result":"done","subtype":"success"}'"#.into(),
            ],
            env: HashMap::new(),
            env_remove: Vec::new(),
            cwd: None,
            name: "test-echo".into(),
        };

        let process = StdioProcess::spawn(&config).unwrap();
        let stream = process.message_stream().await.unwrap();
        let messages: Vec<AgentMessage> = stream.collect().await;

        assert_eq!(messages.len(), 3);
        assert!(matches!(&messages[0], AgentMessage::System { message, .. } if message == "init"));
        assert_eq!(messages[1].text(), Some("hello"));
        assert!(messages[2].is_result());

        let code = process.wait().await.unwrap();
        assert_eq!(code, 0);
    }

    #[tokio::test]
    async fn cancel_kills_process() {
        let config = StdioConfig {
            binary: "sh".into(),
            args: vec!["-c".into(), "sleep 60".into()],
            env: HashMap::new(),
            env_remove: Vec::new(),
            cwd: None,
            name: "test-sleep".into(),
        };

        let process = StdioProcess::spawn(&config).unwrap();
        process.kill().await;
        // Should return quickly after kill
        let code = process.wait().await.unwrap();
        assert_ne!(code, 0);
    }

    #[tokio::test]
    async fn unparseable_lines_are_skipped() {
        let config = StdioConfig {
            binary: "sh".into(),
            args: vec![
                "-c".into(),
                r#"echo 'not json'; echo ''; echo '{"type":"result","result":"ok"}'"#.into(),
            ],
            env: HashMap::new(),
            env_remove: Vec::new(),
            cwd: None,
            name: "test-skip".into(),
        };

        let process = StdioProcess::spawn(&config).unwrap();
        let stream = process.message_stream().await.unwrap();
        let messages: Vec<AgentMessage> = stream.collect().await;

        // Only the valid JSON line should produce a message
        assert_eq!(messages.len(), 1);
        assert!(messages[0].is_result());
    }

    #[tokio::test]
    async fn kill_is_eventually_observed_even_if_called_inline() {
        let config = StdioConfig {
            binary: "sh".into(),
            args: vec!["-c".into(), "sleep 60".into()],
            env: HashMap::new(),
            env_remove: Vec::new(),
            cwd: None,
            name: "test-kill-inline".into(),
        };

        let process = StdioProcess::spawn(&config).unwrap();
        process.kill().await;
        let code = tokio::time::timeout(std::time::Duration::from_secs(5), process.wait())
            .await
            .expect("wait should finish after kill")
            .expect("wait succeeds");
        assert_ne!(code, 0);
    }
}
