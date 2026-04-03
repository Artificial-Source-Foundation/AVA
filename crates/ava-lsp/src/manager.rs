use std::collections::HashMap;
use std::path::{Path, PathBuf};
use std::process::Stdio;
use std::sync::atomic::AtomicI64;
use std::sync::Arc;
use std::time::{Duration, Instant};

use ava_config::{LspConfig, LspMode};
use serde_json::{json, Value};
use tokio::process::Command;
use tokio::sync::Mutex;

use crate::parse::{
    file_uri, merge_json, normalize_path, parse_document_symbols, parse_hover, parse_locations,
    parse_workspace_symbols, summarize_diagnostics,
};
use crate::transport::{drain_stderr, run_reader};
use crate::types::{
    DiagnosticSummary, LspDiagnostic, LspError, LspLocation, LspSnapshot, Result, RuntimeState,
    ServerConnection, ServerRuntime, ServerSnapshot, SymbolInfo,
};

pub struct LspManager {
    config: LspConfig,
    servers: Vec<Arc<ServerRuntime>>,
}

impl LspManager {
    pub fn new(workspace_root: PathBuf, config: LspConfig, enabled: bool) -> Self {
        let effective_mode = if enabled {
            config.mode.clone()
        } else {
            LspMode::Off
        };
        let mut config = config;
        config.mode = effective_mode;
        let servers = config
            .servers
            .iter()
            .cloned()
            .map(|server| {
                Arc::new(ServerRuntime {
                    workspace_root: workspace_root.clone(),
                    config: server,
                    state: tokio::sync::RwLock::new(RuntimeState::Idle),
                    connection: Mutex::new(None),
                    diagnostics: Arc::new(Mutex::new(HashMap::new())),
                    last_used: Mutex::new(Instant::now()),
                    last_error: Mutex::new(None),
                })
            })
            .collect();
        Self { config, servers }
    }

    pub fn is_enabled(&self) -> bool {
        self.config.mode != LspMode::Off
    }

    pub async fn snapshot(&self) -> LspSnapshot {
        self.cleanup_idle().await;
        let mut servers = Vec::new();
        let mut active_server_count = 0;
        let mut summary = DiagnosticSummary::default();
        for runtime in &self.servers {
            let server_summary = runtime.summary().await;
            summary.errors += server_summary.diagnostics.errors;
            summary.warnings += server_summary.diagnostics.warnings;
            summary.info += server_summary.diagnostics.info;
            if server_summary.active {
                active_server_count += 1;
            }
            servers.push(server_summary);
        }
        LspSnapshot {
            enabled: self.is_enabled(),
            mode: match self.config.mode {
                LspMode::Off => "off",
                LspMode::OnDemand => "on_demand",
                LspMode::AlwaysOn => "always_on",
            }
            .to_string(),
            active_server_count,
            summary,
            servers,
        }
    }

    pub async fn diagnostics(&self, file_path: &Path) -> Result<Vec<LspDiagnostic>> {
        let runtime = self.runtime_for_path(file_path).await?;
        runtime
            .collect_diagnostics(file_path, self.config.diagnostics_wait_ms)
            .await
    }

    pub async fn notify_file_changed(&self, file_path: &Path) -> Result<DiagnosticSummary> {
        let runtime = self.runtime_for_path(file_path).await?;
        runtime
            .collect_diagnostics(file_path, self.config.diagnostics_wait_ms)
            .await?;
        Ok(self.snapshot().await.summary)
    }

    pub async fn definition(
        &self,
        file_path: &Path,
        line: u32,
        character: u32,
    ) -> Result<Vec<LspLocation>> {
        let runtime = self.runtime_for_path(file_path).await?;
        runtime
            .location_request("textDocument/definition", file_path, line, character, None)
            .await
    }

    pub async fn references(
        &self,
        file_path: &Path,
        line: u32,
        character: u32,
        include_declaration: bool,
    ) -> Result<Vec<LspLocation>> {
        let runtime = self.runtime_for_path(file_path).await?;
        runtime
            .location_request(
                "textDocument/references",
                file_path,
                line,
                character,
                Some(json!({ "context": { "includeDeclaration": include_declaration } })),
            )
            .await
    }

    pub async fn hover(
        &self,
        file_path: &Path,
        line: u32,
        character: u32,
    ) -> Result<Option<String>> {
        let runtime = self.runtime_for_path(file_path).await?;
        runtime.hover(file_path, line, character).await
    }

    pub async fn document_symbols(&self, file_path: &Path) -> Result<Vec<SymbolInfo>> {
        let runtime = self.runtime_for_path(file_path).await?;
        runtime.document_symbols(file_path).await
    }

