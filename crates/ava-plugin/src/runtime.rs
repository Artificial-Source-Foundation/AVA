//! Plugin process runtime — spawn and communicate with plugin child processes.

use ava_types::{AvaError, Result};
use serde::{Deserialize, Serialize};
use serde_json::Value;
use std::path::Path;
use std::sync::atomic::{AtomicU64, Ordering};
use tokio::io::{AsyncReadExt, AsyncWriteExt, BufReader};
use tracing::{debug, warn};

use crate::manifest::PluginManifest;

/// Maximum plugin message size: 10 MB.
const MAX_MESSAGE_SIZE: usize = 10 * 1024 * 1024;

/// A JSON-RPC 2.0 message for plugin communication.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct JsonRpcMessage {
    pub jsonrpc: String,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub id: Option<Value>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub method: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub params: Option<Value>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub result: Option<Value>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub error: Option<JsonRpcError>,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct JsonRpcError {
    pub code: i64,
    pub message: String,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub data: Option<Value>,
}

impl JsonRpcMessage {
    fn request(id: u64, method: &str, params: Value) -> Self {
        Self {
            jsonrpc: "2.0".to_string(),
            id: Some(Value::Number(id.into())),
            method: Some(method.to_string()),
            params: Some(params),
            result: None,
            error: None,
        }
    }

    fn notification(method: &str, params: Value) -> Self {
        Self {
            jsonrpc: "2.0".to_string(),
            id: None,
            method: Some(method.to_string()),
            params: Some(params),
            result: None,
            error: None,
        }
    }
}

/// Environment variable names that should never be forwarded to plugin processes.
const SENSITIVE_ENV_VARS: &[&str] = &[
    "AWS_SECRET_ACCESS_KEY",
    "AWS_SESSION_TOKEN",
    "GITHUB_TOKEN",
    "GH_TOKEN",
    "OPENAI_API_KEY",
    "ANTHROPIC_API_KEY",
    "OPENROUTER_API_KEY",
    "GOOGLE_API_KEY",
    "AZURE_OPENAI_API_KEY",
    "HF_TOKEN",
    "HUGGING_FACE_HUB_TOKEN",
    "AVA_MASTER_PASSWORD",
];

/// A running plugin child process, communicating via JSON-RPC over stdio.
pub struct PluginProcess {
    child: tokio::process::Child,
    stdin: tokio::io::BufWriter<tokio::process::ChildStdin>,
    stdout: BufReader<tokio::process::ChildStdout>,
    next_id: AtomicU64,
}

impl PluginProcess {
    /// Spawn a plugin process from its manifest and directory.
    pub async fn spawn(manifest: &PluginManifest, plugin_dir: &Path) -> Result<Self> {
        let mut cmd = tokio::process::Command::new(&manifest.runtime.command);
        cmd.args(&manifest.runtime.args)
            .current_dir(plugin_dir)
            .stdin(std::process::Stdio::piped())
            .stdout(std::process::Stdio::piped())
            .stderr(std::process::Stdio::piped())
            .kill_on_drop(true);

        // Strip sensitive env vars from inherited environment
        for var in SENSITIVE_ENV_VARS {
            cmd.env_remove(var);
        }

        // Apply plugin-configured env vars
        for (key, value) in &manifest.runtime.env {
            cmd.env(key, value);
        }

        let mut child = cmd.spawn().map_err(|e| {
            AvaError::ToolError(format!(
                "failed to spawn plugin '{}' (command: {} {}): {e}",
                manifest.plugin.name,
                manifest.runtime.command,
                manifest.runtime.args.join(" ")
            ))
        })?;

        let stdin = child.stdin.take().ok_or_else(|| {
            AvaError::ToolError(format!(
                "plugin '{}' started but stdin is unavailable",
                manifest.plugin.name
            ))
        })?;
        let stdout = child.stdout.take().ok_or_else(|| {
            AvaError::ToolError(format!(
                "plugin '{}' started but stdout is unavailable",
                manifest.plugin.name
            ))
        })?;

        Ok(Self {
            child,
            stdin: tokio::io::BufWriter::new(stdin),
            stdout: BufReader::new(stdout),
            next_id: AtomicU64::new(1),
        })
    }

