use std::collections::HashMap;
use std::path::PathBuf;
use std::sync::Arc;
use std::time::Duration;

use ava_types::{AvaError, Result, ToolResult};
use base64::Engine;
use futures::future::join_all;
use serde_json::Value;
use tokio::sync::Mutex;
use tracing::{info, warn};

/// Default timeout for connecting to a single MCP server (including stdio spawn + initialize).
const MCP_CONNECT_TIMEOUT_SECS: u64 = 30;
/// Default timeout for an already-connected MCP server request.
const MCP_REQUEST_TIMEOUT_SECS: u64 = 45;

/// Maximum character count for MCP tool output before truncation (F41).
pub const MAX_MCP_OUTPUT_CHARS: usize = 100_000;

/// Threshold in seconds for MCP progress logging (F51).
const MCP_PROGRESS_LOG_INTERVAL_SECS: u64 = 30;

use crate::client::{
    MCPClient, MCPPrompt, MCPPromptResult, MCPResource, MCPResourceContent, MCPTool,
};
use crate::config::{MCPServerConfig, TransportType};
use crate::oauth::McpOAuthManager;
use crate::transport::{HttpTransport, HttpTransportConfig, StdioTransport};

// ---------------------------------------------------------------------------
// ExtensionManager — connects to MCP servers and aggregates their tools
// ---------------------------------------------------------------------------

/// F13 — Debounce window for batching `list_changed` notifications.
const LIST_CHANGED_DEBOUNCE_MS: u64 = 500;

pub struct ExtensionManager {
    /// MCP clients keyed by server name, each behind a Mutex for interior mutability.
    clients: HashMap<String, Arc<Mutex<MCPClient>>>,
    /// All discovered tools, each tagged with the server name that owns it.
    tools: Vec<(String, MCPTool)>,
    /// F13 — Servers that have pending `list_changed` notifications awaiting batch refresh.
    pending_refresh: std::collections::HashSet<String>,
    /// F13 — When the debounce window started (first pending notification).
    debounce_start: Option<std::time::Instant>,
}

impl ExtensionManager {
    /// Create a new extension manager (does not connect yet).
    pub fn new() -> Self {
        Self {
            clients: HashMap::new(),
            tools: Vec::new(),
            pending_refresh: std::collections::HashSet::new(),
            debounce_start: None,
        }
    }

    /// Connect to all enabled servers and discover their tools.
    ///
    /// All servers connect in parallel — each with an independent 30 s timeout.
    /// One slow or failing server does NOT delay the others.
    pub async fn initialize(&mut self, configs: Vec<MCPServerConfig>) -> Result<()> {
        let enabled: Vec<MCPServerConfig> = configs
            .into_iter()
            .filter(|c| {
                if !c.enabled {
                    info!(server = %c.name, "MCP server disabled, skipping");
                    false
                } else {
                    true
                }
            })
            .collect();

        if enabled.is_empty() {
            return Ok(());
        }

        // Spawn all connections in parallel.
        let timeout = Duration::from_secs(MCP_CONNECT_TIMEOUT_SECS);
        let futures: Vec<_> = enabled
            .iter()
            .map(|cfg| {
                let cfg = cfg.clone();
                async move {
                    let result =
                        tokio::time::timeout(timeout, connect_server_standalone(&cfg)).await;
                    (cfg.name.clone(), result)
                }
            })
            .collect();

        let results = join_all(futures).await;

        for (server_name, result) in results {
            match result {
                Ok(Ok((client, mcp_tools))) => {
                    info!(server = %server_name, tool_count = mcp_tools.len(), "MCP server connected");
                    for tool in &mcp_tools {
                        self.tools.push((server_name.clone(), tool.clone()));
                    }
                    self.clients
                        .insert(server_name, Arc::new(Mutex::new(client)));
                }
                Ok(Err(e)) => {
                    warn!(
                        server = %server_name,
                        error = %e,
                        "Failed to connect MCP server, skipping"
                    );
                }
                Err(_elapsed) => {
                    warn!(
                        server = %server_name,
                        timeout_secs = MCP_CONNECT_TIMEOUT_SECS,
                        "MCP server connection timed out, skipping"
                    );
                }
            }
        }

        Ok(())
    }

    /// Get all discovered tools as `ava_types::Tool` definitions.
    pub fn list_tools(&self) -> Vec<ava_types::Tool> {
        self.tools
            .iter()
            .map(|(_, tool)| tool.to_ava_tool())
            .collect()
    }

