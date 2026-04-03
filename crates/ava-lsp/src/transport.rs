use std::collections::HashMap;
use std::path::PathBuf;
use std::sync::atomic::Ordering;
use std::sync::Arc;
use std::time::Duration;

use serde_json::{json, Value};
use tokio::io::{AsyncBufReadExt, AsyncReadExt, AsyncWriteExt, BufReader};
use tokio::process::{ChildStderr, ChildStdin, ChildStdout};
use tokio::sync::{oneshot, Mutex};
use tracing::{debug, warn};
use url::Url;

use crate::parse::{file_uri, language_id_for_path, parse_diagnostics_array};
use crate::types::{LspDiagnostic, LspError, Result, ServerConnection};

impl ServerConnection {
    pub(crate) async fn request(&self, method: &str, params: Value) -> Result<Value> {
        let id = self.next_id.fetch_add(1, Ordering::SeqCst);
        let (tx, rx) = oneshot::channel();
        self.pending.lock().await.insert(id, tx);
        self.send_message(json!({
            "jsonrpc": "2.0",
            "id": id,
            "method": method,
            "params": params,
        }))
        .await?;
        match tokio::time::timeout(Duration::from_secs(5), rx).await {
            Ok(Ok(value)) => {
                if let Some(error) = value.get("error") {
                    Err(LspError::RequestFailed(error.to_string()))
                } else {
                    Ok(value.get("result").cloned().unwrap_or(Value::Null))
                }
            }
            Ok(Err(_)) => Err(LspError::RequestFailed(format!(
                "{method} response channel closed"
            ))),
            Err(_) => Err(LspError::RequestFailed(format!("{method} timed out"))),
        }
    }

    pub(crate) async fn notify(&self, method: &str, params: Value) -> std::io::Result<()> {
        self.send_message(json!({
            "jsonrpc": "2.0",
            "method": method,
            "params": params,
        }))
        .await
    }

    pub(crate) async fn open_or_update(&self, file_path: &std::path::Path) -> Result<()> {
        let text = tokio::fs::read_to_string(file_path).await?;
        let uri = file_uri(file_path)?;
        let mut open_files = self.open_files.lock().await;
        let version = open_files.get(file_path).copied().unwrap_or(0) + 1;
        if version == 1 {
            self.notify(
                "textDocument/didOpen",
                json!({
                    "textDocument": {
                        "uri": uri,
                        "languageId": language_id_for_path(file_path),
                        "version": 1,
                        "text": text,
                    }
                }),
            )
            .await?;
        } else {
            self.notify(
                "textDocument/didChange",
                json!({
                    "textDocument": {
                        "uri": uri,
                        "version": version,
                    },
                    "contentChanges": [{ "text": text }]
                }),
            )
            .await?;
        }
        open_files.insert(file_path.to_path_buf(), version);
        Ok(())
    }

    async fn send_message(&self, message: Value) -> std::io::Result<()> {
        let data = serde_json::to_vec(&message).map_err(std::io::Error::other)?;
        let mut stdin = self.stdin.lock().await;
        stdin
            .write_all(format!("Content-Length: {}\r\n\r\n", data.len()).as_bytes())
            .await?;
        stdin.write_all(&data).await?;
        stdin.flush().await
    }
}

pub(crate) async fn run_reader(
    stdout: ChildStdout,
    stdin: Arc<Mutex<ChildStdin>>,
    diagnostics: Arc<Mutex<HashMap<PathBuf, Vec<LspDiagnostic>>>>,
    pending: Arc<Mutex<HashMap<i64, oneshot::Sender<Value>>>>,
    diag_waiters: Arc<Mutex<HashMap<PathBuf, Vec<oneshot::Sender<()>>>>>,
) {
    let mut reader = BufReader::new(stdout);
    loop {
        let message = match read_message(&mut reader).await {
            Ok(Some(message)) => message,
            Ok(None) => break,
            Err(error) => {
                warn!(error = %error, "LSP reader failed");
                break;
            }
        };

        if let Some(id) = message.get("id").and_then(Value::as_i64) {
            if message.get("method").is_some() {
                let response = match message.get("method").and_then(Value::as_str) {
                    Some("workspace/configuration") => json!({
                        "jsonrpc": "2.0",
                        "id": id,
                        "result": []
                    }),
                    Some("client/registerCapability") => json!({
                        "jsonrpc": "2.0",
                        "id": id,
                        "result": Value::Null
                    }),
                    _ => json!({
                        "jsonrpc": "2.0",
                        "id": id,
                        "result": Value::Null
                    }),
                };
                let _ = send_raw_message(&stdin, response).await;
                continue;
            }
            if let Some(tx) = pending.lock().await.remove(&id) {
                let _ = tx.send(message);
            }
            continue;
        }

        if message.get("method").and_then(Value::as_str) == Some("textDocument/publishDiagnostics")
        {
            let uri = message
                .pointer("/params/uri")
                .and_then(Value::as_str)
                .and_then(|raw| Url::parse(raw).ok())
                .and_then(|url| url.to_file_path().ok());
            let Some(uri) = uri else {
                continue;
            };
            let parsed = parse_diagnostics_array(
                message
                    .pointer("/params/diagnostics")
                    .and_then(Value::as_array)
                    .cloned()
                    .unwrap_or_default(),
                &uri,
            );
            diagnostics.lock().await.insert(uri.clone(), parsed);
            if let Some(waiters) = diag_waiters.lock().await.remove(&uri) {
                for waiter in waiters {
                    let _ = waiter.send(());
                }
            }
        }
    }
}

async fn send_raw_message(stdin: &Arc<Mutex<ChildStdin>>, message: Value) -> std::io::Result<()> {
    let data = serde_json::to_vec(&message).map_err(std::io::Error::other)?;
    let mut stdin = stdin.lock().await;
    stdin
        .write_all(format!("Content-Length: {}\r\n\r\n", data.len()).as_bytes())
        .await?;
    stdin.write_all(&data).await?;
    stdin.flush().await
}

async fn read_message(reader: &mut BufReader<ChildStdout>) -> std::io::Result<Option<Value>> {
    let mut content_length = None;
    loop {
        let mut line = String::new();
        let read = reader.read_line(&mut line).await?;
        if read == 0 {
            return Ok(None);
        }
        let trimmed = line.trim();
        if trimmed.is_empty() {
            break;
        }
        if let Some((name, value)) = trimmed.split_once(':') {
            if name.eq_ignore_ascii_case("Content-Length") {
                content_length = value.trim().parse::<usize>().ok();
            }
        }
    }
    let Some(content_length) = content_length else {
        return Ok(None);
    };
    let mut buffer = vec![0; content_length];
    reader.read_exact(&mut buffer).await?;
    serde_json::from_slice(&buffer)
        .map(Some)
        .map_err(std::io::Error::other)
}

pub(crate) async fn drain_stderr(server_name: String, stderr: ChildStderr) {
    let mut reader = BufReader::new(stderr).lines();
    while let Ok(Some(line)) = reader.next_line().await {
        debug!(server = %server_name, stderr = %line, "LSP stderr");
    }
}
