//! Legacy CLI agent adapter.
//!
//! Wraps agents that emit Codex/OpenCode JSONL or plain text output and maps
//! them to the shared `AgentMessage` stream used by AVA.

use std::collections::HashMap;
use std::sync::Arc;

use async_trait::async_trait;
use ava_types::{AvaError, Result};
use serde_json::Value;
use tokio::io::{AsyncBufReadExt, BufReader};
use tokio::process::ChildStdout;
use tokio::sync::Mutex;
use tracing::debug;

use crate::protocol::{
    AgentMessage, AgentQuery, AgentResultDetails, AgentUsage, ContentBlock, PermissionMode,
};
use crate::stdio::{StdioConfig, StdioProcess};
use crate::transport::{AgentMessageStream, AgentTransport};

use super::config::{AgentConfig, AgentProtocol, NESTING_GUARD_ENV_VARS};

/// Adapter for legacy CLI agents (Codex/OpenCode JSONL or plain text).
#[derive(Clone)]
pub struct LegacyCliAdapter {
    config: AgentConfig,
    process: Arc<Mutex<Option<Arc<StdioProcess>>>>,
}

impl LegacyCliAdapter {
    pub fn new(config: AgentConfig) -> Self {
        Self {
            config,
            process: Arc::new(Mutex::new(None)),
        }
    }

    fn build_args(&self, query: &AgentQuery) -> Vec<String> {
        match self.config.protocol {
            AgentProtocol::CodexJsonl => self.build_codex_args(query),
            AgentProtocol::OpenCodeJsonl => self.build_opencode_args(query),
            AgentProtocol::GeminiCliJsonl => self.build_gemini_cli_args(query),
            AgentProtocol::PlainText => self.build_plain_text_args(query),
            AgentProtocol::SdkV1 => Vec::new(),
        }
    }

    fn build_plain_text_args(&self, query: &AgentQuery) -> Vec<String> {
        let mut args = self.config.headless_args.clone();

        if let Some(flag) = &self.config.prompt_flag {
            args.push(flag.clone());
            args.push(query.prompt.clone());
        } else {
            args.push(query.prompt.clone());
        }

        if let (Some(flag), Some(model)) = (&self.config.model_flag, &query.model) {
            args.push(flag.clone());
            args.push(model.clone());
        }

        if let (Some(flag), Some(cwd)) = (&self.config.cwd_flag, &query.working_directory) {
            args.push(flag.clone());
            args.push(cwd.clone());
        }

        if let (Some(flag), Some(session_id)) = (&self.config.session_flag, &query.session_id) {
            if query.resume {
                args.push(flag.clone());
                args.push(session_id.clone());
            }
        }

        args.extend(self.config.trailing_args.iter().cloned());
        args
    }

    fn build_codex_args(&self, query: &AgentQuery) -> Vec<String> {
        let mut args = self.config.headless_args.clone();

        if let Some(model) = &query.model {
            if let Some(flag) = &self.config.model_flag {
                args.push(flag.clone());
                args.push(model.clone());
            }
        }

        if query.resume {
            if let Some(session_id) = &query.session_id {
                // Codex resumes with a positional `resume <session-id>` segment,
                // not a `--session <id>` style flag.
                args.push("resume".into());
                args.push(session_id.clone());
            }
        }

        args.extend(self.config.trailing_args.iter().cloned());
        args.push("-".into());
        args
    }

    fn build_opencode_args(&self, query: &AgentQuery) -> Vec<String> {
        let mut args = self.config.headless_args.clone();

        if let Some(model) = &query.model {
            if let Some(flag) = &self.config.model_flag {
                args.push(flag.clone());
                args.push(model.clone());
            }
        }

        if let (Some(flag), Some(cwd)) = (&self.config.cwd_flag, &query.working_directory) {
            args.push(flag.clone());
            args.push(cwd.clone());
        }

        if let (Some(flag), Some(session_id)) = (&self.config.session_flag, &query.session_id) {
            if query.resume {
                args.push(flag.clone());
                args.push(session_id.clone());
            }
        }

        args.extend(self.config.trailing_args.iter().cloned());
        args.push(query.prompt.clone());
        args
    }