    /// Get all discovered tools with their server names.
    pub fn list_tools_with_server(&self) -> &[(String, MCPTool)] {
        &self.tools
    }

    /// Find which server owns a tool by name.
    pub fn server_for_tool(&self, tool_name: &str) -> Option<&str> {
        self.tools
            .iter()
            .find(|(_, t)| t.name == tool_name)
            .map(|(server, _)| server.as_str())
    }

    /// Execute a tool call, routing to the correct server.
    ///
    /// Includes progress logging for long-running calls (F51), output
    /// truncation for oversized results (F41), and binary blob detection (F43).
    /// This method takes `&self` and uses interior mutability via client Mutexes.
    pub async fn call_tool(&self, name: &str, arguments: Value) -> Result<ToolResult> {
        let server_name = self
            .tools
            .iter()
            .find(|(_, t)| t.name == name)
            .map(|(s, _)| s.clone())
            .ok_or_else(|| AvaError::ToolNotFound {
                tool: name.to_string(),
                available: self
                    .tools
                    .iter()
                    .map(|(_, t)| t.name.as_str())
                    .collect::<Vec<_>>()
                    .join(", "),
            })?;

        let client = self.clients.get(&server_name).ok_or_else(|| {
            AvaError::ToolError(format!("MCP server '{server_name}' is not connected"))
        })?;

        // F51: Progress logging for long-running tool calls
        let result = call_tool_with_progress(
            client,
            &server_name,
            name,
            arguments,
            Duration::from_secs(MCP_REQUEST_TIMEOUT_SECS),
        )
        .await?;

        // F43: Check for binary blobs in content array
        if let Some(blob_result) = detect_and_save_binary_blob(&result, &server_name, name) {
            return Ok(ToolResult {
                call_id: format!("mcp-{server_name}-{name}"),
                content: blob_result,
                is_error: false,
            });
        }

        // Parse the MCP result into a ToolResult
        let content = extract_text_content(&result);
        let is_error = result
            .get("isError")
            .and_then(Value::as_bool)
            .unwrap_or(false);

        // F41: Truncate oversized output
        let content = truncate_mcp_output(&content, &server_name, name);

        Ok(ToolResult {
            call_id: format!("mcp-{server_name}-{name}"),
            content,
            is_error,
        })
    }

    async fn call_tool_with_timeout(
        client: &Arc<Mutex<MCPClient>>,
        server_name: &str,
        tool_name: &str,
        arguments: Value,
        timeout_duration: Duration,
    ) -> Result<Value> {
        let mut client = client.lock().await;
        tokio::time::timeout(timeout_duration, client.call_tool(tool_name, arguments))
            .await
            .map_err(|_| {
                AvaError::ToolError(format!(
                    "MCP server '{server_name}' timed out after {} while calling tool '{tool_name}'",
                    format_timeout(timeout_duration)
                ))
            })?
    }

    /// F13 — Queue a server for batched tool refresh.
    ///
    /// Instead of immediately re-fetching tools on every `list_changed` notification,
    /// this queues the server name and starts a debounce window. Call
    /// `flush_pending_refresh()` after the debounce period to batch-refresh all
    /// queued servers in one pass.
    pub fn queue_refresh(&mut self, server_name: &str) {
        tracing::debug!(server = server_name, "F13: queued MCP server for refresh");
        if self.pending_refresh.is_empty() {
            self.debounce_start = Some(std::time::Instant::now());
        }
        self.pending_refresh.insert(server_name.to_string());
    }

    /// F13 — Check if the debounce window has elapsed and pending refreshes should be flushed.
    pub fn should_flush_refresh(&self) -> bool {
        if self.pending_refresh.is_empty() {
            return false;
        }
        self.debounce_start
            .map(|start| start.elapsed().as_millis() >= LIST_CHANGED_DEBOUNCE_MS as u128)
            .unwrap_or(false)
    }

    /// F13 — Batch-refresh all queued servers and return results.
    ///
    /// Clears the pending set and debounce timer. Returns a vec of
    /// `(server_name, tool_count)` for each successfully refreshed server.
    pub async fn flush_pending_refresh(&mut self) -> Vec<(String, usize)> {
        let servers: Vec<String> = self.pending_refresh.drain().collect();
        self.debounce_start = None;

        tracing::info!(
            count = servers.len(),
            "F13: flushing batched MCP server refreshes"
        );

        let mut results = Vec::new();
        for server_name in servers {
            match self.reload_server_tools(&server_name).await {
                Ok(tools) => {
                    info!(
                        server = %server_name,
                        tool_count = tools.len(),
                        "F13: batch-refreshed MCP server tools"
                    );
                    results.push((server_name, tools.len()));
                }
                Err(e) => {
                    warn!(
                        server = %server_name,
                        error = %e,
                        "F13: failed to refresh MCP server tools"
                    );
                }
            }
        }
        results
    }