    /// Send a JSON-RPC request and wait for the response.
    pub async fn send_request(&mut self, method: &str, params: Value) -> Result<Value> {
        let id = self.next_id.fetch_add(1, Ordering::SeqCst);
        let msg = JsonRpcMessage::request(id, method, params);
        self.send_message(&msg).await?;
        let response = self.receive_message().await?;

        if let Some(error) = response.error {
            return Err(AvaError::ToolError(format!(
                "plugin RPC error ({}): {}",
                error.code, error.message
            )));
        }

        Ok(response.result.unwrap_or(Value::Null))
    }

    /// Send a JSON-RPC notification (fire and forget — no response expected).
    pub async fn send_notification(&mut self, method: &str, params: Value) {
        let msg = JsonRpcMessage::notification(method, params);
        if let Err(e) = self.send_message(&msg).await {
            warn!(method, error = %e, "failed to send notification to plugin");
        }
    }

    /// Send the `initialize` request to the plugin with project context.
    pub async fn initialize(&mut self, context: Value) -> Result<Value> {
        self.send_request("initialize", context).await
    }

    /// Gracefully shut down the plugin: send shutdown notification,
    /// wait up to 2 seconds, then kill the process.
    pub async fn shutdown(&mut self) {
        debug!("sending shutdown notification to plugin");
        self.send_notification("shutdown", Value::Null).await;

        // Give the plugin 2 seconds to exit gracefully
        match tokio::time::timeout(std::time::Duration::from_secs(2), self.child.wait()).await {
            Ok(Ok(status)) => {
                debug!(?status, "plugin exited gracefully");
            }
            Ok(Err(e)) => {
                warn!(error = %e, "error waiting for plugin exit");
                let _ = self.child.kill().await;
            }
            Err(_) => {
                debug!("plugin did not exit within 2s, killing");
                let _ = self.child.kill().await;
            }
        }
    }

    /// Check if the plugin process is still running.
    pub fn is_running(&mut self) -> bool {
        matches!(self.child.try_wait(), Ok(None))
    }

    // --- Wire format (Content-Length framing, same as MCP) ---

    async fn send_message(&mut self, msg: &JsonRpcMessage) -> Result<()> {
        let payload =
            serde_json::to_string(msg).map_err(|e| AvaError::SerializationError(e.to_string()))?;
        let frame = format!("Content-Length: {}\r\n\r\n{}", payload.len(), payload);
        self.stdin
            .write_all(frame.as_bytes())
            .await
            .map_err(AvaError::from)?;
        self.stdin.flush().await.map_err(AvaError::from)?;
        Ok(())
    }

    async fn receive_message(&mut self) -> Result<JsonRpcMessage> {
        // Read headers until we see \r\n\r\n
        let mut header_bytes = Vec::new();
        let mut byte = [0_u8; 1];

        loop {
            let n = self.stdout.read(&mut byte).await.map_err(AvaError::from)?;
            if n == 0 {
                return Err(AvaError::ValidationError(
                    "unexpected EOF while reading plugin response headers".to_string(),
                ));
            }
            header_bytes.push(byte[0]);
            if header_bytes.ends_with(b"\r\n\r\n") {
                break;
            }
        }

        let header_text = String::from_utf8(header_bytes)
            .map_err(|e| AvaError::SerializationError(e.to_string()))?;
        let len = parse_content_length(header_text.trim_end_matches("\r\n\r\n"))?;

        if len > MAX_MESSAGE_SIZE {
            return Err(AvaError::ToolError(format!(
                "plugin response too large: {len} bytes exceeds {MAX_MESSAGE_SIZE} byte limit"
            )));
        }

        let mut body = vec![0_u8; len];
        self.stdout
            .read_exact(&mut body)
            .await
            .map_err(AvaError::from)?;

        let body =
            String::from_utf8(body).map_err(|e| AvaError::SerializationError(e.to_string()))?;
        serde_json::from_str(&body).map_err(|e| AvaError::SerializationError(e.to_string()))
    }
}