    fn build_gemini_cli_args(&self, query: &AgentQuery) -> Vec<String> {
        let mut args = self.config.headless_args.clone();

        if let Some(flag) = &self.config.prompt_flag {
            args.push(flag.clone());
            args.push(query.prompt.clone());
        } else {
            args.push(query.prompt.clone());
        }

        if let (Some(flag), Some(model)) = (&self.config.model_flag, &query.model) {
            args.push(flag.clone());
            args.push(model.clone());
        }

        if let (Some(flag), Some(cwd)) = (&self.config.cwd_flag, &query.working_directory) {
            args.push(flag.clone());
            args.push(cwd.clone());
        }

        if let (Some(flag), Some(session_id)) = (&self.config.session_flag, &query.session_id) {
            if query.resume {
                args.push(flag.clone());
                args.push(session_id.clone());
            }
        }

        if let Some(flag) = &self.config.permission_mode_flag {
            if let Some(mode) = &query.permission_mode {
                let gemini_mode = match mode {
                    PermissionMode::BypassPermissions => "yolo",
                    PermissionMode::AcceptEdits => "auto_edit",
                    _ => "default",
                };
                args.push(flag.clone());
                args.push(gemini_mode.into());
            }
        }

        args.extend(self.config.trailing_args.iter().cloned());
        args
    }

    async fn spawn_attempt(&self, query: &AgentQuery) -> Result<(Arc<StdioProcess>, ChildStdout)> {
        let args = self.build_args(query);
        let cwd = query.working_directory.clone();

        let stdio_config = StdioConfig {
            binary: self.config.binary.clone(),
            args,
            env: HashMap::new(),
            env_remove: NESTING_GUARD_ENV_VARS
                .iter()
                .map(|s| s.to_string())
                .collect(),
            cwd,
            name: self.config.name.clone(),
        };

        let process = Arc::new(StdioProcess::spawn(&stdio_config)?);
        if self.config.protocol == AgentProtocol::CodexJsonl {
            process.write_stdin(&query.prompt).await.map_err(|error| {
                AvaError::ToolError(format!(
                    "failed to send prompt to {} stdin: {error}",
                    self.config.name
                ))
            })?;
        }

        let stdout = process
            .take_stdout()
            .await
            .ok_or_else(|| AvaError::PlatformError("failed to take agent stdout".into()))?;
        *self.process.lock().await = Some(Arc::clone(&process));
        Ok((process, stdout))
    }
}

#[async_trait]
impl AgentTransport for LegacyCliAdapter {
    async fn query(&self, query: AgentQuery) -> Result<AgentMessageStream> {
        let protocol = self.config.protocol;
        let adapter = self.clone();
        let stream_query = query.clone();

        let stream = async_stream::stream! {
            let mut attempt_query = stream_query;
            let mut retries_remaining = usize::from(attempt_query.resume && supports_stale_retry(protocol));

            'attempt: loop {
                let (process, stdout) = match adapter.spawn_attempt(&attempt_query).await {
                    Ok(attempt) => attempt,
                    Err(error) => {
                        yield AgentMessage::Error { message: error.to_string(), code: None };
                        break;
                    }
                };

                let reader = BufReader::new(stdout);
                let mut lines = reader.lines();
                let mut plain_text_output = String::new();
                let mut buffered = Vec::new();
                let mut flushed = protocol == AgentProtocol::PlainText;
                let mut retry_fresh = false;

                while let Ok(Some(line)) = lines.next_line().await {
                    match protocol {
                        AgentProtocol::PlainText => {
                            plain_text_output.push_str(&line);
                            plain_text_output.push('\n');
                            yield AgentMessage::Assistant {
                                content: vec![ContentBlock::Text { text: line }],
                                session_id: None,
                            };
                        }
                        AgentProtocol::CodexJsonl
                        | AgentProtocol::OpenCodeJsonl
                        | AgentProtocol::GeminiCliJsonl => {
                            let parsed = match protocol {
                                AgentProtocol::CodexJsonl => parse_codex_jsonl_line(&line),
                                AgentProtocol::OpenCodeJsonl => parse_opencode_jsonl_line(&line),
                                AgentProtocol::GeminiCliJsonl => parse_gemini_cli_jsonl_line(&line),
                                _ => None,
                            };

                            if let Some(msg) = parsed {
                                if attempt_query.resume
                                    && retries_remaining > 0
                                    && is_unknown_session_message(&msg)
                                {
                                    retry_fresh = true;
                                    retries_remaining -= 1;
                                    break;
                                }

                                if flushed {
                                    yield msg;
                                } else {
                                    buffered.push(msg.clone());
                                    if is_live_progress_message(&msg) {
                                        flushed = true;
                                        for buffered_msg in buffered.drain(..) {
                                            yield buffered_msg;
                                        }
                                    }
                                }
                            }
                        }
                        AgentProtocol::SdkV1 => {}
                    }
                }

                if retry_fresh {
                    process.kill().await;
                    attempt_query.resume = false;
                    attempt_query.session_id = None;
                    continue 'attempt;
                }

                if protocol == AgentProtocol::PlainText {
                    yield AgentMessage::Result {
                        result: plain_text_output,
                        details: AgentResultDetails::default(),
                    };
                } else {
                    for buffered_msg in buffered.drain(..) {
                        yield buffered_msg;
                    }
                }

                let mut guard = adapter.process.lock().await;
                if guard.as_ref().is_some_and(|active| Arc::ptr_eq(active, &process)) {
                    *guard = None;
                }
                break;
            }
        };