    /// Re-fetch the tool list from a single server and update the registry.
    ///
    /// Called when the server sends a `notifications/tools/list_changed` notification.
    /// Returns the updated list of tools for that server, or an error if the server
    /// is not connected.
    pub async fn reload_server_tools(&mut self, server_name: &str) -> Result<Vec<MCPTool>> {
        let client = self.clients.get(server_name).ok_or_else(|| {
            AvaError::ToolError(format!(
                "MCP server '{server_name}' is not connected — cannot reload tools"
            ))
        })?;

        let mut client = client.lock().await;
        let new_tools = tokio::time::timeout(
            Duration::from_secs(MCP_REQUEST_TIMEOUT_SECS),
            client.list_tools(),
        )
        .await
        .map_err(|_| {
            AvaError::ToolError(format!(
                "MCP server '{server_name}' timed out after {MCP_REQUEST_TIMEOUT_SECS}s while reloading tools"
            ))
        })??;

        // Remove all existing tools registered for this server.
        self.tools.retain(|(srv, _)| srv != server_name);

        // Insert the freshly fetched tools.
        for tool in &new_tools {
            self.tools.push((server_name.to_string(), tool.clone()));
        }

        info!(
            server = %server_name,
            tool_count = new_tools.len(),
            "MCP tools reloaded after list_changed notification"
        );

        Ok(new_tools)
    }

    /// List all resources available on a specific MCP server.
    ///
    /// Returns an error if the server is not connected or does not support resources.
    pub async fn list_resources(&self, server_name: &str) -> Result<Vec<MCPResource>> {
        let client = self.clients.get(server_name).ok_or_else(|| {
            AvaError::ToolError(format!("MCP server '{server_name}' is not connected"))
        })?;
        let mut client = client.lock().await;
        tokio::time::timeout(
            Duration::from_secs(MCP_REQUEST_TIMEOUT_SECS),
            client.list_resources(),
        )
        .await
        .map_err(|_| {
            AvaError::ToolError(format!(
                "MCP server '{server_name}' timed out after {MCP_REQUEST_TIMEOUT_SECS}s while listing resources"
            ))
        })?
    }

    /// Read the content of a resource by URI from a specific MCP server.
    pub async fn read_resource(
        &self,
        server_name: &str,
        uri: &str,
    ) -> Result<Vec<MCPResourceContent>> {
        let client = self.clients.get(server_name).ok_or_else(|| {
            AvaError::ToolError(format!("MCP server '{server_name}' is not connected"))
        })?;
        let mut client = client.lock().await;
        tokio::time::timeout(
            Duration::from_secs(MCP_REQUEST_TIMEOUT_SECS),
            client.read_resource(uri),
        )
        .await
        .map_err(|_| {
            AvaError::ToolError(format!(
                "MCP server '{server_name}' timed out after {MCP_REQUEST_TIMEOUT_SECS}s while reading resource '{uri}'"
            ))
        })?
    }

    /// List all prompt templates available on a specific MCP server.
    pub async fn list_prompts(&self, server_name: &str) -> Result<Vec<MCPPrompt>> {
        let client = self.clients.get(server_name).ok_or_else(|| {
            AvaError::ToolError(format!("MCP server '{server_name}' is not connected"))
        })?;
        let mut client = client.lock().await;
        tokio::time::timeout(
            Duration::from_secs(MCP_REQUEST_TIMEOUT_SECS),
            client.list_prompts(),
        )
        .await
        .map_err(|_| {
            AvaError::ToolError(format!(
                "MCP server '{server_name}' timed out after {MCP_REQUEST_TIMEOUT_SECS}s while listing prompts"
            ))
        })?
    }

    /// Retrieve and render a prompt template from a specific MCP server.
    ///
    /// `arguments` should be a JSON object mapping argument names to string values.
    pub async fn get_prompt(
        &self,
        server_name: &str,
        prompt_name: &str,
        arguments: serde_json::Value,
    ) -> Result<MCPPromptResult> {
        let client = self.clients.get(server_name).ok_or_else(|| {
            AvaError::ToolError(format!("MCP server '{server_name}' is not connected"))
        })?;
        let mut client = client.lock().await;
        tokio::time::timeout(
            Duration::from_secs(MCP_REQUEST_TIMEOUT_SECS),
            client.get_prompt(prompt_name, arguments),
        )
        .await
        .map_err(|_| {
            AvaError::ToolError(format!(
                "MCP server '{server_name}' timed out after {MCP_REQUEST_TIMEOUT_SECS}s while rendering prompt '{prompt_name}'"
            ))
        })?
    }

