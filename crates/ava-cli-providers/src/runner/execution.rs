use std::time::Instant;

use tokio::io::{AsyncBufReadExt, BufReader};
use tokio::process::Command;
use tokio::sync::mpsc;

use std::process::Stdio;

use ava_types::{AvaError, Result};

use super::{CLIAgentRunner, RunOptions};
use crate::config::{CLIAgentEvent, CLIAgentResult, ContentBlock, TokenUsage};

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
        let supports_sdk = self.config.supports_agent_sdk_events;
        let tx_stdout = tx.clone();
        let stdout_task = tokio::spawn(async move {
            let mut lines = BufReader::new(stdout).lines();
            let mut output = String::new();
            let mut events = Vec::new();
            let mut tokens = None;
            let mut session_id: Option<String> = None;
            let mut total_cost_usd: Option<f64> = None;
            let mut result_subtype: Option<String> = None;

            while let Ok(Some(line)) = lines.next_line().await {
                if supports_json {
                    if let Some(event) = CLIAgentRunner::parse_event(&line) {
                        match &event {
                            CLIAgentEvent::Usage {
                                input_tokens,
                                output_tokens,
                            } => {
                                tokens = Some(TokenUsage {
                                    input: *input_tokens,
                                    output: *output_tokens,
                                });
                            }
                            CLIAgentEvent::Text { content } => {
                                output.push_str(content);
                                output.push('\n');
                            }
                            // Agent SDK: assistant messages with structured content
                            CLIAgentEvent::Assistant {
                                content,
                                session_id: sid,
                            } if supports_sdk => {
                                if session_id.is_none() {
                                    session_id.clone_from(sid);
                                }
                                for block in content {
                                    if let ContentBlock::Text { text } = block {
                                        output.push_str(text);
                                        output.push('\n');
                                    }
                                }
                            }
                            // Agent SDK: final result with cost/usage
                            CLIAgentEvent::Result {
                                result,
                                session_id: sid,
                                total_cost_usd: cost,
                                usage,
                                subtype,
                            } if supports_sdk => {
                                output.push_str(result);
                                if session_id.is_none() {
                                    session_id.clone_from(sid);
                                }
                                total_cost_usd = *cost;
                                result_subtype.clone_from(subtype);
                                if let Some(u) = usage {
                                    tokens = Some(TokenUsage {
                                        input: u.input_tokens,
                                        output: u.output_tokens,
                                    });
                                }
                            }
                            // Agent SDK: system messages
                            CLIAgentEvent::System {
                                session_id: sid, ..
                            } if supports_sdk => {
                                if session_id.is_none() {
                                    session_id.clone_from(sid);
                                }
                            }
                            _ => {}
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

            (
                output,
                events,
                tokens,
                session_id,
                total_cost_usd,
                result_subtype,
            )
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

        let (mut output, events, tokens_used, session_id, total_cost_usd, result_subtype) =
            stdout_task
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
            session_id,
            total_cost_usd,
            result_subtype,
        })
    }
}