        Ok(Box::pin(stream))
    }

    async fn cancel(&self) -> Result<()> {
        let mut guard = self.process.lock().await;
        if let Some(process) = guard.take() {
            process.kill().await;
            debug!(agent = %self.config.name, "cancelled legacy agent");
        }
        Ok(())
    }

    fn name(&self) -> &str {
        &self.config.name
    }
}

fn parse_codex_jsonl_line(line: &str) -> Option<AgentMessage> {
    let event: Value = serde_json::from_str(line).ok()?;
    let event_type = event.get("type")?.as_str()?;

    match event_type {
        "thread.started" => Some(AgentMessage::System {
            message: "thread.started".into(),
            session_id: string_at(
                &event,
                &["thread_id", "threadId", "session_id", "sessionId"],
            ),
        }),
        "item.completed" => parse_codex_item_completed(&event),
        "turn.completed" => Some(AgentMessage::Result {
            result: string_at(&event, &["summary", "message"]).unwrap_or_default(),
            details: AgentResultDetails {
                session_id: string_at(
                    &event,
                    &["thread_id", "threadId", "session_id", "sessionId"],
                ),
                total_cost_usd: number_at(&event, &["cost_usd", "costUsd"]),
                usage: parse_codex_usage(&event),
                subtype: Some("success".into()),
            },
        }),
        "turn.failed" | "error" => Some(AgentMessage::Error {
            message: string_at(&event, &["message", "error.message", "error"])
                .unwrap_or_else(|| "Codex run failed".into()),
            code: event.get("code").and_then(Value::as_i64).map(|v| v as i32),
        }),
        _ => None,
    }
}

fn parse_codex_item_completed(event: &Value) -> Option<AgentMessage> {
    let item = event.get("item")?;
    let item_type = item.get("type")?.as_str()?;

    match item_type {
        "agent_message" | "message" => {
            extract_content_text(item).map(|text| AgentMessage::Assistant {
                content: vec![ContentBlock::Text { text }],
                session_id: string_at(event, &["thread_id", "threadId", "session_id", "sessionId"]),
            })
        }
        "reasoning" => extract_content_text(item).map(|thinking| AgentMessage::Assistant {
            content: vec![ContentBlock::Thinking { thinking }],
            session_id: string_at(event, &["thread_id", "threadId", "session_id", "sessionId"]),
        }),
        "command_execution" => {
            let id = string_at(item, &["id"]).unwrap_or_else(|| "codex-tool".into());
            let name = string_at(item, &["name", "command", "title"])
                .unwrap_or_else(|| "command_execution".into());
            Some(AgentMessage::Assistant {
                content: vec![ContentBlock::ToolUse {
                    id,
                    name,
                    input: item.clone(),
                }],
                session_id: string_at(event, &["thread_id", "threadId", "session_id", "sessionId"]),
            })
        }
        "file_change" => extract_content_text(item).map(|text| AgentMessage::Assistant {
            content: vec![ContentBlock::Text { text }],
            session_id: string_at(event, &["thread_id", "threadId", "session_id", "sessionId"]),
        }),
        _ => None,
    }
}

fn parse_codex_usage(event: &Value) -> Option<AgentUsage> {
    let usage = event.get("usage")?;
    Some(AgentUsage {
        input_tokens: usage
            .get("input_tokens")
            .and_then(Value::as_u64)
            .unwrap_or(0),
        output_tokens: usage
            .get("output_tokens")
            .and_then(Value::as_u64)
            .unwrap_or(0),
        cache_creation_input_tokens: usage
            .get("cache_creation_input_tokens")
            .and_then(Value::as_u64),
        cache_read_input_tokens: usage
            .get("cached_input_tokens")
            .or_else(|| usage.get("cache_read_input_tokens"))
            .and_then(Value::as_u64),
    })
}