    /// Disconnect all servers.
    pub async fn shutdown(&mut self) -> Result<()> {
        for (name, client) in self.clients.drain() {
            let mut client = client.lock().await;
            if let Err(e) = client.disconnect().await {
                warn!(server = %name, error = %e, "Error disconnecting MCP server");
            }
        }
        self.tools.clear();
        Ok(())
    }

    /// Number of connected servers.
    pub fn server_count(&self) -> usize {
        self.clients.len()
    }

    /// Number of discovered tools.
    pub fn tool_count(&self) -> usize {
        self.tools.len()
    }

    /// Get the names of all connected servers.
    pub fn connected_server_names(&self) -> Vec<String> {
        self.clients.keys().cloned().collect()
    }
}

fn format_timeout(duration: Duration) -> String {
    if duration.as_millis() < 1000 {
        format!("{}ms", duration.as_millis())
    } else {
        format!("{}s", duration.as_secs())
    }
}

impl Default for ExtensionManager {
    fn default() -> Self {
        Self::new()
    }
}

// ---------------------------------------------------------------------------
// Free-standing connect helper (used by parallel init)
// ---------------------------------------------------------------------------

/// Connect to a single MCP server and return the client + discovered tools.
///
/// This is a free function (not `&mut self`) so it can run in parallel for
/// multiple servers without requiring exclusive access to `ExtensionManager`.
async fn connect_server_standalone(config: &MCPServerConfig) -> Result<(MCPClient, Vec<MCPTool>)> {
    let transport: Box<dyn crate::transport::MCPTransport> = match &config.transport {
        TransportType::Stdio { command, args, env } => {
            Box::new(StdioTransport::spawn(command, args, env).await?)
        }
        TransportType::Http {
            url,
            auth,
            bearer_token,
            headers,
        } => {
            let http_config = HttpTransportConfig {
                bearer_token: bearer_token.clone(),
                headers: headers.clone(),
            };
            let mut transport = HttpTransport::with_config(url, http_config);

            if let Some(oauth_cfg) = auth {
                let mut oauth_mgr = McpOAuthManager::new(&config.name, oauth_cfg.clone());
                let token = oauth_mgr.get_access_token().await?;
                transport.set_bearer_token(token);
            }

            if let Err(e) = transport.connect_sse().await {
                tracing::debug!(
                    server = %config.name,
                    error = %e,
                    "MCP SSE connect failed (may not be an SSE server, will use POST-only)"
                );
            }

            Box::new(transport)
        }
    };

    let mut client = MCPClient::new(transport, &config.name);
    client.initialize().await?;
    let mcp_tools = client.list_tools().await?;

    info!(
        server = %config.name,
        tool_count = mcp_tools.len(),
        "Discovered MCP tools"
    );

    Ok((client, mcp_tools))
}

/// Extract text content from an MCP tools/call result.
///
/// MCP spec says result.content is an array of content blocks.
/// Each block has a "type" and "text" (for text blocks).
fn extract_text_content(result: &Value) -> String {
    if let Some(content_array) = result.get("content").and_then(Value::as_array) {
        content_array
            .iter()
            .filter_map(|block| {
                if block.get("type").and_then(Value::as_str) == Some("text") {
                    block.get("text").and_then(Value::as_str).map(String::from)
                } else {
                    // For non-text blocks, serialize them
                    Some(serde_json::to_string(block).unwrap_or_default())
                }
            })
            .collect::<Vec<_>>()
            .join("\n")
    } else if let Some(text) = result.get("content").and_then(Value::as_str) {
        // Some servers return content as a plain string
        text.to_string()
    } else {
        serde_json::to_string(result).unwrap_or_default()
    }
}

// ---------------------------------------------------------------------------
// F41: MCP Result Token Validation — truncate oversized output
// ---------------------------------------------------------------------------