    pub async fn workspace_symbols(&self, query: &str) -> Result<Vec<SymbolInfo>> {
        self.cleanup_idle().await;
        if !self.is_enabled() {
            return Err(LspError::Disabled);
        }
        let mut collected = Vec::new();
        for runtime in &self.servers {
            if !runtime.config.enabled {
                continue;
            }
            if let Ok(symbols) = runtime.workspace_symbols(query).await {
                collected.extend(symbols);
            }
        }
        Ok(collected)
    }

    async fn runtime_for_path(&self, file_path: &Path) -> Result<Arc<ServerRuntime>> {
        self.cleanup_idle().await;
        if !self.is_enabled() {
            return Err(LspError::Disabled);
        }
        let extension = file_path
            .extension()
            .and_then(|ext| ext.to_str())
            .unwrap_or_default();
        for runtime in &self.servers {
            if runtime.config.enabled
                && runtime
                    .config
                    .file_extensions
                    .iter()
                    .any(|candidate| candidate == extension)
            {
                self.ensure_capacity(runtime).await;
                return Ok(Arc::clone(runtime));
            }
        }
        Err(LspError::Unsupported(file_path.display().to_string()))
    }

    async fn ensure_capacity(&self, requested: &Arc<ServerRuntime>) {
        let mut active = Vec::new();
        for runtime in &self.servers {
            if runtime.connection.lock().await.is_some() {
                let last_used = *runtime.last_used.lock().await;
                active.push((Arc::clone(runtime), last_used));
            }
        }
        if active.len() < self.config.max_active_servers {
            return;
        }
        active.sort_by_key(|(_, last_used)| *last_used);
        for (runtime, _) in active {
            if Arc::ptr_eq(&runtime, requested) {
                continue;
            }
            runtime.shutdown().await;
            break;
        }
    }

    async fn cleanup_idle(&self) {
        if self.config.mode != LspMode::OnDemand {
            return;
        }
        let idle_timeout = Duration::from_secs(self.config.idle_timeout_secs);
        for runtime in &self.servers {
            let last_used = *runtime.last_used.lock().await;
            if runtime.connection.lock().await.is_some() && last_used.elapsed() >= idle_timeout {
                runtime.shutdown().await;
            }
        }
    }
}

impl ServerRuntime {
    async fn summary(&self) -> ServerSnapshot {
        let diagnostics = self.diagnostics.lock().await;
        let diagnostics = summarize_diagnostics(diagnostics.values().flatten());
        let state = *self.state.read().await;
        let active = self.connection.lock().await.is_some();
        let last_error = self.last_error.lock().await.clone();
        ServerSnapshot {
            name: self.config.name.clone(),
            state,
            active,
            diagnostics,
            last_error,
        }
    }

    async fn collect_diagnostics(
        &self,
        file_path: &Path,
        wait_ms: u64,
    ) -> Result<Vec<LspDiagnostic>> {
        let file_path = normalize_path(&self.workspace_root, file_path);
        let connection = self.ensure_started().await?;
        self.touch_last_used().await;
        let waiter = {
            let (tx, rx) = tokio::sync::oneshot::channel();
            let mut waiters = connection.diag_waiters.lock().await;
            waiters.entry(file_path.clone()).or_default().push(tx);
            rx
        };
        connection.open_or_update(&file_path).await?;
        let _ = tokio::time::timeout(Duration::from_millis(wait_ms), waiter).await;
        let diagnostics = self.diagnostics.lock().await;
        Ok(diagnostics.get(&file_path).cloned().unwrap_or_default())
    }

    async fn location_request(
        &self,
        method: &str,
        file_path: &Path,
        line: u32,
        character: u32,
        extra: Option<Value>,
    ) -> Result<Vec<LspLocation>> {
        let file_path = normalize_path(&self.workspace_root, file_path);
        let connection = self.ensure_started().await?;
        self.touch_last_used().await;
        connection.open_or_update(&file_path).await?;
        let mut params = json!({
            "textDocument": { "uri": file_uri(&file_path)? },
            "position": { "line": line, "character": character }
        });
        if let Some(extra) = extra {
            merge_json(&mut params, extra);
        }
        let result = connection.request(method, params).await?;
        Ok(parse_locations(&result))
    }

    async fn hover(&self, file_path: &Path, line: u32, character: u32) -> Result<Option<String>> {
        let file_path = normalize_path(&self.workspace_root, file_path);
        let connection = self.ensure_started().await?;
        self.touch_last_used().await;
        connection.open_or_update(&file_path).await?;
        let result = connection
            .request(
                "textDocument/hover",
                json!({
                    "textDocument": { "uri": file_uri(&file_path)? },
                    "position": { "line": line, "character": character }
                }),
            )
            .await?;
        Ok(parse_hover(&result))
    }