fn parse_opencode_jsonl_line(line: &str) -> Option<AgentMessage> {
    let event: Value = serde_json::from_str(line).ok()?;
    let event_type = event.get("type")?.as_str()?;
    let session_id = string_at(&event, &["sessionID", "sessionId", "session_id"]);

    match event_type {
        "text" => extract_content_text(event.get("part").unwrap_or(&event)).map(|text| {
            AgentMessage::Assistant {
                content: vec![ContentBlock::Text { text }],
                session_id,
            }
        }),
        "tool_use" => {
            let part = event.get("part").unwrap_or(&event);
            let id = string_at(part, &["id"]).unwrap_or_else(|| "opencode-tool".into());
            let name =
                string_at(part, &["name", "tool", "title"]).unwrap_or_else(|| "tool_use".into());
            Some(AgentMessage::Assistant {
                content: vec![ContentBlock::ToolUse {
                    id,
                    name,
                    input: part.clone(),
                }],
                session_id,
            })
        }
        "step_finish" => {
            let part = event.get("part").unwrap_or(&event);
            Some(AgentMessage::Result {
                result: string_at(part, &["summary", "text"]).unwrap_or_default(),
                details: AgentResultDetails {
                    session_id,
                    total_cost_usd: number_at(part, &["cost"]),
                    usage: parse_opencode_usage(part),
                    subtype: Some("success".into()),
                },
            })
        }
        "error" => Some(AgentMessage::Error {
            message: string_at(&event, &["error", "message"])
                .unwrap_or_else(|| "OpenCode run failed".into()),
            code: event.get("code").and_then(Value::as_i64).map(|v| v as i32),
        }),
        _ => None,
    }
}

fn parse_opencode_usage(part: &Value) -> Option<AgentUsage> {
    let tokens = part.get("tokens")?;
    let cache = tokens.get("cache");
    Some(AgentUsage {
        input_tokens: tokens.get("input").and_then(Value::as_u64).unwrap_or(0),
        output_tokens: tokens.get("output").and_then(Value::as_u64).unwrap_or(0),
        cache_creation_input_tokens: cache.and_then(|c| c.get("write")).and_then(Value::as_u64),
        cache_read_input_tokens: cache.and_then(|c| c.get("read")).and_then(Value::as_u64),
    })
}

fn parse_gemini_cli_jsonl_line(line: &str) -> Option<AgentMessage> {
    let event: Value = serde_json::from_str(line).ok()?;
    let event_type = event.get("type")?.as_str()?;

    match event_type {
        "init" => Some(AgentMessage::System {
            message: "init".into(),
            session_id: string_at(&event, &["session_id", "sessionId"]),
        }),
        "message" => {
            let role = string_at(&event, &["role"]);
            if role.as_deref() == Some("user") {
                return None;
            }
            extract_content_text(&event).map(|text| AgentMessage::Assistant {
                content: vec![ContentBlock::Text { text }],
                session_id: string_at(&event, &["session_id", "sessionId"]),
            })
        }
        "tool_use" => {
            let id = string_at(&event, &["tool_id", "id", "toolId"])
                .unwrap_or_else(|| "gemini-tool".into());
            let name = string_at(&event, &["tool_name", "name", "toolName"])
                .unwrap_or_else(|| "tool_use".into());
            let input = event
                .get("parameters")
                .or_else(|| event.get("args"))
                .or_else(|| event.get("input"))
                .cloned()
                .unwrap_or(Value::Object(Default::default()));
            Some(AgentMessage::Assistant {
                content: vec![ContentBlock::ToolUse { id, name, input }],
                session_id: string_at(&event, &["session_id", "sessionId"]),
            })
        }
        "tool_result" => {
            let tool_use_id = string_at(&event, &["tool_id", "toolUseId", "id"])
                .unwrap_or_else(|| "gemini-tool".into());
            let content = string_at(&event, &["output", "result", "content"]).unwrap_or_default();
            let is_error = string_at(&event, &["status"]).is_some_and(|s| s != "success")
                || event
                    .get("isError")
                    .and_then(Value::as_bool)
                    .unwrap_or(false);
            Some(AgentMessage::Assistant {
                content: vec![ContentBlock::ToolResult {
                    tool_use_id,
                    content,
                    is_error,
                }],
                session_id: string_at(&event, &["session_id", "sessionId"]),
            })
        }
        "result" => {
            let status = string_at(&event, &["status"]).unwrap_or_default();
            Some(AgentMessage::Result {
                result: status.clone(),
                details: AgentResultDetails {
                    session_id: string_at(&event, &["session_id", "sessionId"]),
                    total_cost_usd: number_at(&event, &["stats.cost", "cost"]),
                    usage: parse_gemini_cli_usage(&event),
                    subtype: Some(status),
                },
            })
        }
        "error" => Some(AgentMessage::Error {
            message: string_at(&event, &["message", "error", "error.message"])
                .unwrap_or_else(|| "Gemini CLI run failed".into()),
            code: event.get("code").and_then(Value::as_i64).map(|v| v as i32),
        }),
        _ => None,
    }
}

