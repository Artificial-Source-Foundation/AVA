use std::path::{Path, PathBuf};

use ava_types::{ToolCall, ToolResult};
use chrono::Utc;
use serde::{Deserialize, Serialize};
use serde_json::{Map, Value};
use tokio::fs::{self, File, OpenOptions};
use tokio::io::AsyncWriteExt;

const DEFAULT_MAX_FIELD_BYTES: usize = 8_192;
const REDACTED: &str = "[REDACTED]";
const TRUNCATED_SUFFIX: &str = "...[truncated]";

#[derive(Debug, Clone, Serialize, Deserialize, Default)]
pub struct TrajectoryConfig {
    #[serde(default)]
    pub enabled: bool,
    #[serde(default)]
    pub output_dir: Option<PathBuf>,
    #[serde(default = "default_max_field_bytes")]
    pub max_field_bytes: usize,
    #[serde(default)]
    pub route: Option<TrajectoryRouteInfo>,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct TrajectoryRouteInfo {
    pub provider: String,
    pub model: String,
    pub source: String,
    #[serde(default)]
    pub reasons: Vec<String>,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct TrajectoryEvent {
    pub ts: String,
    pub session_id: String,
    pub seq: u64,
    #[serde(flatten)]
    pub kind: TrajectoryEventKind,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(tag = "type", rename_all = "snake_case")]
pub enum TrajectoryEventKind {
    SessionStart {
        model: String,
        route: Option<TrajectoryRouteInfo>,
    },
    TurnStart {
        turn: usize,
    },
    ModelResponse {
        turn: usize,
        text: String,
        tool_call_count: usize,
    },
    ToolCall {
        turn: usize,
        call_id: String,
        name: String,
        arguments: Value,
    },
    ToolResult {
        turn: usize,
        call_id: String,
        is_error: bool,
        content: String,
    },
    TokenUsage {
        turn: usize,
        input_tokens: usize,
        output_tokens: usize,
        cache_read_tokens: usize,
        cache_creation_tokens: usize,
        cost_usd: f64,
    },
    TurnEnd {
        turn: usize,
    },
    Termination {
        reason: String,
    },
}

pub struct TrajectoryRecorder {
    session_id: String,
    file_path: PathBuf,
    file: File,
    seq: u64,
    max_field_bytes: usize,
}

impl TrajectoryRecorder {
    pub async fn open(
        config: &TrajectoryConfig,
        session_id: &str,
    ) -> std::io::Result<Option<Self>> {
        if !config.enabled {
            return Ok(None);
        }

        let base = resolve_output_dir(config.output_dir.as_deref());
        fs::create_dir_all(&base).await?;
        let file_path = base.join(format!("{session_id}.jsonl"));
        let file = OpenOptions::new()
            .create(true)
            .write(true)
            .truncate(true)
            .open(&file_path)
            .await?;

        Ok(Some(Self {
            session_id: session_id.to_string(),
            file_path,
            file,
            seq: 0,
            max_field_bytes: config.max_field_bytes,
        }))
    }

    pub fn file_path(&self) -> &Path {
        &self.file_path
    }

    pub async fn write_event(&mut self, kind: TrajectoryEventKind) -> std::io::Result<()> {
        let event = TrajectoryEvent {
            ts: Utc::now().to_rfc3339(),
            session_id: self.session_id.clone(),
            seq: self.seq,
            kind,
        };
        self.seq += 1;
        let mut line = serde_json::to_vec(&event)?;
        line.push(b'\n');
        self.file.write_all(&line).await
    }

    pub fn sanitize_text(&self, text: &str) -> String {
        truncate_text(&redact_string(text), self.max_field_bytes)
    }

    pub fn sanitize_json(&self, value: &Value) -> Value {
        sanitize_json_value(value, self.max_field_bytes)
    }

    pub fn sanitize_tool_call(&self, call: &ToolCall) -> TrajectoryEventKind {
        TrajectoryEventKind::ToolCall {
            turn: 0,
            call_id: self.sanitize_text(&call.id),
            name: self.sanitize_text(&call.name),
            arguments: self.sanitize_json(&call.arguments),
        }
    }

    pub fn sanitize_tool_result(&self, result: &ToolResult) -> TrajectoryEventKind {
        TrajectoryEventKind::ToolResult {
            turn: 0,
            call_id: self.sanitize_text(&result.call_id),
            is_error: result.is_error,
            content: self.sanitize_text(&result.content),
        }
    }
}

fn default_max_field_bytes() -> usize {
    DEFAULT_MAX_FIELD_BYTES
}

fn resolve_output_dir(configured: Option<&Path>) -> PathBuf {
    if let Some(path) = configured {
        return path.to_path_buf();
    }
    std::env::current_dir()
        .unwrap_or_else(|_| PathBuf::from("."))
        .join(".ava")
        .join("trajectories")
}

fn sanitize_json_value(value: &Value, max_field_bytes: usize) -> Value {
    match value {
        Value::Object(map) => {
            let mut out = Map::with_capacity(map.len());
            for (key, value) in map {
                if is_sensitive_key(key) {
                    out.insert(key.clone(), Value::String(REDACTED.to_string()));
                } else {
                    out.insert(key.clone(), sanitize_json_value(value, max_field_bytes));
                }
            }
            Value::Object(out)
        }
        Value::Array(items) => Value::Array(
            items
                .iter()
                .map(|item| sanitize_json_value(item, max_field_bytes))
                .collect(),
        ),
        Value::String(text) => Value::String(truncate_text(&redact_string(text), max_field_bytes)),
        _ => value.clone(),
    }
}

fn is_sensitive_key(key: &str) -> bool {
    let lower = key.to_ascii_lowercase();
    [
        "key",
        "token",
        "secret",
        "password",
        "authorization",
        "auth",
        "credential",
        "cookie",
        "session",
    ]
    .iter()
    .any(|needle| lower.contains(needle))
}

fn redact_string(input: &str) -> String {
    if input.contains("-----BEGIN") && input.contains("PRIVATE KEY-----") {
        return REDACTED.to_string();
    }

    let mut text = input.to_string();
    for prefix in [
        "sk-",
        "ghp_",
        "gho_",
        "xoxb-",
        "xoxp-",
        "AIza",
        "AKIA",
        "Authorization: Bearer ",
    ] {
        text = redact_token_like_segments(&text, prefix);
    }
    text
}

fn redact_token_like_segments(input: &str, prefix: &str) -> String {
    let mut out = String::with_capacity(input.len());
    let mut cursor = 0;

    while let Some(found) = input[cursor..].find(prefix) {
        let start = cursor + found;
        out.push_str(&input[cursor..start]);
        let end = token_end_index(input, start + prefix.len());
        out.push_str(prefix);
        out.push_str(REDACTED);
        cursor = end;
    }

    out.push_str(&input[cursor..]);
    out
}

fn token_end_index(input: &str, start: usize) -> usize {
    for (offset, ch) in input[start..].char_indices() {
        if ch.is_whitespace() || ch == '"' || ch == '\'' || ch == ',' || ch == ';' {
            return start + offset;
        }
    }
    input.len()
}

fn truncate_text(input: &str, max_bytes: usize) -> String {
    if max_bytes == 0 {
        return String::new();
    }
    if input.len() <= max_bytes {
        return input.to_string();
    }

    let mut cut = max_bytes;
    while cut > 0 && !input.is_char_boundary(cut) {
        cut -= 1;
    }
    let mut out = input[..cut].to_string();
    out.push_str(TRUNCATED_SUFFIX);
    out
}