    async fn document_symbols(&self, file_path: &Path) -> Result<Vec<SymbolInfo>> {
        let file_path = normalize_path(&self.workspace_root, file_path);
        let connection = self.ensure_started().await?;
        self.touch_last_used().await;
        connection.open_or_update(&file_path).await?;
        let result = connection
            .request(
                "textDocument/documentSymbol",
                json!({ "textDocument": { "uri": file_uri(&file_path)? } }),
            )
            .await?;
        Ok(parse_document_symbols(&result))
    }

    async fn workspace_symbols(&self, query: &str) -> Result<Vec<SymbolInfo>> {
        let connection = self.ensure_started().await?;
        self.touch_last_used().await;
        let result = connection
            .request("workspace/symbol", json!({ "query": query }))
            .await?;
        Ok(parse_workspace_symbols(&result))
    }

    async fn ensure_started(&self) -> Result<Arc<ServerConnection>> {
        {
            let guard = self.connection.lock().await;
            if guard.is_some() {
                return Ok(Arc::clone(guard.as_ref().expect("checked is_some")));
            }
        }
        *self.state.write().await = RuntimeState::Starting;
        match self.start_connection().await {
            Ok(connection) => {
                let mut guard = self.connection.lock().await;
                *guard = Some(Arc::new(connection));
                *self.state.write().await = RuntimeState::Ready;
                self.last_error.lock().await.take();
                Ok(Arc::clone(guard.as_ref().expect("just inserted")))
            }
            Err(error) => {
                let message = error.to_string();
                *self.last_error.lock().await = Some(message);
                *self.state.write().await = RuntimeState::Unavailable;
                Err(error)
            }
        }
    }

    async fn start_connection(&self) -> Result<ServerConnection> {
        let mut command = Command::new(&self.config.command);
        command
            .args(&self.config.args)
            .current_dir(&self.workspace_root)
            .stdin(Stdio::piped())
            .stdout(Stdio::piped())
            .stderr(Stdio::piped());
        let mut child = command.spawn().map_err(|error| LspError::StartFailed {
            server: self.config.name.clone(),
            message: error.to_string(),
        })?;
        let stdin = child.stdin.take().ok_or_else(|| LspError::StartFailed {
            server: self.config.name.clone(),
            message: "missing stdin".to_string(),
        })?;
        let stdout = child.stdout.take().ok_or_else(|| LspError::StartFailed {
            server: self.config.name.clone(),
            message: "missing stdout".to_string(),
        })?;
        let stderr = child.stderr.take().ok_or_else(|| LspError::StartFailed {
            server: self.config.name.clone(),
            message: "missing stderr".to_string(),
        })?;

        let pending = Arc::new(Mutex::new(HashMap::new()));
        let diag_waiters = Arc::new(Mutex::new(HashMap::new()));
        let open_files = Arc::new(Mutex::new(HashMap::new()));
        let stdin = Arc::new(Mutex::new(stdin));
        let diagnostics = Arc::clone(&self.diagnostics);
        let pending_for_reader = Arc::clone(&pending);
        let diag_waiters_for_reader = Arc::clone(&diag_waiters);
        let stdin_for_reader = Arc::clone(&stdin);
        let reader_task = tokio::spawn(async move {
            run_reader(
                stdout,
                stdin_for_reader,
                diagnostics,
                pending_for_reader,
                diag_waiters_for_reader,
            )
            .await;
        });
        let server_name = self.config.name.clone();
        let stderr_task = tokio::spawn(async move {
            drain_stderr(server_name, stderr).await;
        });

        let connection = ServerConnection {
            child: Mutex::new(child),
            stdin,
            next_id: AtomicI64::new(1),
            pending,
            diag_waiters,
            open_files,
            _reader_task: reader_task,
            _stderr_task: stderr_task,
        };
        connection
            .request(
                "initialize",
                json!({
                    "processId": std::process::id(),
                    "rootUri": file_uri(&self.workspace_root)?,
                    "workspaceFolders": [{
                        "uri": file_uri(&self.workspace_root)?,
                        "name": self.workspace_root.file_name().and_then(|name| name.to_str()).unwrap_or("workspace")
                    }],
                    "capabilities": {
                        "workspace": { "configuration": true },
                        "textDocument": {
                            "publishDiagnostics": { "relatedInformation": true }
                        }
                    }
                }),
            )
            .await?;
        connection
            .notify("initialized", json!({}))
            .await
            .map_err(LspError::Io)?;
        Ok(connection)
    }

    async fn shutdown(&self) {
        let mut guard = self.connection.lock().await;
        let Some(connection) = guard.take() else {
            return;
        };
        let _ = connection.request("shutdown", json!(null)).await;
        let _ = connection.notify("exit", json!(null)).await;
        let _ = connection.child.lock().await.kill().await;
        *self.state.write().await = RuntimeState::Idle;
    }

    async fn touch_last_used(&self) {
        *self.last_used.lock().await = Instant::now();
    }
}