fn parse_gemini_cli_usage(event: &Value) -> Option<AgentUsage> {
    let stats = event.get("stats")?;
    Some(AgentUsage {
        input_tokens: stats
            .get("input_tokens")
            .or_else(|| stats.get("inputTokens"))
            .and_then(Value::as_u64)
            .unwrap_or(0),
        output_tokens: stats
            .get("output_tokens")
            .or_else(|| stats.get("outputTokens"))
            .and_then(Value::as_u64)
            .unwrap_or(0),
        cache_creation_input_tokens: None,
        cache_read_input_tokens: stats
            .get("cached")
            .and_then(Value::as_u64)
            .filter(|&v| v > 0),
    })
}

fn supports_stale_retry(protocol: AgentProtocol) -> bool {
    matches!(
        protocol,
        AgentProtocol::CodexJsonl | AgentProtocol::OpenCodeJsonl | AgentProtocol::GeminiCliJsonl
    )
}

fn is_unknown_session_message(message: &AgentMessage) -> bool {
    let text = match message {
        AgentMessage::Error { message, .. } => message.as_str(),
        AgentMessage::Result { result, .. } => result.as_str(),
        _ => return false,
    };
    let lower = text.to_lowercase();
    [
        "unknown session",
        "session is unavailable",
        "session not found",
        "no conversation found",
        "thread not found",
        "invalid session",
    ]
    .iter()
    .any(|needle| lower.contains(needle))
}

fn is_live_progress_message(message: &AgentMessage) -> bool {
    match message {
        AgentMessage::Assistant { .. }
        | AgentMessage::System { .. }
        | AgentMessage::Result { .. } => true,
        AgentMessage::Error { .. } => false,
        AgentMessage::Unknown => false,
    }
}

fn string_at(root: &Value, paths: &[&str]) -> Option<String> {
    paths
        .iter()
        .find_map(|path| lookup_path(root, path).and_then(value_to_string))
}

fn number_at(root: &Value, paths: &[&str]) -> Option<f64> {
    paths
        .iter()
        .find_map(|path| lookup_path(root, path).and_then(Value::as_f64))
}

fn lookup_path<'a>(root: &'a Value, path: &str) -> Option<&'a Value> {
    let mut current = root;
    for part in path.split('.') {
        current = current.get(part)?;
    }
    Some(current)
}

fn value_to_string(value: &Value) -> Option<String> {
    match value {
        Value::String(s) => Some(s.clone()),
        Value::Number(n) => Some(n.to_string()),
        Value::Bool(b) => Some(b.to_string()),
        _ => None,
    }
}

