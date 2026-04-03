use std::collections::HashMap;
use std::env;
use std::ffi::OsString;
use std::fs;
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
    file_uri, merge_json, normalize_path, parse_diagnostic_report, parse_document_symbols,
    parse_hover, parse_locations, parse_workspace_symbols, summarize_diagnostics,
};
use crate::transport::{drain_stderr, run_reader};
use crate::types::{
    DiagnosticSummary, LspDiagnostic, LspError, LspInstallResult, LspLocation, LspSnapshot,
    LspSuggestion, Result, RuntimeState, ServerConnection, ServerRuntime, ServerSnapshot,
    SymbolInfo,
};

pub struct LspManager {
    workspace_root: PathBuf,
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
        Self {
            workspace_root,
            config,
            servers,
        }
    }

    pub fn is_enabled(&self) -> bool {
        self.config.mode != LspMode::Off
    }

    pub async fn snapshot(&self) -> LspSnapshot {
        self.cleanup_idle().await;
        let mut servers = Vec::new();
        let mut active_server_count = 0;
        let mut summary = DiagnosticSummary::default();
        let mut suggestions = Vec::new();
        for runtime in &self.servers {
            let server_summary = runtime.summary().await;
            summary.errors += server_summary.diagnostics.errors;
            summary.warnings += server_summary.diagnostics.warnings;
            summary.info += server_summary.diagnostics.info;
            if server_summary.active {
                active_server_count += 1;
            }
            if let Some(suggestion) = self.suggestion_for(runtime, &server_summary).await {
                suggestions.push(suggestion);
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
            suggestions,
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

    pub async fn install_profile(&self, profile: &str) -> Result<LspInstallResult> {
        let Some(plan) = install_plan(profile) else {
            return Err(LspError::RequestFailed(format!(
                "unknown LSP install profile: {profile}"
            )));
        };

        let augmented_path = env::join_paths(command_search_paths()).map_err(|error| {
            LspError::RequestFailed(format!("failed to prepare PATH for install: {error}"))
        })?;

        let output = Command::new("sh")
            .args(["-lc", &plan.command])
            .current_dir(&self.workspace_root)
            .env("PATH", augmented_path)
            .output()
            .await
            .map_err(LspError::Io)?;

        let combined = [
            String::from_utf8_lossy(&output.stdout).trim().to_string(),
            String::from_utf8_lossy(&output.stderr).trim().to_string(),
        ]
        .into_iter()
        .filter(|part| !part.is_empty())
        .collect::<Vec<_>>()
        .join("\n");

        let verified = plan
            .verify_server
            .as_ref()
            .map(|server| resolve_command_path(server).is_some())
            .unwrap_or(output.status.success());

        Ok(LspInstallResult {
            profile: profile.to_string(),
            command: plan.command,
            success: output.status.success() && verified,
            message: if combined.is_empty() {
                if output.status.success() && verified {
                    format!("Installed {profile}")
                } else {
                    format!("Install command for {profile} completed but verification failed")
                }
            } else {
                combined
            },
        })
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
            relevant: project_uses_server(&self.workspace_root, &self.config.name),
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
        let cached = {
            let diagnostics = self.diagnostics.lock().await;
            diagnostics.get(&file_path).cloned().unwrap_or_default()
        };
        if !cached.is_empty() {
            return Ok(cached);
        }

        let pulled = connection
            .request(
                "textDocument/diagnostic",
                json!({
                    "textDocument": { "uri": file_uri(&file_path)? }
                }),
            )
            .await
            .map(|value| parse_diagnostic_report(&value, &file_path))
            .unwrap_or_default();

        if !pulled.is_empty() {
            self.diagnostics
                .lock()
                .await
                .insert(file_path.clone(), pulled.clone());
        }
        Ok(pulled)
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
        let symbols = parse_workspace_symbols(&result);
        if !symbols.is_empty() {
            return Ok(symbols);
        }
        self.workspace_symbol_fallback(query).await
    }

    async fn workspace_symbol_fallback(&self, query: &str) -> Result<Vec<SymbolInfo>> {
        let Some(connection) = self.connection.lock().await.as_ref().map(Arc::clone) else {
            return Ok(Vec::new());
        };
        let mut collected = Vec::new();
        for file_path in
            workspace_candidate_files(&self.workspace_root, &self.config.file_extensions, 64)
        {
            connection.open_or_update(&file_path).await?;
            let result = connection
                .request(
                    "textDocument/documentSymbol",
                    json!({ "textDocument": { "uri": file_uri(&file_path)? } }),
                )
                .await?;
            let matches = parse_document_symbols(&result)
                .into_iter()
                .filter(|symbol| symbol.name.to_lowercase().contains(&query.to_lowercase()));
            collected.extend(matches);
            if collected.len() >= 50 {
                break;
            }
        }
        Ok(collected)
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
        let command_path = resolve_server_command(&self.config)?;
        let mut command = Command::new(&command_path);
        let augmented_path =
            env::join_paths(command_search_paths()).map_err(|error| LspError::StartFailed {
                server: self.config.name.clone(),
                message: format!(
                    "failed to prepare PATH for {}: {error}",
                    self.config.command
                ),
            })?;
        command
            .args(&self.config.args)
            .current_dir(&self.workspace_root)
            .env("PATH", augmented_path)
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

impl LspManager {
    async fn suggestion_for(
        &self,
        runtime: &Arc<ServerRuntime>,
        snapshot: &ServerSnapshot,
    ) -> Option<LspSuggestion> {
        if !snapshot.relevant || !runtime.config.enabled {
            return None;
        }

        if let Err(error) = preflight_server_command(&runtime.config) {
            return Some(build_install_suggestion(
                &self.workspace_root,
                &runtime.config.name,
                &error.to_string(),
            ));
        }

        snapshot.last_error.as_ref().and_then(|error| {
            if error.contains("timed out") || error.contains("failed to start") {
                Some(build_install_suggestion(
                    &self.workspace_root,
                    &runtime.config.name,
                    error,
                ))
            } else {
                None
            }
        })
    }
}

fn build_install_suggestion(workspace_root: &Path, server: &str, error: &str) -> LspSuggestion {
    let frameworks = detect_frameworks(workspace_root, server);
    let profile = install_profile_for(server, &frameworks);
    let (title, install_command) = profile
        .as_deref()
        .and_then(install_plan)
        .map(|plan| (plan.title, Some(plan.command.clone())))
        .unwrap_or(("Install language tools", None));
    let framework_suffix = if frameworks.is_empty() {
        String::new()
    } else {
        format!(" for {}", frameworks.join(", "))
    };

    LspSuggestion {
        server: server.to_string(),
        title: title.to_string(),
        message: format!("{server} LSP{framework_suffix} is unavailable: {error}"),
        frameworks,
        install_profile: profile,
        install_command,
        key: format!("{server}:{error}"),
    }
}

struct InstallPlan {
    title: &'static str,
    command: String,
    verify_server: Option<&'static str>,
}

fn install_profile_for(server: &str, frameworks: &[String]) -> Option<String> {
    match server {
        "rust" => Some("rust".to_string()),
        "typescript" if frameworks.iter().any(|item| item == "astro") => {
            Some("typescript-astro".to_string())
        }
        "typescript" if frameworks.iter().any(|item| item == "svelte") => {
            Some("typescript-svelte".to_string())
        }
        "typescript"
            if frameworks
                .iter()
                .any(|item| item == "vue" || item == "nuxt") =>
        {
            Some("typescript-vue".to_string())
        }
        "typescript" => Some("typescript".to_string()),
        "python" => Some("python".to_string()),
        "go" => Some("go".to_string()),
        "java" => Some("java".to_string()),
        _ => None,
    }
}

fn install_plan(profile: &str) -> Option<InstallPlan> {
    let home = dirs::home_dir()?.display().to_string();
    Some(match profile {
        "rust" => InstallPlan {
            title: "Install Rust code intelligence",
            command: "rustup component add rust-analyzer".to_string(),
            verify_server: Some("rust-analyzer"),
        },
        "typescript" => InstallPlan {
            title: "Install TypeScript language tools",
            command: format!(
                "npm install --prefix \"{home}/.local\" typescript-language-server typescript"
            ),
            verify_server: Some("typescript-language-server"),
        },
        "typescript-vue" => InstallPlan {
            title: "Install Vue language tools",
            command: format!(
                "npm install --prefix \"{home}/.local\" typescript-language-server typescript @vue/language-server"
            ),
            verify_server: Some("typescript-language-server"),
        },
        "typescript-svelte" => InstallPlan {
            title: "Install Svelte language tools",
            command: format!(
                "npm install --prefix \"{home}/.local\" typescript-language-server typescript svelte-language-server svelte"
            ),
            verify_server: Some("typescript-language-server"),
        },
        "typescript-astro" => InstallPlan {
            title: "Install Astro language tools",
            command: format!(
                "npm install --prefix \"{home}/.local\" typescript-language-server typescript @astrojs/language-server astro"
            ),
            verify_server: Some("typescript-language-server"),
        },
        "python" => InstallPlan {
            title: "Install Python language tools",
            command: "python3 -m pip install --user python-lsp-server".to_string(),
            verify_server: Some("pylsp"),
        },
        "go" => InstallPlan {
            title: "Install Go language tools",
            command: format!(
                "set -e; if [ ! -x \"{home}/.local/go/bin/go\" ] && ! command -v go >/dev/null 2>&1; then case \"$(uname -s)-$(uname -m)\" in Linux-x86_64) GO_URL=https://go.dev/dl/go1.26.1.linux-amd64.tar.gz ;; Linux-aarch64|Linux-arm64) GO_URL=https://go.dev/dl/go1.26.1.linux-arm64.tar.gz ;; Darwin-x86_64) GO_URL=https://go.dev/dl/go1.26.1.darwin-amd64.tar.gz ;; Darwin-arm64) GO_URL=https://go.dev/dl/go1.26.1.darwin-arm64.tar.gz ;; *) echo 'Unsupported OS/arch for automatic Go install' >&2; exit 1 ;; esac; mkdir -p \"{home}/.local\"; curl -L \"$GO_URL\" -o \"{home}/.local/go-lsp.tar.gz\"; rm -rf \"{home}/.local/go\"; tar -C \"{home}/.local\" -xzf \"{home}/.local/go-lsp.tar.gz\"; fi; PATH=\"{home}/.local/go/bin:{home}/.local/bin:$PATH\" GOBIN=\"{home}/.local/bin\" \"${{GO_BIN:-go}}\" install golang.org/x/tools/gopls@latest"
            ),
            verify_server: Some("gopls"),
        },
        "java" => InstallPlan {
            title: "Install Java language tools",
            command: format!(
                "set -e; mkdir -p \"{home}/.local/bin\" \"{home}/.local/jdtls\"; curl -L \"https://www.eclipse.org/downloads/download.php?file=/jdtls/snapshots/jdt-language-server-latest.tar.gz&r=1\" -o \"{home}/.local/jdt-language-server-latest.tar.gz\"; rm -rf \"{home}/.local/jdtls\"; mkdir -p \"{home}/.local/jdtls\"; tar -C \"{home}/.local/jdtls\" -xzf \"{home}/.local/jdt-language-server-latest.tar.gz\"; cat > \"{home}/.local/bin/jdtls\" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail
JDTLS_HOME=\"{home}/.local/jdtls\"
LAUNCHER=$(ls \"$JDTLS_HOME\"/plugins/org.eclipse.equinox.launcher_*.jar | head -n 1)
case \"$(uname -s)\" in Darwin) CONFIG_DIR=\"$JDTLS_HOME/config_mac\" ;; *) CONFIG_DIR=\"$JDTLS_HOME/config_linux\" ;; esac
DATA_DIR=\"${{XDG_CACHE_HOME:-$HOME/.cache}}/ava-jdtls-workspace\"
mkdir -p \"$DATA_DIR\"
exec java -Declipse.application=org.eclipse.jdt.ls.core.id1 -Dosgi.bundles.defaultStartLevel=4 -Declipse.product=org.eclipse.jdt.ls.core.product -Dlog.level=ERROR -Xms256m -Xmx768m -jar \"$LAUNCHER\" -configuration \"$CONFIG_DIR\" -data \"$DATA_DIR\"
EOF
chmod +x \"{home}/.local/bin/jdtls\""
            ),
            verify_server: Some("jdtls"),
        },
        _ => return None,
    })
}

fn resolve_server_command(config: &ava_config::LspServerConfig) -> Result<PathBuf> {
    let Some(command_path) = resolve_command_path(&config.command) else {
        return Err(LspError::StartFailed {
            server: config.name.clone(),
            message: format!("{} is not installed or not on PATH", config.command),
        });
    };

    if config.command == "rust-analyzer"
        && fs::symlink_metadata(&command_path)
            .ok()
            .filter(|meta| meta.file_type().is_symlink())
            .and_then(|_| fs::read_link(&command_path).ok())
            .map(|target| {
                target
                    .file_name()
                    .map(|name| name == "rustup")
                    .unwrap_or(false)
            })
            .unwrap_or(false)
    {
        let output = std::process::Command::new("rustup")
            .args(["which", "rust-analyzer"])
            .output();
        match output {
            Ok(output) if output.status.success() => {}
            Ok(output) => {
                let stderr = String::from_utf8_lossy(&output.stderr).trim().to_string();
                return Err(LspError::StartFailed {
                    server: config.name.clone(),
                    message: if stderr.is_empty() {
                        "rust-analyzer is not installed for the active toolchain".to_string()
                    } else {
                        stderr
                    },
                });
            }
            Err(error) => {
                return Err(LspError::StartFailed {
                    server: config.name.clone(),
                    message: format!("failed to verify rust-analyzer via rustup: {error}"),
                });
            }
        }
    }

    Ok(command_path)
}

fn preflight_server_command(config: &ava_config::LspServerConfig) -> Result<()> {
    resolve_server_command(config).map(|_| ())
}

fn resolve_command_path(command: &str) -> Option<PathBuf> {
    let candidate = PathBuf::from(command);
    if candidate.components().count() > 1 {
        return candidate.exists().then_some(candidate);
    }

    command_search_paths()
        .into_iter()
        .flat_map(|dir| executable_candidates(&dir, command))
        .find(|path| path.exists())
}

fn command_search_paths() -> Vec<PathBuf> {
    let mut paths = Vec::new();
    if let Some(path) = env::var_os("PATH") {
        paths.extend(env::split_paths(&path));
    }
    if let Some(home) = dirs::home_dir() {
        paths.extend([
            home.join(".local/bin"),
            home.join(".local/go/bin"),
            home.join(".local/node_modules/.bin"),
            home.join(".cargo/bin"),
        ]);
    }
    paths.sort();
    paths.dedup();
    paths
}

fn executable_candidates(dir: &Path, command: &str) -> Vec<PathBuf> {
    let mut candidates = vec![dir.join(command)];
    if cfg!(windows) {
        let path_ext = env::var_os("PATHEXT").unwrap_or_else(|| OsString::from(".EXE;.BAT;.CMD"));
        for ext in path_ext.to_string_lossy().split(';') {
            if !ext.is_empty() {
                candidates.push(dir.join(format!("{command}{ext}")));
            }
        }
    }
    candidates
}

fn project_uses_server(workspace_root: &Path, server_name: &str) -> bool {
    match server_name {
        "rust" => workspace_root.join("Cargo.toml").exists(),
        "typescript" => {
            workspace_root.join("package.json").exists()
                || workspace_root.join("tsconfig.json").exists()
                || workspace_root.join("biome.json").exists()
                || workspace_root.join("eslint.config.js").exists()
                || workspace_root.join("eslint.config.mjs").exists()
        }
        "python" => workspace_root.join("pyproject.toml").exists(),
        "go" => workspace_root.join("go.mod").exists(),
        "java" => {
            workspace_root.join("pom.xml").exists()
                || workspace_root.join("build.gradle").exists()
                || workspace_root.join("build.gradle.kts").exists()
        }
        _ => false,
    }
}

fn detect_frameworks(workspace_root: &Path, server_name: &str) -> Vec<String> {
    let mut frameworks = Vec::new();
    match server_name {
        "rust" => {
            if workspace_root.join("src-tauri").exists() {
                frameworks.push("tauri".to_string());
            }
        }
        "typescript" => {
            let markers = [
                (
                    "astro",
                    ["astro.config.mjs", "astro.config.ts", "astro.config.js"].as_slice(),
                ),
                (
                    "svelte",
                    ["svelte.config.js", "svelte.config.ts"].as_slice(),
                ),
                (
                    "vue",
                    [
                        "vue.config.js",
                        "vue.config.ts",
                        "vite.config.ts",
                        "vite.config.js",
                    ]
                    .as_slice(),
                ),
                ("nuxt", ["nuxt.config.ts", "nuxt.config.js"].as_slice()),
                (
                    "nextjs",
                    ["next.config.js", "next.config.mjs", "next.config.ts"].as_slice(),
                ),
                ("solid", ["app.config.ts", "solid.config.ts"].as_slice()),
            ];
            for (name, files) in markers {
                if files.iter().any(|file| workspace_root.join(file).exists()) {
                    frameworks.push(name.to_string());
                }
            }
            if workspace_root.join("biome.json").exists()
                || workspace_root.join("biome.jsonc").exists()
            {
                frameworks.push("biome".to_string());
            }
            if [
                "eslint.config.js",
                "eslint.config.mjs",
                ".eslintrc",
                ".eslintrc.js",
            ]
            .iter()
            .any(|file| workspace_root.join(file).exists())
            {
                frameworks.push("eslint".to_string());
            }
        }
        "python" => {
            if workspace_root.join("manage.py").exists() {
                frameworks.push("django".to_string());
            }
            if file_contains(workspace_root.join("pyproject.toml"), "fastapi")
                || file_contains(workspace_root.join("requirements.txt"), "fastapi")
            {
                frameworks.push("fastapi".to_string());
            }
            if file_contains(workspace_root.join("pyproject.toml"), "flask")
                || file_contains(workspace_root.join("requirements.txt"), "flask")
            {
                frameworks.push("flask".to_string());
            }
        }
        "go" => {
            if file_contains(workspace_root.join("go.mod"), "gin-gonic/gin") {
                frameworks.push("gin".to_string());
            }
        }
        "java" => {
            if file_contains(workspace_root.join("pom.xml"), "spring-boot")
                || file_contains(workspace_root.join("build.gradle"), "spring-boot")
                || file_contains(workspace_root.join("build.gradle.kts"), "spring-boot")
            {
                frameworks.push("spring".to_string());
            }
        }
        _ => {}
    }
    frameworks.sort();
    frameworks.dedup();
    frameworks
}

fn file_contains(path: PathBuf, needle: &str) -> bool {
    fs::read_to_string(path)
        .map(|content| content.to_lowercase().contains(&needle.to_lowercase()))
        .unwrap_or(false)
}

fn workspace_candidate_files(root: &Path, extensions: &[String], limit: usize) -> Vec<PathBuf> {
    let mut files = Vec::new();
    collect_workspace_candidate_files(root, extensions, limit, &mut files);
    files
}

fn collect_workspace_candidate_files(
    root: &Path,
    extensions: &[String],
    limit: usize,
    out: &mut Vec<PathBuf>,
) {
    if out.len() >= limit {
        return;
    }
    let Ok(entries) = fs::read_dir(root) else {
        return;
    };
    for entry in entries.flatten() {
        if out.len() >= limit {
            break;
        }
        let path = entry.path();
        if path.is_dir() {
            let Some(name) = path.file_name().and_then(|name| name.to_str()) else {
                continue;
            };
            if matches!(name, ".git" | "node_modules" | "target" | ".ava" | "dist") {
                continue;
            }
            collect_workspace_candidate_files(&path, extensions, limit, out);
            continue;
        }
        let Some(extension) = path.extension().and_then(|ext| ext.to_str()) else {
            continue;
        };
        if extensions.iter().any(|candidate| candidate == extension) {
            out.push(path);
        }
    }
}
