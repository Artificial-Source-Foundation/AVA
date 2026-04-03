use std::collections::HashMap;
use std::path::PathBuf;
use std::sync::atomic::AtomicI64;
use std::sync::Arc;
use std::time::Instant;

use ava_config::LspServerConfig;
use serde::Serialize;
use serde_json::Value;
use thiserror::Error;
use tokio::process::{Child, ChildStdin};
use tokio::sync::{oneshot, Mutex, RwLock};

#[derive(Debug, Error)]
pub enum LspError {
    #[error("LSP is disabled")]
    Disabled,
    #[error("No configured LSP server supports {0}")]
    Unsupported(String),
    #[error("failed to start {server}: {message}")]
    StartFailed { server: String, message: String },
    #[error("LSP request failed: {0}")]
    RequestFailed(String),
    #[error("I/O error: {0}")]
    Io(#[from] std::io::Error),
    #[error("JSON error: {0}")]
    Json(#[from] serde_json::Error),
}

pub type Result<T> = std::result::Result<T, LspError>;

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize)]
#[serde(rename_all = "snake_case")]
pub enum RuntimeState {
    Disabled,
    Idle,
    Starting,
    Ready,
    Error,
    Unavailable,
}

#[derive(Debug, Clone, Serialize, Default)]
pub struct DiagnosticSummary {
    pub errors: usize,
    pub warnings: usize,
    pub info: usize,
}

#[derive(Debug, Clone, Serialize)]
pub struct LspDiagnostic {
    pub file: String,
    pub line: u32,
    pub column: u32,
    pub severity: String,
    pub message: String,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub source: Option<String>,
}

#[derive(Debug, Clone, Serialize)]
pub struct LspLocation {
    pub file: String,
    pub line: u32,
    pub column: u32,
    pub end_line: u32,
    pub end_column: u32,
}

#[derive(Debug, Clone, Serialize)]
pub struct SymbolInfo {
    pub name: String,
    pub kind: String,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub detail: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub location: Option<LspLocation>,
}

#[derive(Debug, Clone, Serialize)]
pub struct ServerSnapshot {
    pub name: String,
    pub state: RuntimeState,
    pub active: bool,
    pub diagnostics: DiagnosticSummary,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub last_error: Option<String>,
}

#[derive(Debug, Clone, Serialize)]
pub struct LspSnapshot {
    pub enabled: bool,
    pub mode: String,
    pub active_server_count: usize,
    pub summary: DiagnosticSummary,
    pub servers: Vec<ServerSnapshot>,
}

pub(crate) struct ServerConnection {
    pub(crate) child: Mutex<Child>,
    pub(crate) stdin: Arc<Mutex<ChildStdin>>,
    pub(crate) next_id: AtomicI64,
    pub(crate) pending: Arc<Mutex<HashMap<i64, oneshot::Sender<Value>>>>,
    pub(crate) diag_waiters: Arc<Mutex<HashMap<PathBuf, Vec<oneshot::Sender<()>>>>>,
    pub(crate) open_files: Arc<Mutex<HashMap<PathBuf, i32>>>,
    pub(crate) _reader_task: tokio::task::JoinHandle<()>,
    pub(crate) _stderr_task: tokio::task::JoinHandle<()>,
}

pub(crate) struct ServerRuntime {
    pub(crate) workspace_root: PathBuf,
    pub(crate) config: LspServerConfig,
    pub(crate) state: RwLock<RuntimeState>,
    pub(crate) connection: Mutex<Option<Arc<ServerConnection>>>,
    pub(crate) diagnostics: Arc<Mutex<HashMap<PathBuf, Vec<LspDiagnostic>>>>,
    pub(crate) last_used: Mutex<Instant>,
    pub(crate) last_error: Mutex<Option<String>>,
}