fn extract_content_text(value: &Value) -> Option<String> {
    if let Some(text) = string_at(value, &["text", "summary", "content"]) {
        if !text.is_empty() {
            return Some(text);
        }
    }

    value
        .get("content")
        .and_then(Value::as_array)
        .map(|parts| {
            parts
                .iter()
                .filter_map(|part| string_at(part, &["text", "output_text"]))
                .collect::<Vec<_>>()
                .join("\n")
        })
        .filter(|text| !text.is_empty())
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::adapters::config::builtin_agents;
    use crate::stdio::StdioConfig;

    #[test]
    fn build_args_with_prompt_flag() {
        let config = builtin_agents()
            .into_iter()
            .find(|a| a.name == "aider")
            .unwrap();
        let adapter = LegacyCliAdapter::new(config);

        let query = AgentQuery::simple("fix bug");
        let args = adapter.build_args(&query);

        assert!(args.contains(&"--message".to_string()));
        assert!(args.contains(&"fix bug".to_string()));
    }

    #[test]
    fn build_args_for_codex_uses_exec_json_and_stdin_placeholder() {
        let config = builtin_agents()
            .into_iter()
            .find(|a| a.name == "codex")
            .unwrap();
        let adapter = LegacyCliAdapter::new(config);

        let mut query = AgentQuery::simple("fix bug");
        query.model = Some("gpt-5-codex".into());
        let args = adapter.build_args(&query);

        assert_eq!(args[0], "exec");
        assert!(args.contains(&"--json".to_string()));
        assert!(args.contains(&"--model".to_string()));
        assert_eq!(args.last().map(String::as_str), Some("-"));
    }

    #[test]
    fn build_args_for_opencode_uses_run_json_and_dir() {
        let config = builtin_agents()
            .into_iter()
            .find(|a| a.name == "opencode")
            .unwrap();
        let adapter = LegacyCliAdapter::new(config);

        let mut query = AgentQuery::simple("fix bug");
        query.model = Some("openai/gpt-5.4".into());
        query.working_directory = Some("/tmp/project".into());
        let args = adapter.build_args(&query);

        assert_eq!(args[0], "run");
        assert!(args.contains(&"--format".to_string()));
        assert!(args.contains(&"json".to_string()));
        assert!(args.contains(&"--dir".to_string()));
        assert_eq!(args.last().map(String::as_str), Some("fix bug"));
    }

    #[test]
    fn parse_codex_jsonl_extracts_session_and_usage() {
        let started =
            parse_codex_jsonl_line(r#"{"type":"thread.started","thread_id":"thread-123"}"#)
                .expect("thread.started should parse");
        assert!(
            matches!(started, AgentMessage::System { session_id: Some(ref id), .. } if id == "thread-123")
        );

        let completed = parse_codex_jsonl_line(
            r#"{"type":"turn.completed","summary":"done","usage":{"input_tokens":10,"cached_input_tokens":3,"output_tokens":5}}"#,
        )
        .expect("turn.completed should parse");
        match completed {
            AgentMessage::Result { details, .. } => {
                let usage = details.usage.expect("usage");
                assert_eq!(usage.input_tokens, 10);
                assert_eq!(usage.cache_read_input_tokens, Some(3));
                assert_eq!(usage.output_tokens, 5);
            }
            _ => panic!("expected result event"),
        }
    }

    #[test]
    fn parse_opencode_jsonl_extracts_cost_and_usage() {
        let msg = parse_opencode_jsonl_line(
            r#"{"type":"step_finish","sessionID":"sess-1","part":{"cost":0.12,"tokens":{"input":7,"output":9,"cache":{"read":2}}}}"#,
        )
        .expect("step_finish should parse");

        match msg {
            AgentMessage::Result { details, .. } => {
                assert_eq!(details.session_id.as_deref(), Some("sess-1"));
                assert_eq!(details.total_cost_usd, Some(0.12));
                let usage = details.usage.expect("usage");
                assert_eq!(usage.input_tokens, 7);
                assert_eq!(usage.output_tokens, 9);
                assert_eq!(usage.cache_read_input_tokens, Some(2));
            }
            _ => panic!("expected result event"),
        }
    }

    #[tokio::test]
    async fn codex_retry_clears_stale_session_and_retries_fresh() {
        use futures::StreamExt;

        let config = AgentConfig {
            name: "codex-test".into(),
            binary: "sh".into(),
            protocol: AgentProtocol::CodexJsonl,
            headless_args: vec![
                "-c".into(),
                "IFS= read -r _ || true; stale=0; for arg in \"$@\"; do if [ \"$arg\" = \"resume\" ]; then stale=1; fi; done; if [ \"$stale\" = \"1\" ]; then echo '{\"type\":\"error\",\"message\":\"unknown session\"}'; else echo '{\"type\":\"thread.started\",\"thread_id\":\"fresh-thread\"}'; echo '{\"type\":\"turn.completed\",\"summary\":\"done\"}'; fi".into(),
                "sh".into(),
            ],
            trailing_args: vec![],
            prompt_flag: None,
            model_flag: None,
            cwd_flag: None,
            session_flag: None,
            max_turns_flag: None,
            permission_mode_flag: None,
            max_budget_flag: None,
            version_command: vec![],
        };

        let adapter = LegacyCliAdapter::new(config);
        let mut query = AgentQuery::simple("hello world");
        query.resume = true;
        query.session_id = Some("stale-thread".into());

        let messages: Vec<AgentMessage> = adapter.query(query).await.unwrap().collect().await;

        assert!(messages.iter().any(|msg| matches!(msg, AgentMessage::System { session_id: Some(id), .. } if id == "fresh-thread")));
        assert!(messages
            .iter()
            .any(|msg| matches!(msg, AgentMessage::Result { result, .. } if result == "done")));
        assert!(!messages.iter().any(|msg| matches!(msg, AgentMessage::Error { message, .. } if message.contains("unknown session"))));
    }

    #[tokio::test]
    async fn opencode_retry_clears_stale_session_and_retries_fresh() {
        use futures::StreamExt;

        let config = AgentConfig {
            name: "opencode-test".into(),
            binary: "sh".into(),
            protocol: AgentProtocol::OpenCodeJsonl,
            headless_args: vec![
                "-c".into(),
                "stale=0; for arg in \"$@\"; do if [ \"$arg\" = \"--session\" ]; then stale=1; fi; done; if [ \"$stale\" = \"1\" ]; then echo '{\"type\":\"error\",\"message\":\"session not found\"}'; else echo '{\"type\":\"text\",\"sessionID\":\"fresh-session\",\"part\":{\"text\":\"hello\"}}'; echo '{\"type\":\"step_finish\",\"sessionID\":\"fresh-session\",\"part\":{\"text\":\"done\",\"tokens\":{\"input\":1,\"output\":1}}}'; fi".into(),
                "sh".into(),
            ],
            trailing_args: vec![],
            prompt_flag: None,
            model_flag: None,
            cwd_flag: None,
            session_flag: Some("--session".into()),
            max_turns_flag: None,
            permission_mode_flag: None,
            max_budget_flag: None,
            version_command: vec![],
        };

        let adapter = LegacyCliAdapter::new(config);
        let mut query = AgentQuery::simple("hello world");
        query.resume = true;
        query.session_id = Some("stale-session".into());

        let messages: Vec<AgentMessage> = adapter.query(query).await.unwrap().collect().await;

        assert!(messages.iter().any(|msg| matches!(msg, AgentMessage::Assistant { session_id: Some(id), .. } if id == "fresh-session")));
        assert!(messages.iter().any(|msg| matches!(msg, AgentMessage::Result { details, .. } if details.session_id.as_deref() == Some("fresh-session"))));
        assert!(!messages.iter().any(|msg| matches!(msg, AgentMessage::Error { message, .. } if message.contains("session not found"))));
    }

    #[tokio::test]
    async fn plain_text_agent_wraps_output() {
        use futures::StreamExt;
        let config = AgentConfig {
            name: "test-echo".into(),
            binary: "echo".into(),
            protocol: AgentProtocol::PlainText,
            headless_args: vec![],
            trailing_args: vec![],
            prompt_flag: None,
            model_flag: None,
            cwd_flag: None,
            session_flag: None,
            max_turns_flag: None,
            permission_mode_flag: None,
            max_budget_flag: None,
            version_command: vec![],
        };

        let adapter = LegacyCliAdapter::new(config);
        let stream = adapter
            .query(AgentQuery::simple("hello world"))
            .await
            .unwrap();
        let messages: Vec<AgentMessage> = stream.collect().await;

        assert!(messages.len() >= 2);
        assert!(messages.last().unwrap().is_result());
    }

    #[tokio::test]
    async fn cancel_clears_stored_process() {
        let config = builtin_agents()
            .into_iter()
            .find(|a| a.name == "opencode")
            .unwrap();
        let adapter = LegacyCliAdapter::new(config);
        let process = Arc::new(
            StdioProcess::spawn(&StdioConfig {
                binary: "sh".into(),
                args: vec!["-c".into(), "sleep 60".into()],
                env: HashMap::new(),
                env_remove: Vec::new(),
                cwd: None,
                name: "test-sleep".into(),
            })
            .unwrap(),
        );
        *adapter.process.lock().await = Some(process);

        adapter.cancel().await.unwrap();

        assert!(adapter.process.lock().await.is_none());
    }

    #[test]
    fn build_args_for_gemini_cli_uses_stream_json_and_prompt_flag() {
        let config = builtin_agents()
            .into_iter()
            .find(|a| a.name == "gemini-cli")
            .unwrap();
        let adapter = LegacyCliAdapter::new(config);

        let mut query = AgentQuery::simple("fix bug");
        query.model = Some("pro".into());
        let args = adapter.build_args(&query);

        assert!(args.contains(&"--output-format".to_string()));
        assert!(args.contains(&"stream-json".to_string()));
        assert!(args.contains(&"-p".to_string()));
        assert!(args.contains(&"fix bug".to_string()));
        assert!(args.contains(&"--model".to_string()));
        assert!(args.contains(&"pro".to_string()));
    }

    #[test]
    fn build_args_for_gemini_cli_maps_permission_modes() {
        let config = builtin_agents()
            .into_iter()
            .find(|a| a.name == "gemini-cli")
            .unwrap();
        let adapter = LegacyCliAdapter::new(config);

        let mut query = AgentQuery::simple("fix bug");
        query.permission_mode = Some(PermissionMode::BypassPermissions);
        let args = adapter.build_args(&query);

        assert!(args.contains(&"--approval-mode".to_string()));
        assert!(args.contains(&"yolo".to_string()));
    }

    #[test]
    fn parse_gemini_cli_init_extracts_session() {
        let msg = parse_gemini_cli_jsonl_line(
            r#"{"type":"init","session_id":"60caf996-793b-46d8-bca7-d28bc435ae52","model":"auto-gemini-3"}"#,
        )
        .expect("init should parse");
        assert!(
            matches!(msg, AgentMessage::System { session_id: Some(ref id), .. } if id == "60caf996-793b-46d8-bca7-d28bc435ae52")
        );
    }

    #[test]
    fn parse_gemini_cli_message_extracts_text() {
        let msg = parse_gemini_cli_jsonl_line(
            r#"{"type":"message","role":"assistant","content":"Hello from Gemini","delta":true}"#,
        )
        .expect("message should parse");
        assert_eq!(msg.text(), Some("Hello from Gemini"));
    }

    #[test]
    fn parse_gemini_cli_skips_user_messages() {
        let msg =
            parse_gemini_cli_jsonl_line(r#"{"type":"message","role":"user","content":"fix bug"}"#);
        assert!(msg.is_none());
    }

    #[test]
    fn parse_gemini_cli_tool_use() {
        let msg = parse_gemini_cli_jsonl_line(
            r#"{"type":"tool_use","tool_name":"list_directory","tool_id":"list_directory_123","parameters":{"dir_path":"."}}"#,
        )
        .expect("tool_use should parse");
        match msg {
            AgentMessage::Assistant { content, .. } => {
                assert!(
                    matches!(&content[0], ContentBlock::ToolUse { name, .. } if name == "list_directory")
                );
                if let ContentBlock::ToolUse { id, input, .. } = &content[0] {
                    assert_eq!(id, "list_directory_123");
                    assert_eq!(input.get("dir_path").and_then(Value::as_str), Some("."));
                }
            }
            _ => panic!("expected Assistant"),
        }
    }

    #[test]
    fn parse_gemini_cli_tool_result() {
        let msg = parse_gemini_cli_jsonl_line(
            r#"{"type":"tool_result","tool_id":"list_directory_123","status":"success","output":"Listed 65 item(s)."}"#,
        )
        .expect("tool_result should parse");
        match msg {
            AgentMessage::Assistant { content, .. } => {
                assert!(
                    matches!(&content[0], ContentBlock::ToolResult { tool_use_id, content, is_error } if tool_use_id == "list_directory_123" && content == "Listed 65 item(s)." && !is_error)
                );
            }
            _ => panic!("expected Assistant"),
        }
    }

    #[test]
    fn parse_gemini_cli_tool_result_error_status() {
        let msg = parse_gemini_cli_jsonl_line(
            r#"{"type":"tool_result","tool_id":"t1","status":"error","output":"permission denied"}"#,
        )
        .expect("tool_result error should parse");
        match msg {
            AgentMessage::Assistant { content, .. } => {
                assert!(
                    matches!(&content[0], ContentBlock::ToolResult { is_error, .. } if *is_error)
                );
            }
            _ => panic!("expected Assistant"),
        }
    }

    #[test]
    fn parse_gemini_cli_result_with_usage() {
        let msg = parse_gemini_cli_jsonl_line(
            r#"{"type":"result","status":"success","stats":{"total_tokens":24813,"input_tokens":24088,"output_tokens":304,"cached":0,"duration_ms":6917,"tool_calls":1}}"#,
        )
        .expect("result should parse");

        match msg {
            AgentMessage::Result { result, details } => {
                assert_eq!(result, "success");
                assert_eq!(details.subtype.as_deref(), Some("success"));
                let usage = details.usage.expect("usage");
                assert_eq!(usage.input_tokens, 24088);
                assert_eq!(usage.output_tokens, 304);
                assert_eq!(usage.cache_read_input_tokens, None); // cached=0 filtered
            }
            _ => panic!("expected Result"),
        }
    }

    #[test]
    fn parse_gemini_cli_error() {
        let msg =
            parse_gemini_cli_jsonl_line(r#"{"type":"error","message":"rate limited","code":429}"#)
                .expect("error should parse");
        assert!(msg.is_error());
        match msg {
            AgentMessage::Error { message, code } => {
                assert_eq!(message, "rate limited");
                assert_eq!(code, Some(429));
            }
            _ => panic!("expected Error"),
        }
    }

    #[tokio::test]
    async fn gemini_cli_jsonl_stream_integration() {
        use futures::StreamExt;

        let config = AgentConfig {
            name: "gemini-test".into(),
            binary: "sh".into(),
            protocol: AgentProtocol::GeminiCliJsonl,
            headless_args: vec![
                "-c".into(),
                r#"echo '{"type":"init","session_id":"g1"}'; echo '{"type":"message","role":"assistant","content":"done"}'; echo '{"type":"result","status":"success","stats":{"input_tokens":5,"output_tokens":3,"total_tokens":8,"cached":0}}'"#.into(),
                "sh".into(),
            ],
            trailing_args: vec![],
            prompt_flag: None,
            model_flag: None,
            cwd_flag: None,
            session_flag: None,
            max_turns_flag: None,
            permission_mode_flag: None,
            max_budget_flag: None,
            version_command: vec![],
        };

        let adapter = LegacyCliAdapter::new(config);
        let messages: Vec<AgentMessage> = adapter
            .query(AgentQuery::simple("test"))
            .await
            .unwrap()
            .collect()
            .await;

        assert!(messages
            .iter()
            .any(|m| matches!(m, AgentMessage::System { session_id: Some(id), .. } if id == "g1")));
        assert!(messages.iter().any(|m| m.text() == Some("done")));
        assert!(messages.iter().any(|m| m.is_result()));
    }
}