/// Truncate MCP output if it exceeds `MAX_MCP_OUTPUT_CHARS`.
///
/// When truncated, the full output is saved to `~/.ava/mcp-output/` and a
/// notice is appended to the truncated content.
fn truncate_mcp_output(content: &str, server_name: &str, tool_name: &str) -> String {
    if content.len() <= MAX_MCP_OUTPUT_CHARS {
        return content.to_string();
    }

    let original_len = content.len();

    // Save full output to fallback file
    if let Some(path) = save_mcp_output_fallback(content, server_name, tool_name) {
        info!(
            server = %server_name,
            tool = %tool_name,
            original_len,
            saved_to = %path.display(),
            "MCP output truncated, full output saved"
        );
    }

    let mut truncated = content[..MAX_MCP_OUTPUT_CHARS].to_string();
    truncated.push_str(&format!(
        "\n[MCP output truncated — {original_len} chars exceeded {MAX_MCP_OUTPUT_CHARS} limit]"
    ));
    truncated
}

/// Save full MCP output to `~/.ava/mcp-output/{server}-{tool}-{timestamp}.txt`.
fn save_mcp_output_fallback(content: &str, server_name: &str, tool_name: &str) -> Option<PathBuf> {
    let dir = mcp_output_dir()?;
    std::fs::create_dir_all(&dir).ok()?;

    let timestamp = std::time::SystemTime::now()
        .duration_since(std::time::UNIX_EPOCH)
        .unwrap_or_default()
        .as_millis();

    let filename = format!("{server_name}-{tool_name}-{timestamp}.txt");
    let path = dir.join(filename);
    std::fs::write(&path, content).ok()?;
    Some(path)
}

/// MCP output directory path.
fn mcp_output_dir() -> Option<PathBuf> {
    dirs::home_dir().map(|h| h.join(".ava").join("mcp-output"))
}

// ---------------------------------------------------------------------------
// F43: Binary Blob Handling
// ---------------------------------------------------------------------------

/// Detect base64-encoded binary content in MCP tool results and save decoded bytes.
///
/// Checks the content array for base64-encoded blobs (either via `mimeType` hints
/// or by detecting the base64 pattern in text content over 1000 chars).
/// Returns `Some(message)` if a blob was detected and saved, `None` otherwise.
fn detect_and_save_binary_blob(
    result: &Value,
    server_name: &str,
    tool_name: &str,
) -> Option<String> {
    let content_array = result.get("content").and_then(Value::as_array)?;

    for block in content_array {
        // Check for explicit blob field (MCP spec: binary content)
        if let Some(blob_b64) = block.get("blob").and_then(Value::as_str) {
            let mime_type = block.get("mimeType").and_then(Value::as_str).unwrap_or("");
            return save_binary_blob(blob_b64, mime_type, server_name, tool_name);
        }

        // Check for base64 text content with binary MIME type
        let mime_type = block.get("mimeType").and_then(Value::as_str).unwrap_or("");
        let is_binary_mime = mime_type.starts_with("image/")
            || mime_type.starts_with("application/octet")
            || mime_type == "application/pdf";

        if let Some(text) = block.get("text").and_then(Value::as_str) {
            if (is_binary_mime || is_likely_base64(text)) && text.len() > 1000 {
                return save_binary_blob(text, mime_type, server_name, tool_name);
            }
        }
    }

    None
}

/// Check if a string looks like base64-encoded binary data.
fn is_likely_base64(s: &str) -> bool {
    if s.len() < 1000 {
        return false;
    }
    // Base64 strings consist of [A-Za-z0-9+/=] with optional whitespace
    let trimmed = s.trim();
    let valid_chars = trimmed
        .chars()
        .filter(|c| !c.is_whitespace())
        .all(|c| c.is_ascii_alphanumeric() || c == '+' || c == '/' || c == '=');
    valid_chars && trimmed.len() > 1000
}

/// Infer file extension from decoded binary magic bytes.
fn infer_extension(bytes: &[u8], mime_type: &str) -> &'static str {
    // Check magic bytes first
    if bytes.len() >= 4 {
        if bytes[0..4] == [0x89, 0x50, 0x4E, 0x47] {
            return "png";
        }
        if bytes[0..2] == [0xFF, 0xD8] {
            return "jpg";
        }
        if bytes.starts_with(b"%PDF") {
            return "pdf";
        }
        if bytes.starts_with(b"GIF8") {
            return "gif";
        }
        if bytes[0..4] == [0x50, 0x4B, 0x03, 0x04] {
            return "zip";
        }
    }

    // Fall back to MIME type
    match mime_type {
        "image/png" => "png",
        "image/jpeg" | "image/jpg" => "jpg",
        "image/gif" => "gif",
        "application/pdf" => "pdf",
        "application/zip" => "zip",
        _ => "bin",
    }
}

