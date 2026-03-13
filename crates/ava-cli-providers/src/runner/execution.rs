use std::time::Instant;

use tokio::io::{AsyncBufReadExt, BufReader};
use tokio::process::Command;
use tokio::sync::mpsc;

use std::process::Stdio;

use ava_types::{AvaError, Result};

use super::{CLIAgentRunner, RunOptions};
use crate::config::{CLIAgentEvent, CLIAgentResult, TokenUsage};

impl CLIAgentRunner {
    /// Parse a line of stream-json output into an event.
    pub(crate) fn parse_event(line: &str) -> Option<CLIAgentEvent> {
        serde_json::from_str(line).ok()
    }

    pub(super) async fn run_internal(
        &self,
        options: RunOptions,
        tx: Option<mpsc::Sender<CLIAgentEvent>>,
    ) -> Result<CLIAgentResult> {
        let args = self.build_args(&options);
        let mut cmd = Command::new(&self.config.binary);
        cmd.args(&args)
            .stdout(Stdio::piped())
            .stderr(Stdio::piped());
        if self.config.cwd_flag.is_none() {
            cmd.current_dir(&options.cwd);
        }

        if let Some(env) = &options.env {
            for (key, val) in env {
                cmd.env(key, val);
            }
        }

        let started = Instant::now();
        let mut child = cmd.spawn().map_err(|e| {
            AvaError::PlatformError(format!("failed to spawn {}: {e}", self.config.binary))
        })?;

        let stdout = child
            .stdout
            .take()
            .ok_or_else(|| AvaError::PlatformError("failed to capture stdout".to_string()))?;
        let stderr = child
            .stderr
            .take()
            .ok_or_else(|| AvaError::PlatformError("failed to capture stderr".to_string()))?;

        let supports_json = self.config.supports_stream_json;
        let tx_stdout = tx.clone();
        let stdout_task = tokio::spawn(async move {
            let mut lines = BufReader::new(stdout).lines();
            let mut output = String::new();
            let mut events = Vec::new();
            let mut tokens = None;

            while let Ok(Some(line)) = lines.next_line().await {
                if supports_json {
                    if let Some(event) = CLIAgentRunner::parse_event(&line) {
                        if let CLIAgentEvent::Usage {
                            input_tokens,
                            output_tokens,
                        } = &event
                        {
                            tokens = Some(TokenUsage {
                                input: *input_tokens,
                                output: *output_tokens,
                            });
                        }

                        if let CLIAgentEvent::Text { content } = &event {
                            output.push_str(content);
                            output.push('\n');
                        }

                        if let Some(sender) = &tx_stdout {
                            let _ = sender.send(event.clone()).await;
                        }

                        events.push(event);
                    }
                } else {
                    output.push_str(&line);
                    output.push('\n');
                    let event = CLIAgentEvent::Text { content: line };
                    if let Some(sender) = &tx_stdout {
                        let _ = sender.send(event.clone()).await;
                    }
                    events.push(event);
                }
            }

            (output, events, tokens)
        });

        let stderr_task = tokio::spawn(async move {
            let mut lines = BufReader::new(stderr).lines();
            let mut output = String::new();
            while let Ok(Some(line)) = lines.next_line().await {
                if !line.is_empty() {
                    output.push_str(&line);
                    output.push('\n');
                }
            }
            output
        });

        let status = {
            let mut timed_out = false;
            let mut cancelled = false;
            let wait_result = if let Some(timeout_ms) = options.timeout_ms {
                tokio::select! {
                    result = child.wait() => Some(result),
                    _ = tokio::time::sleep(std::time::Duration::from_millis(timeout_ms)) => {
                        timed_out = true;
                        None
                    }
                    _ = self.cancel.cancelled() => {
                        cancelled = true;
                        None
                    }
                }
            } else {
                tokio::select! {
                    result = child.wait() => Some(result),
                    _ = self.cancel.cancelled() => {
                        cancelled = true;
                        None
                    }
                }
            };

            if let Some(result) = wait_result {
                result.map_err(|e| {
                    AvaError::PlatformError(format!("failed waiting for child process: {e}"))
                })?
            } else {
                let _ = child.kill().await;
                if timed_out {
                    return Err(AvaError::TimeoutError(format!(
                        "CLI agent '{}' timed out after {}ms",
                        self.config.name,
                        options.timeout_ms.unwrap_or_default()
                    )));
                }

                if cancelled {
                    return Err(AvaError::TimeoutError(
                        "CLI agent execution cancelled".to_string(),
                    ));
                }

                return Err(AvaError::PlatformError(
                    "CLI agent stopped for unknown reason".to_string(),
                ));
            }
        };

        let (mut output, events, tokens_used) = stdout_task
            .await
            .map_err(|e| AvaError::PlatformError(format!("stdout task failed: {e}")))?;
        let stderr_output = stderr_task
            .await
            .map_err(|e| AvaError::PlatformError(format!("stderr task failed: {e}")))?;

        let success = status.success();
        if !success && !stderr_output.is_empty() {
            if !output.is_empty() {
                output.push('\n');
            }
            output.push_str(&stderr_output);
        }

        let exit_code = status.code().unwrap_or(-1);
        let duration_ms = started.elapsed().as_millis() as u64;
        Ok(CLIAgentResult {
            success,
            output: output.trim().to_string(),
            exit_code,
            events,
            tokens_used,
            duration_ms,
        })
    }
}