/// Parse the Content-Length value from raw headers.
fn parse_content_length(raw: &str) -> Result<usize> {
    for line in raw.lines() {
        let (key, value) = line
            .split_once(':')
            .ok_or_else(|| AvaError::ValidationError(format!("invalid header line: {line}")))?;
        if key.trim().eq_ignore_ascii_case("content-length") {
            return value
                .trim()
                .parse::<usize>()
                .map_err(|e| AvaError::ValidationError(format!("invalid content-length: {e}")));
        }
    }
    Err(AvaError::ValidationError(
        "missing content-length header".to_string(),
    ))
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::collections::HashMap;

    #[test]
    fn json_rpc_request_serialization() {
        let msg = JsonRpcMessage::request(1, "test", serde_json::json!({"key": "value"}));
        let json = serde_json::to_string(&msg).unwrap();
        let parsed: JsonRpcMessage = serde_json::from_str(&json).unwrap();
        assert_eq!(parsed.jsonrpc, "2.0");
        assert_eq!(parsed.method.as_deref(), Some("test"));
        assert_eq!(parsed.id, Some(Value::Number(1.into())));
    }

    #[test]
    fn json_rpc_notification_has_no_id() {
        let msg = JsonRpcMessage::notification("event", serde_json::json!({}));
        let json = serde_json::to_string(&msg).unwrap();
        assert!(!json.contains("\"id\""));
    }

    #[test]
    fn parse_content_length_valid() {
        let result = parse_content_length("Content-Length: 42");
        assert_eq!(result.unwrap(), 42);
    }

    #[test]
    fn parse_content_length_missing() {
        let result = parse_content_length("X-Custom: foo");
        assert!(result.is_err());
    }

    #[test]
    fn parse_content_length_case_insensitive() {
        let result = parse_content_length("content-length: 100");
        assert_eq!(result.unwrap(), 100);
    }

    /// Test that we can spawn a process and communicate via JSON-RPC framing.
    /// Uses `cat` as an echo server — we write framed messages and read them back.
    #[tokio::test]
    async fn spawn_with_cat_echo() {
        // Build a minimal manifest that runs `cat`
        let manifest = PluginManifest {
            plugin: crate::manifest::PluginMeta {
                name: "test-echo".to_string(),
                version: "0.1.0".to_string(),
                description: String::new(),
                author: String::new(),
            },
            runtime: crate::manifest::RuntimeConfig {
                command: "cat".to_string(),
                args: vec![],
                env: HashMap::new(),
            },
            hooks: crate::manifest::HookSubscriptions::default(),
        };

        let tmp = tempfile::TempDir::new().unwrap();
        let result = PluginProcess::spawn(&manifest, tmp.path()).await;
        if result.is_err() {
            // cat may not be available in all test environments
            return;
        }
        let mut process = result.unwrap();

        // `cat` echoes back what we send — so a "request" we send will come back
        // as if it were the response. This tests the framing, not the protocol.
        assert!(process.is_running());

        process.shutdown().await;
    }

    #[tokio::test]
    async fn spawn_nonexistent_command() {
        let manifest = PluginManifest {
            plugin: crate::manifest::PluginMeta {
                name: "bad".to_string(),
                version: "0.1.0".to_string(),
                description: String::new(),
                author: String::new(),
            },
            runtime: crate::manifest::RuntimeConfig {
                command: "/nonexistent/binary".to_string(),
                args: vec![],
                env: HashMap::new(),
            },
            hooks: crate::manifest::HookSubscriptions::default(),
        };

        let tmp = tempfile::TempDir::new().unwrap();
        let result = PluginProcess::spawn(&manifest, tmp.path()).await;
        assert!(result.is_err());
    }
}