/// Save decoded binary data and return a message about the saved file.
fn save_binary_blob(
    b64_data: &str,
    mime_type: &str,
    server_name: &str,
    _tool_name: &str,
) -> Option<String> {
    use base64::engine::general_purpose::STANDARD;

    let clean: String = b64_data.chars().filter(|c| !c.is_whitespace()).collect();
    let bytes = STANDARD.decode(&clean).ok()?;
    let ext = infer_extension(&bytes, mime_type);
    let size = bytes.len();

    let dir = mcp_output_dir()?;
    std::fs::create_dir_all(&dir).ok()?;

    let timestamp = std::time::SystemTime::now()
        .duration_since(std::time::UNIX_EPOCH)
        .unwrap_or_default()
        .as_millis();

    let filename = format!("{server_name}-{timestamp}.{ext}");
    let path = dir.join(&filename);
    std::fs::write(&path, &bytes).ok()?;

    info!(
        server = %server_name,
        path = %path.display(),
        size,
        "Saved MCP binary blob"
    );

    Some(format!(
        "[Binary content saved to {} ({size} bytes)]",
        path.display()
    ))
}

// ---------------------------------------------------------------------------
// F51: MCP Progress Logging
// ---------------------------------------------------------------------------

/// Execute an MCP tool call with progress logging for long-running operations.
///
/// If the call takes longer than `MCP_PROGRESS_LOG_INTERVAL_SECS`, logs a
/// progress message every interval until the call completes.
async fn call_tool_with_progress(
    client: &Arc<Mutex<MCPClient>>,
    server_name: &str,
    tool_name: &str,
    arguments: Value,
    timeout_duration: Duration,
) -> Result<Value> {
    let server = server_name.to_string();
    let tool = tool_name.to_string();
    let start = std::time::Instant::now();

    let call_fut = ExtensionManager::call_tool_with_timeout(
        client,
        server_name,
        tool_name,
        arguments,
        timeout_duration,
    );

    let progress_fut = async {
        let mut interval =
            tokio::time::interval(Duration::from_secs(MCP_PROGRESS_LOG_INTERVAL_SECS));
        // Skip the first immediate tick
        interval.tick().await;
        loop {
            interval.tick().await;
            let elapsed_secs = start.elapsed().as_secs();
            log_mcp_progress(&server, &tool, elapsed_secs);
        }
    };

    tokio::select! {
        result = call_fut => result,
        _ = progress_fut => {
            // This branch never completes (infinite loop), but required for select!
            unreachable!()
        }
    }
}

/// Log progress for a long-running MCP tool call.
pub fn log_mcp_progress(server: &str, tool: &str, elapsed_secs: u64) {
    info!(
        server = %server,
        tool = %tool,
        elapsed_secs,
        "MCP tool call still in progress"
    );
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

#[cfg(test)]
mod tests {
    use super::*;
    use crate::transport::{InMemoryTransport, JsonRpcMessage, MCPTransport};
    use serde_json::json;

    /// Spawn a mock MCP server that responds to initialize, tools/list, and tools/call.
    async fn run_mock_server(mut transport: InMemoryTransport, tools: Vec<MCPTool>) {
        // initialize
        let req = transport.receive().await.unwrap();
        let resp = JsonRpcMessage {
            jsonrpc: "2.0".to_string(),
            id: req.id.clone(),
            method: None,
            params: None,
            result: Some(json!({
                "protocolVersion": "2024-11-05",
                "capabilities": { "tools": {} },
                "serverInfo": { "name": "mock", "version": "1.0" }
            })),
            error: None,
        };
        transport.send(&resp).await.unwrap();

        // initialized notification
        let _notif = transport.receive().await.unwrap();

        // tools/list
        let req = transport.receive().await.unwrap();
        let tools_json: Vec<Value> = tools
            .iter()
            .map(|t| {
                json!({
                    "name": t.name,
                    "description": t.description,
                    "inputSchema": t.parameters
                })
            })
            .collect();
        let resp = JsonRpcMessage {
            jsonrpc: "2.0".to_string(),
            id: req.id.clone(),
            method: None,
            params: None,
            result: Some(json!({ "tools": tools_json })),
            error: None,
        };
        transport.send(&resp).await.unwrap();

        // tools/call (loop to handle multiple calls)
        loop {
            let Ok(req) = transport.receive().await else {
                break;
            };

            let resp = JsonRpcMessage {
                jsonrpc: "2.0".to_string(),
                id: req.id.clone(),
                method: None,
                params: None,
                result: Some(json!({
                    "content": [{ "type": "text", "text": "mock result" }],
                    "isError": false
                })),
                error: None,
            };
            if transport.send(&resp).await.is_err() {
                break;
            }
        }
    }

    #[tokio::test]
    async fn extension_manager_with_mock_server() {
        let (client_transport, server_transport) = InMemoryTransport::pair();

        let tools = vec![
            MCPTool {
                name: "tool_a".to_string(),
                description: "Tool A".to_string(),
                parameters: json!({"type": "object"}),
            },
            MCPTool {
                name: "tool_b".to_string(),
                description: "Tool B".to_string(),
                parameters: json!({"type": "object"}),
            },
        ];

        let server_handle = tokio::spawn(run_mock_server(server_transport, tools));

        let mut manager = ExtensionManager::new();

        // Manually connect using the in-memory transport
        let mut client = MCPClient::new(Box::new(client_transport), "mock-server");
        client.initialize().await.unwrap();
        let mcp_tools = client.list_tools().await.unwrap();
        for tool in &mcp_tools {
            manager
                .tools
                .push(("mock-server".to_string(), tool.clone()));
        }
        manager
            .clients
            .insert("mock-server".to_string(), Arc::new(Mutex::new(client)));

        assert_eq!(manager.tool_count(), 2);
        assert_eq!(manager.server_count(), 1);

        let ava_tools = manager.list_tools();
        assert_eq!(ava_tools.len(), 2);
        assert_eq!(ava_tools[0].name, "tool_a");

        assert_eq!(manager.server_for_tool("tool_a"), Some("mock-server"));
        assert_eq!(manager.server_for_tool("nonexistent"), None);

        // Call a tool
        let result = manager.call_tool("tool_a", json!({})).await.unwrap();
        assert_eq!(result.content, "mock result");
        assert!(!result.is_error);

        manager.shutdown().await.unwrap();
        assert_eq!(manager.tool_count(), 0);
        assert_eq!(manager.server_count(), 0);

        // Server task should end when transport closes
        let _ = server_handle.await;
    }

    #[tokio::test]
    async fn call_tool_times_out_for_unresponsive_server() {
        let (client_transport, mut server_transport) = InMemoryTransport::pair();

        let server_handle = tokio::spawn(async move {
            let req = server_transport.receive().await.unwrap();
            let resp = JsonRpcMessage {
                jsonrpc: "2.0".to_string(),
                id: req.id.clone(),
                method: None,
                params: None,
                result: Some(json!({
                    "protocolVersion": "2024-11-05",
                    "capabilities": { "tools": {} },
                    "serverInfo": { "name": "mock", "version": "1.0" }
                })),
                error: None,
            };
            server_transport.send(&resp).await.unwrap();

            let _ = server_transport.receive().await.unwrap();

            let req = server_transport.receive().await.unwrap();
            let resp = JsonRpcMessage {
                jsonrpc: "2.0".to_string(),
                id: req.id.clone(),
                method: None,
                params: None,
                result: Some(json!({
                    "tools": [{
                        "name": "slow_tool",
                        "description": "Slow tool",
                        "inputSchema": {"type": "object"}
                    }]
                })),
                error: None,
            };
            server_transport.send(&resp).await.unwrap();

            let _ = server_transport.receive().await.unwrap();
            tokio::time::sleep(Duration::from_millis(100)).await;
        });

        let mut client = MCPClient::new(Box::new(client_transport), "mock-server");
        client.initialize().await.unwrap();
        let mcp_tools = client.list_tools().await.unwrap();

        let mut manager = ExtensionManager::new();
        for tool in &mcp_tools {
            manager
                .tools
                .push(("mock-server".to_string(), tool.clone()));
        }
        manager
            .clients
            .insert("mock-server".to_string(), Arc::new(Mutex::new(client)));

        let err = ExtensionManager::call_tool_with_timeout(
            manager.clients.get("mock-server").unwrap(),
            "mock-server",
            "slow_tool",
            json!({}),
            Duration::from_millis(20),
        )
        .await
        .unwrap_err();

        assert!(err.to_string().contains("timed out"));
        let _ = server_handle.await;
    }

    #[test]
    fn extract_text_from_content_array() {
        let result = json!({
            "content": [
                { "type": "text", "text": "line 1" },
                { "type": "text", "text": "line 2" }
            ]
        });
        assert_eq!(extract_text_content(&result), "line 1\nline 2");
    }

    #[test]
    fn extract_text_from_string_content() {
        let result = json!({ "content": "plain text" });
        assert_eq!(extract_text_content(&result), "plain text");
    }

    #[test]
    fn extract_text_empty() {
        let result = json!({});
        let text = extract_text_content(&result);
        assert_eq!(text, "{}");
    }

    // --- F41: MCP Result Token Validation tests ---

    #[test]
    fn truncate_under_limit_unchanged() {
        let content = "Hello, world!";
        let result = truncate_mcp_output(content, "test-server", "test-tool");
        assert_eq!(result, content);
    }

    #[test]
    fn truncate_over_limit_adds_notice() {
        let content = "x".repeat(MAX_MCP_OUTPUT_CHARS + 500);
        let result = truncate_mcp_output(&content, "test-server", "test-tool");

        assert!(result.len() < content.len());
        assert!(result.contains("[MCP output truncated"));
        assert!(result.contains(&format!("{}", MAX_MCP_OUTPUT_CHARS + 500)));
        assert!(result.contains(&format!("{MAX_MCP_OUTPUT_CHARS}")));
        // First part should be preserved
        assert!(result.starts_with(&"x".repeat(100)));
    }

    #[test]
    fn truncate_exact_limit_unchanged() {
        let content = "y".repeat(MAX_MCP_OUTPUT_CHARS);
        let result = truncate_mcp_output(&content, "server", "tool");
        assert_eq!(result, content);
    }

    // --- F43: Binary Blob Handling tests ---

    #[test]
    fn detect_base64_png_blob() {
        // Create a fake PNG: magic bytes + padding
        let mut png_data = vec![0x89u8, 0x50, 0x4E, 0x47]; // PNG magic
        png_data.extend(vec![0u8; 1000]); // padding to make it substantial
        let b64 = base64::engine::general_purpose::STANDARD.encode(&png_data);

        let result = json!({
            "content": [{
                "type": "image",
                "blob": b64,
                "mimeType": "image/png"
            }]
        });

        let msg = detect_and_save_binary_blob(&result, "test-server", "screenshot");
        assert!(msg.is_some());
        let msg = msg.unwrap();
        assert!(msg.contains("Binary content saved to"));
        assert!(msg.contains("bytes"));
        assert!(msg.contains(".png"));
    }

    #[test]
    fn detect_base64_text_passthrough() {
        // Short base64-like text should NOT be detected as binary
        let result = json!({
            "content": [{
                "type": "text",
                "text": "SGVsbG8gV29ybGQ="  // "Hello World" in base64
            }]
        });

        let msg = detect_and_save_binary_blob(&result, "server", "tool");
        assert!(msg.is_none());
    }

    #[test]
    fn non_binary_content_passthrough() {
        let result = json!({
            "content": [{
                "type": "text",
                "text": "This is just regular text output from the MCP tool"
            }]
        });

        let msg = detect_and_save_binary_blob(&result, "server", "tool");
        assert!(msg.is_none());
    }

    #[test]
    fn infer_extension_from_magic_bytes() {
        assert_eq!(infer_extension(&[0x89, 0x50, 0x4E, 0x47], ""), "png");
        assert_eq!(infer_extension(&[0xFF, 0xD8, 0x00, 0x00], ""), "jpg");
        assert_eq!(infer_extension(b"%PDF-1.4", ""), "pdf");
        assert_eq!(infer_extension(&[0x47, 0x49, 0x46, 0x38], ""), "gif"); // GIF8
        assert_eq!(infer_extension(&[0x00, 0x00, 0x00, 0x00], ""), "bin");
    }

    #[test]
    fn infer_extension_from_mime_type() {
        assert_eq!(infer_extension(&[], "image/png"), "png");
        assert_eq!(infer_extension(&[], "image/jpeg"), "jpg");
        assert_eq!(infer_extension(&[], "application/pdf"), "pdf");
        assert_eq!(infer_extension(&[], "application/octet-stream"), "bin");
    }

    #[test]
    fn is_likely_base64_detection() {
        // Short string: not base64
        assert!(!is_likely_base64("SGVsbG8="));

        // Long valid base64
        let long_b64 = "A".repeat(2000);
        assert!(is_likely_base64(&long_b64));

        // Long string with non-base64 chars
        let non_b64 = "Hello World! ".repeat(200);
        assert!(!is_likely_base64(&non_b64));
    }

    // --- F51: MCP Progress Logging test ---

    #[test]
    fn log_mcp_progress_formats_correctly() {
        // Just verify the function exists and can be called (tracing output
        // is captured by test infrastructure, not asserted on directly)
        log_mcp_progress("test-server", "slow-tool", 60);
        log_mcp_progress("test-server", "slow-tool", 90);
    }
}
