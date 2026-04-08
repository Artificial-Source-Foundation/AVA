//! Top-level plugin manager — owns all plugin runtimes and orchestrates lifecycle.

use ava_types::Result;
use serde::{Deserialize, Serialize};
use serde_json::Value;
use std::collections::HashMap;
use std::path::PathBuf;
use std::sync::Arc;
use std::time::Duration;
use tokio::sync::Mutex;
use tracing::{debug, info, warn};

/// Maximum time allowed for a plugin's `initialize` call before it is
/// considered hung and the plugin is marked as Failed.
const PLUGIN_INIT_TIMEOUT_SECS: u64 = 10;
const PLUGIN_APP_CALL_TIMEOUT_SECS: u64 = 15;

use crate::discovery::discover_plugins;
use crate::hooks::{AuthCredentials, AuthMethodsResponse, HookDispatcher, HookEvent, HookResponse};
use crate::runtime::PluginProcess;

/// Status of a managed plugin.
#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub enum PluginStatus {
    /// Plugin process is running and ready.
    Running,
    /// Plugin process has exited or failed.
    Stopped,
    /// Plugin failed to start.
    Failed(String),
}

/// Summary info for a loaded plugin.
#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct PluginInfo {
    /// Plugin name from manifest.
    pub name: String,
    /// Plugin version from manifest.
    pub version: String,
    /// Current status.
    pub status: PluginStatus,
    /// Hook subscriptions declared in manifest.
    pub hooks: Vec<String>,
    /// App host capabilities exposed by the plugin.
    pub app: PluginAppCapabilities,
}

/// App-level capabilities a plugin can expose to the host.
#[derive(Debug, Clone, Default, Serialize, Deserialize, PartialEq, Eq)]
#[serde(rename_all = "camelCase")]
pub struct PluginAppCapabilities {
    #[serde(default)]
    pub commands: Vec<PluginCommandSpec>,
    #[serde(default)]
    pub routes: Vec<PluginRouteSpec>,
    #[serde(default)]
    pub events: Vec<PluginEventSpec>,
    #[serde(default)]
    pub mounts: Vec<PluginMountSpec>,
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq, Eq)]
#[serde(rename_all = "camelCase")]
pub struct PluginCommandSpec {
    pub name: String,
    #[serde(default)]
    pub description: String,
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq, Eq)]
#[serde(rename_all = "camelCase")]
pub struct PluginRouteSpec {
    pub path: String,
    pub method: String,
    #[serde(default)]
    pub description: String,
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq, Eq)]
#[serde(rename_all = "camelCase")]
pub struct PluginEventSpec {
    pub name: String,
    #[serde(default)]
    pub description: String,
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq, Eq)]
#[serde(rename_all = "camelCase")]
pub struct PluginMountSpec {
    pub id: String,
    pub location: String,
    pub label: String,
    #[serde(default)]
    pub description: String,
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
#[serde(rename_all = "camelCase")]
pub struct PluginAppEvent {
    pub event: String,
    #[serde(default)]
    pub payload: Value,
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
#[serde(rename_all = "camelCase")]
pub struct PluginAppResponse {
    pub result: Value,
    #[serde(default)]
    pub emitted_events: Vec<PluginAppEvent>,
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq, Eq)]
#[serde(rename_all = "camelCase")]
pub struct PluginMountRegistration {
    pub plugin: String,
    pub mount: PluginMountSpec,
}

#[derive(Clone)]
pub struct PluginAppHandle {
    plugin: String,
    process: Arc<Mutex<PluginProcess>>,
}

impl PluginAppHandle {
    pub async fn invoke_command(&self, command: &str, payload: Value) -> Result<PluginAppResponse> {
        let response =
            tokio::time::timeout(Duration::from_secs(PLUGIN_APP_CALL_TIMEOUT_SECS), async {
                self.process
                    .lock()
                    .await
                    .send_request(
                        "app.command",
                        serde_json::json!({
                            "command": command,
                            "payload": payload,
                        }),
                    )
                    .await
            })
            .await
            .map_err(|_| {
                ava_types::AvaError::ToolError(format!(
                    "plugin '{}' command '{}' timed out after {}s",
                    self.plugin, command, PLUGIN_APP_CALL_TIMEOUT_SECS
                ))
            })??;

        Ok(parse_app_response(response))
    }

    pub async fn invoke_route(
        &self,
        method: &str,
        path: &str,
        query: Value,
        body: Option<Value>,
    ) -> Result<PluginAppResponse> {
        let response =
            tokio::time::timeout(Duration::from_secs(PLUGIN_APP_CALL_TIMEOUT_SECS), async {
                self.process
                    .lock()
                    .await
                    .send_request(
                        "app.route",
                        serde_json::json!({
                            "method": method,
                            "path": path,
                            "query": query,
                            "body": body,
                        }),
                    )
                    .await
            })
            .await
            .map_err(|_| {
                ava_types::AvaError::ToolError(format!(
                    "plugin '{}' route '{} {}' timed out after {}s",
                    self.plugin, method, path, PLUGIN_APP_CALL_TIMEOUT_SECS
                ))
            })??;

        Ok(parse_app_response(response))
    }
}

/// Decision returned by a plugin for the `permission.ask` hook.
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum PluginPermissionDecision {
    Allow,
    Deny { reason: String },
}

fn parse_app_capabilities(init_caps: &Value) -> PluginAppCapabilities {
    init_caps
        .get("app")
        .cloned()
        .and_then(|value| serde_json::from_value::<PluginAppCapabilities>(value).ok())
        .unwrap_or_default()
}

fn parse_app_response(value: Value) -> PluginAppResponse {
    #[derive(Debug, Deserialize)]
    #[serde(rename_all = "camelCase")]
    struct ResponseEnvelope {
        #[serde(default)]
        result: Value,
        #[serde(default)]
        emitted_events: Vec<PluginAppEvent>,
    }

    let looks_like_envelope = value
        .as_object()
        .map(|object| object.contains_key("result") || object.contains_key("emittedEvents"))
        .unwrap_or(false);

    if looks_like_envelope {
        if let Ok(envelope) = serde_json::from_value::<ResponseEnvelope>(value.clone()) {
            return PluginAppResponse {
                result: envelope.result,
                emitted_events: envelope.emitted_events,
            };
        }
    }

    PluginAppResponse {
        result: value,
        emitted_events: Vec::new(),
    }
}

/// Manages the lifecycle of all power plugins.
pub struct PluginManager {
    /// Running plugin processes, keyed by plugin name.
    processes: HashMap<String, Arc<Mutex<PluginProcess>>>,
    /// Plugin metadata for reporting, keyed by plugin name.
    plugin_info: HashMap<String, PluginInfo>,
    /// Hook dispatcher for routing events.
    dispatcher: HookDispatcher,
}

impl PluginManager {
    /// Create a new empty plugin manager.
    pub fn new() -> Self {
        Self {
            processes: HashMap::new(),
            plugin_info: HashMap::new(),
            dispatcher: HookDispatcher::new(),
        }
    }

    /// Returns true when at least one plugin subscribes to the given hook.
    pub fn has_hook_subscribers(&self, event: HookEvent) -> bool {
        !self.dispatcher.subscribers(&event).is_empty()
    }

    /// Discover and load all plugins from the given directories.
    ///
    /// For each discovered plugin:
    /// 1. Spawn the child process
    /// 2. Send `initialize` request
    /// 3. Register hook subscriptions
    ///
    /// Plugins that fail to start are recorded with `Failed` status but do not
    /// prevent other plugins from loading.
    pub async fn load_plugins(&mut self, dirs: &[PathBuf]) -> Result<()> {
        let discovered = discover_plugins(dirs);
        info!(count = discovered.len(), "discovered plugins");

        for plugin in discovered {
            let name = plugin.manifest.plugin.name.clone();
            let version = plugin.manifest.plugin.version.clone();
            let hooks = plugin.manifest.hooks.subscribe.clone();

            debug!(plugin = %name, "spawning plugin process");
            match PluginProcess::spawn(&plugin.manifest, &plugin.path).await {
                Ok(mut process) => {
                    // Send initialize with a timeout to prevent hanging forever.
                    let init_params = serde_json::json!({
                        "plugin": name.clone(),
                        "version": version.clone(),
                    });
                    let init_timeout = Duration::from_secs(PLUGIN_INIT_TIMEOUT_SECS);
                    let init_result =
                        tokio::time::timeout(init_timeout, process.initialize(init_params)).await;
                    let mut app = PluginAppCapabilities::default();

                    match init_result {
                        Ok(Ok(_caps)) => {
                            app = parse_app_capabilities(&_caps);
                            debug!(plugin = %name, "plugin initialized successfully");
                        }
                        Ok(Err(e)) => {
                            warn!(plugin = %name, error = %e, "plugin initialization failed, continuing anyway");
                        }
                        Err(_elapsed) => {
                            warn!(
                                plugin = %name,
                                timeout_secs = PLUGIN_INIT_TIMEOUT_SECS,
                                "plugin initialize timed out — marking as Failed"
                            );
                            // Kill the hung process and record as Failed.
                            process.shutdown().await;
                            self.plugin_info.insert(
                                name.clone(),
                                PluginInfo {
                                    name,
                                    version,
                                    status: PluginStatus::Failed(format!(
                                        "initialize timed out after {PLUGIN_INIT_TIMEOUT_SECS}s"
                                    )),
                                    hooks,
                                    app: PluginAppCapabilities::default(),
                                },
                            );
                            continue;
                        }
                    }

                    self.dispatcher.register(&name, &hooks);
                    self.processes
                        .insert(name.clone(), Arc::new(Mutex::new(process)));
                    self.plugin_info.insert(
                        name.clone(),
                        PluginInfo {
                            name,
                            version,
                            status: PluginStatus::Running,
                            hooks,
                            app,
                        },
                    );
                }
                Err(e) => {
                    warn!(plugin = %name, error = %e, "failed to spawn plugin");
                    self.plugin_info.insert(
                        name.clone(),
                        PluginInfo {
                            name,
                            version,
                            status: PluginStatus::Failed(e.to_string()),
                            hooks,
                            app: PluginAppCapabilities::default(),
                        },
                    );
                }
            }
        }

        Ok(())
    }

    /// Trigger a hook event, dispatching to all subscribed plugins.
    pub async fn trigger_hook(&mut self, event: HookEvent, params: Value) -> Vec<HookResponse> {
        self.dispatcher
            .dispatch(&event, &params, &self.processes)
            .await
    }

    // -----------------------------------------------------------------------
    // Auth sub-protocol
    // -----------------------------------------------------------------------

    /// Query all subscribed plugins for auth methods they can provide for `provider`.
    ///
    /// Each subscribed plugin receives a `hook/auth.methods` request with
    /// `{"provider": "<name>"}` and is expected to return an [`AuthMethodsResponse`].
    /// Plugins that error or time out are logged and skipped.
    pub async fn get_auth_methods(&mut self, provider: &str) -> Vec<AuthMethodsResponse> {
        let params = serde_json::json!({ "provider": provider });
        let responses = self
            .dispatcher
            .dispatch(&HookEvent::AuthMethods, &params, &self.processes)
            .await;

        let mut results = Vec::new();
        for resp in responses {
            if resp.error.is_some() {
                warn!(
                    plugin = resp.plugin_name,
                    provider,
                    error = ?resp.error,
                    "plugin failed to provide auth methods"
                );
                continue;
            }
            match serde_json::from_value::<AuthMethodsResponse>(resp.result) {
                Ok(auth_resp) => results.push(auth_resp),
                Err(e) => {
                    warn!(
                        plugin = resp.plugin_name,
                        provider,
                        error = %e,
                        "plugin returned invalid auth methods response"
                    );
                }
            }
        }
        results
    }

    /// Execute an auth flow via a plugin for the given `provider`.
    ///
    /// `method_index` selects which [`AuthMethod`](crate::hooks::AuthMethod) from
    /// the plugin's `get_auth_methods` response to use. `user_input` carries
    /// user-provided data (e.g. a pasted API key, an OAuth callback code).
    ///
    /// Sends `hook/auth.authorize` to subscribed plugins and returns the first
    /// successful [`AuthCredentials`].
    pub async fn authorize(
        &mut self,
        provider: &str,
        method_index: usize,
        user_input: Option<&str>,
    ) -> Option<AuthCredentials> {
        let params = serde_json::json!({
            "provider": provider,
            "method_index": method_index,
            "user_input": user_input,
        });
        let responses = self
            .dispatcher
            .dispatch(&HookEvent::AuthAuthorize, &params, &self.processes)
            .await;

        for resp in responses {
            if resp.error.is_some() {
                warn!(
                    plugin = resp.plugin_name,
                    provider,
                    error = ?resp.error,
                    "plugin auth authorize failed"
                );
                continue;
            }
            match serde_json::from_value::<AuthCredentials>(resp.result) {
                Ok(creds) => return Some(creds),
                Err(e) => {
                    warn!(
                        plugin = resp.plugin_name,
                        provider,
                        error = %e,
                        "plugin returned invalid auth credentials"
                    );
                }
            }
        }
        None
    }

    /// Refresh expired credentials via a plugin.
    ///
    /// Sends `hook/auth.refresh` with the provider name and refresh token.
    /// Returns the first successful [`AuthCredentials`] or `None` if no plugin
    /// can refresh.
    pub async fn refresh_auth(
        &mut self,
        provider: &str,
        refresh_token: &str,
    ) -> Option<AuthCredentials> {
        let params = serde_json::json!({
            "provider": provider,
            "refresh_token": refresh_token,
        });
        let responses = self
            .dispatcher
            .dispatch(&HookEvent::AuthRefresh, &params, &self.processes)
            .await;

        for resp in responses {
            if resp.error.is_some() {
                warn!(
                    plugin = resp.plugin_name,
                    provider,
                    error = ?resp.error,
                    "plugin auth refresh failed"
                );
                continue;
            }
            match serde_json::from_value::<AuthCredentials>(resp.result) {
                Ok(creds) => return Some(creds),
                Err(e) => {
                    warn!(
                        plugin = resp.plugin_name,
                        provider,
                        error = %e,
                        "plugin returned invalid refresh credentials"
                    );
                }
            }
        }
        None
    }

    // -----------------------------------------------------------------------
    // tool.definition — modify tool definitions before sending to the LLM
    // -----------------------------------------------------------------------

    /// Run the `tool.definition` hook, allowing plugins to modify tool definitions.
    ///
    /// Passes all tool definitions as a JSON array to subscribed plugins. Each plugin
    /// may return a modified version. Plugins are applied in subscription order; each
    /// plugin receives the output of the previous one (chain). Tools returned by
    /// a plugin that lack a `name` field are silently discarded.
    ///
    /// Returns the (possibly modified) list of tool definitions as JSON values.
    pub async fn apply_tool_definition_hook(&mut self, tools: Vec<Value>) -> Vec<Value> {
        let subscribers = self
            .dispatcher
            .subscribers(&HookEvent::ToolDefinition)
            .to_vec();
        if subscribers.is_empty() {
            return tools;
        }

        let mut current_tools = tools;
        for plugin_name in &subscribers {
            let params = serde_json::json!({ "tools": current_tools });
            let responses = self
                .dispatcher
                .dispatch_to_plugins(
                    &HookEvent::ToolDefinition,
                    &params,
                    &self.processes,
                    std::slice::from_ref(plugin_name),
                )
                .await;

            for resp in responses {
                if let Some(ref e) = resp.error {
                    warn!(
                        plugin = resp.plugin_name,
                        error = %e,
                        "tool.definition hook failed, keeping current definitions"
                    );
                    continue;
                }
                if let Some(arr) = resp.result.get("tools").and_then(|v| v.as_array()) {
                    // Only keep entries that have a "name" field (minimal validity check).
                    current_tools = arr
                        .iter()
                        .filter(|t| t.get("name").and_then(|n| n.as_str()).is_some())
                        .cloned()
                        .collect();
                } else {
                    warn!(
                        plugin = resp.plugin_name,
                        "tool.definition hook returned no 'tools' array, ignoring"
                    );
                }
            }
        }

        current_tools
    }

    // -----------------------------------------------------------------------
    // chat.params — modify LLM call parameters before each call
    // -----------------------------------------------------------------------

    /// Run the `chat.params` hook, allowing plugins to modify LLM call parameters.
    ///
    /// `params` should be a JSON object with fields like `model`, `temperature`,
    /// `max_tokens`, etc. Each subscribed plugin may override any field. Plugins
    /// are applied in subscription order (chain). Returns the (possibly modified)
    /// params object.
    pub async fn apply_chat_params_hook(&mut self, params: Value) -> Value {
        let subscribers = self.dispatcher.subscribers(&HookEvent::ChatParams).to_vec();
        if subscribers.is_empty() {
            return params;
        }

        let mut current = params;
        for plugin_name in &subscribers {
            let responses = self
                .dispatcher
                .dispatch_to_plugins(
                    &HookEvent::ChatParams,
                    &current,
                    &self.processes,
                    std::slice::from_ref(plugin_name),
                )
                .await;

            for resp in responses {
                if let Some(ref e) = resp.error {
                    warn!(
                        plugin = resp.plugin_name,
                        error = %e,
                        "chat.params hook failed, keeping current params"
                    );
                    continue;
                }
                if resp.result.is_object() {
                    // Merge: plugin response fields override current fields.
                    if let (Value::Object(current_map), Value::Object(overrides)) =
                        (&mut current, resp.result)
                    {
                        for (k, v) in overrides {
                            current_map.insert(k, v);
                        }
                    }
                } else {
                    warn!(
                        plugin = resp.plugin_name,
                        "chat.params hook did not return an object, ignoring"
                    );
                }
            }
        }

        current
    }

    // -----------------------------------------------------------------------
    // permission.ask — programmatic approve/deny of permission requests
    // -----------------------------------------------------------------------

    /// Run the `permission.ask` hook for a tool call awaiting approval.
    ///
    /// Parameters passed to plugins:
    /// ```json
    /// {"tool": "<name>", "arguments": {...}, "risk_level": "<str>", "reason": "<str>"}
    /// ```
    ///
    /// The first plugin that returns `{"action": "allow"}` or `{"action": "deny"}`
    /// wins. If no plugin returns a decision, `None` is returned and the normal
    /// interactive approval flow continues.
    pub async fn ask_permission(
        &mut self,
        tool_name: &str,
        arguments: &Value,
        risk_level: &str,
        reason: &str,
    ) -> Option<PluginPermissionDecision> {
        let params = serde_json::json!({
            "tool": tool_name,
            "arguments": arguments,
            "risk_level": risk_level,
            "reason": reason,
        });
        let responses = self
            .dispatcher
            .dispatch(&HookEvent::PermissionAsk, &params, &self.processes)
            .await;

        for resp in responses {
            if let Some(ref e) = resp.error {
                warn!(
                    plugin = resp.plugin_name,
                    tool = tool_name,
                    error = %e,
                    "permission.ask hook failed"
                );
                continue;
            }
            if let Some(action) = resp.result.get("action").and_then(|a| a.as_str()) {
                match action {
                    "allow" => return Some(PluginPermissionDecision::Allow),
                    "deny" => {
                        let reason = resp
                            .result
                            .get("reason")
                            .and_then(|r| r.as_str())
                            .unwrap_or("denied by plugin")
                            .to_string();
                        return Some(PluginPermissionDecision::Deny { reason });
                    }
                    _ => {
                        warn!(
                            plugin = resp.plugin_name,
                            action, "permission.ask returned unknown action, ignoring"
                        );
                    }
                }
            }
        }

        None // No plugin made a decision — continue normal approval flow
    }

    // -----------------------------------------------------------------------
    // chat.system — inject text into the system prompt
    // -----------------------------------------------------------------------

    /// Run the `chat.system` hook, collecting system prompt injections from plugins.
    ///
    /// Passes `{"model": "<name>", "provider": "<name>"}` to subscribed plugins.
    /// Each plugin may return `{"inject": "<text to append>"}`. All non-empty
    /// injections are concatenated with `\n\n` separators and returned.
    ///
    /// Returns `None` if no plugins are subscribed or no injections are provided.
    pub async fn collect_system_injections(
        &mut self,
        model: &str,
        provider: &str,
    ) -> Option<String> {
        let subscribers = self.dispatcher.subscribers(&HookEvent::ChatSystem).to_vec();
        if subscribers.is_empty() {
            return None;
        }

        let params = serde_json::json!({ "model": model, "provider": provider });
        let responses = self
            .dispatcher
            .dispatch(&HookEvent::ChatSystem, &params, &self.processes)
            .await;

        let mut injections: Vec<String> = Vec::new();
        for resp in responses {
            if let Some(ref e) = resp.error {
                warn!(
                    plugin = resp.plugin_name,
                    error = %e,
                    "chat.system hook failed"
                );
                continue;
            }
            if let Some(text) = resp.result.get("inject").and_then(|v| v.as_str()) {
                let trimmed = text.trim();
                if !trimmed.is_empty() {
                    injections.push(trimmed.to_string());
                }
            }
        }

        if injections.is_empty() {
            None
        } else {
            Some(injections.join("\n\n"))
        }
    }

    // -----------------------------------------------------------------------
    // chat.messages.transform — rewrite conversation history before LLM call
    // -----------------------------------------------------------------------

    /// Run the `chat.messages.transform` hook, allowing plugins to rewrite the
    /// message list before it is sent to the LLM.
    ///
    /// `messages` is a JSON array of `{"role": "...", "content": "..."}` objects.
    /// Each subscribed plugin may return a modified array under the `"messages"` key.
    /// Plugins are chained — each plugin receives the output of the previous one.
    /// Messages returned by a plugin that lack a `role` field are silently discarded.
    ///
    /// Returns the (possibly modified) message list as JSON values.
    pub async fn apply_messages_transform_hook(&mut self, messages: Vec<Value>) -> Vec<Value> {
        let subscribers = self
            .dispatcher
            .subscribers(&HookEvent::ChatMessagesTransform)
            .to_vec();
        if subscribers.is_empty() {
            return messages;
        }

        let mut current = messages;
        for plugin_name in &subscribers {
            let params = serde_json::json!({ "messages": current });
            let responses = self
                .dispatcher
                .dispatch_to_plugins(
                    &HookEvent::ChatMessagesTransform,
                    &params,
                    &self.processes,
                    std::slice::from_ref(plugin_name),
                )
                .await;

            for resp in responses {
                if let Some(ref e) = resp.error {
                    warn!(
                        plugin = resp.plugin_name,
                        error = %e,
                        "chat.messages.transform hook failed, keeping current messages"
                    );
                    continue;
                }
                if let Some(arr) = resp.result.get("messages").and_then(|v| v.as_array()) {
                    // Only keep entries that have a "role" field.
                    current = arr
                        .iter()
                        .filter(|m| m.get("role").and_then(|r| r.as_str()).is_some())
                        .cloned()
                        .collect();
                } else {
                    warn!(
                        plugin = resp.plugin_name,
                        "chat.messages.transform hook returned no 'messages' array, ignoring"
                    );
                }
            }
        }

        current
    }

    // -----------------------------------------------------------------------
    // session.compacting — inject context before compaction
    // -----------------------------------------------------------------------

    /// Run the `session.compacting` hook before context compaction starts.
    ///
    /// Parameters: `{"session_id": "...", "message_count": N, "token_count": N}`.
    /// Each plugin may return:
    /// - `"context"`: `Vec<String>` of extra context strings to inject.
    /// - `"prompt"`: `String` custom compaction prompt (first non-empty wins).
    ///
    /// Returns `(extra_context, custom_prompt)`.
    pub async fn apply_session_compacting_hook(
        &mut self,
        session_id: &str,
        message_count: usize,
        token_count: usize,
    ) -> (Vec<String>, Option<String>) {
        let subscribers = self
            .dispatcher
            .subscribers(&HookEvent::SessionCompacting)
            .to_vec();
        if subscribers.is_empty() {
            return (Vec::new(), None);
        }

        let params = serde_json::json!({
            "session_id": session_id,
            "message_count": message_count,
            "token_count": token_count,
        });
        let responses = self
            .dispatcher
            .dispatch(&HookEvent::SessionCompacting, &params, &self.processes)
            .await;

        let mut extra_context: Vec<String> = Vec::new();
        let mut custom_prompt: Option<String> = None;

        for resp in responses {
            if let Some(ref e) = resp.error {
                warn!(
                    plugin = resp.plugin_name,
                    error = %e,
                    "session.compacting hook failed"
                );
                continue;
            }
            if let Some(ctx_arr) = resp.result.get("context").and_then(|v| v.as_array()) {
                for item in ctx_arr {
                    if let Some(s) = item.as_str() {
                        let trimmed = s.trim();
                        if !trimmed.is_empty() {
                            extra_context.push(trimmed.to_string());
                        }
                    }
                }
            }
            if custom_prompt.is_none() {
                if let Some(p) = resp.result.get("prompt").and_then(|v| v.as_str()) {
                    let trimmed = p.trim();
                    if !trimmed.is_empty() {
                        custom_prompt = Some(trimmed.to_string());
                    }
                }
            }
        }

        (extra_context, custom_prompt)
    }

    // -----------------------------------------------------------------------
    // command.execute.before — allow plugins to block slash commands
    // -----------------------------------------------------------------------

    /// Run the `command.execute.before` hook before a slash command executes.
    ///
    /// Parameters: `{"command": "<name without />", "arguments": "<rest of line>"}`.
    /// The first plugin that returns `{"block": true}` wins and blocks execution.
    ///
    /// Returns `Some(reason)` if a plugin blocked the command, `None` otherwise.
    pub async fn check_command_execute_before(
        &mut self,
        command: &str,
        arguments: &str,
    ) -> Option<String> {
        let subscribers = self
            .dispatcher
            .subscribers(&HookEvent::CommandExecuteBefore)
            .to_vec();
        if subscribers.is_empty() {
            return None;
        }

        let params = serde_json::json!({
            "command": command,
            "arguments": arguments,
        });
        let responses = self
            .dispatcher
            .dispatch(&HookEvent::CommandExecuteBefore, &params, &self.processes)
            .await;

        for resp in responses {
            if let Some(ref e) = resp.error {
                warn!(
                    plugin = resp.plugin_name,
                    command,
                    error = %e,
                    "command.execute.before hook failed"
                );
                continue;
            }
            if resp
                .result
                .get("block")
                .and_then(|v| v.as_bool())
                .unwrap_or(false)
            {
                let reason = resp
                    .result
                    .get("reason")
                    .and_then(|r| r.as_str())
                    .unwrap_or("blocked by plugin")
                    .to_string();
                return Some(reason);
            }
        }

        None // No plugin blocked the command
    }

    /// Gracefully shut down all running plugin processes.
    pub async fn shutdown_all(&mut self) {
        info!(count = self.processes.len(), "shutting down all plugins");
        // Collect names to avoid borrow issues
        let names: Vec<String> = self.processes.keys().cloned().collect();
        for name in &names {
            if let Some(process) = self.processes.remove(name) {
                debug!(plugin = %name, "shutting down plugin");
                process.lock().await.shutdown().await;
            }
            if let Some(info) = self.plugin_info.get_mut(name) {
                info.status = PluginStatus::Stopped;
            }
            self.dispatcher.unregister(name);
        }
    }

    /// List all known plugins with their status and hooks.
    pub fn list_plugins(&self) -> Vec<PluginInfo> {
        self.plugin_info.values().cloned().collect()
    }

    /// List frontend mount registrations exposed by running plugins.
    pub fn list_plugin_mounts(&self) -> Vec<PluginMountRegistration> {
        self.plugin_info
            .values()
            .filter(|info| matches!(info.status, PluginStatus::Running))
            .flat_map(|info| {
                info.app
                    .mounts
                    .iter()
                    .cloned()
                    .map(|mount| PluginMountRegistration {
                        plugin: info.name.clone(),
                        mount,
                    })
            })
            .collect()
    }

    pub fn get_app_command_handle(&self, plugin: &str, command: &str) -> Result<PluginAppHandle> {
        let info = self.plugin_info.get(plugin).ok_or_else(|| {
            ava_types::AvaError::ToolError(format!("plugin '{}' is not loaded", plugin))
        })?;

        if !matches!(info.status, PluginStatus::Running) {
            return Err(ava_types::AvaError::ToolError(format!(
                "plugin '{}' is not running",
                plugin
            )));
        }

        if !info.app.commands.iter().any(|spec| spec.name == command) {
            return Err(ava_types::AvaError::ToolError(format!(
                "plugin '{}' does not expose command '{}'",
                plugin, command
            )));
        }

        let process = self.processes.get(plugin).cloned().ok_or_else(|| {
            ava_types::AvaError::ToolError(format!("plugin '{}' process is unavailable", plugin))
        })?;

        Ok(PluginAppHandle {
            plugin: plugin.to_string(),
            process,
        })
    }

    pub fn get_app_route_handle(
        &self,
        plugin: &str,
        method: &str,
        path: &str,
    ) -> Result<PluginAppHandle> {
        let info = self.plugin_info.get(plugin).ok_or_else(|| {
            ava_types::AvaError::ToolError(format!("plugin '{}' is not loaded", plugin))
        })?;

        if !matches!(info.status, PluginStatus::Running) {
            return Err(ava_types::AvaError::ToolError(format!(
                "plugin '{}' is not running",
                plugin
            )));
        }

        if !info
            .app
            .routes
            .iter()
            .any(|spec| spec.path == path && spec.method.eq_ignore_ascii_case(method))
        {
            return Err(ava_types::AvaError::ToolError(format!(
                "plugin '{}' does not expose route '{} {}'",
                plugin, method, path
            )));
        }

        let process = self.processes.get(plugin).cloned().ok_or_else(|| {
            ava_types::AvaError::ToolError(format!("plugin '{}' process is unavailable", plugin))
        })?;

        Ok(PluginAppHandle {
            plugin: plugin.to_string(),
            process,
        })
    }

    /// Get the number of running plugins.
    pub fn running_count(&self) -> usize {
        self.processes.len()
    }
}

impl Default for PluginManager {
    fn default() -> Self {
        Self::new()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn new_manager_is_empty() {
        let manager = PluginManager::new();
        assert!(manager.list_plugins().is_empty());
        assert!(manager.list_plugin_mounts().is_empty());
        assert_eq!(manager.running_count(), 0);
    }

    #[tokio::test]
    async fn load_from_empty_dir() {
        let tmp = tempfile::TempDir::new().unwrap();
        let mut manager = PluginManager::new();
        manager
            .load_plugins(&[tmp.path().to_path_buf()])
            .await
            .unwrap();
        assert!(manager.list_plugins().is_empty());
        assert_eq!(manager.running_count(), 0);
    }

    #[tokio::test]
    async fn load_from_nonexistent_dir() {
        let mut manager = PluginManager::new();
        manager
            .load_plugins(&[PathBuf::from("/nonexistent/plugins")])
            .await
            .unwrap();
        assert!(manager.list_plugins().is_empty());
    }

    #[tokio::test]
    async fn trigger_hook_with_no_plugins() {
        let mut manager = PluginManager::new();
        let responses = manager
            .trigger_hook(HookEvent::Auth, serde_json::json!({}))
            .await;
        assert!(responses.is_empty());
    }

    #[tokio::test]
    async fn shutdown_empty_manager() {
        let mut manager = PluginManager::new();
        manager.shutdown_all().await;
        assert_eq!(manager.running_count(), 0);
    }

    #[tokio::test]
    async fn get_auth_methods_no_plugins() {
        let mut manager = PluginManager::new();
        let methods = manager.get_auth_methods("copilot").await;
        assert!(methods.is_empty(), "no plugins = no auth methods");
    }

    #[tokio::test]
    async fn authorize_no_plugins() {
        let mut manager = PluginManager::new();
        let creds = manager.authorize("copilot", 0, None).await;
        assert!(creds.is_none(), "no plugins = no credentials");
    }

    #[tokio::test]
    async fn refresh_auth_no_plugins() {
        let mut manager = PluginManager::new();
        let creds = manager.refresh_auth("copilot", "old-token").await;
        assert!(creds.is_none(), "no plugins = no refresh");
    }

    #[tokio::test]
    async fn apply_tool_definition_hook_no_plugins() {
        let mut manager = PluginManager::new();
        let tools = vec![
            serde_json::json!({"name": "read", "description": "Read a file", "parameters": {}}),
        ];
        let result = manager.apply_tool_definition_hook(tools.clone()).await;
        assert_eq!(result, tools, "no plugins = unchanged tool definitions");
    }

    #[tokio::test]
    async fn apply_chat_params_hook_no_plugins() {
        let mut manager = PluginManager::new();
        let params = serde_json::json!({ "model": "claude-sonnet-4", "temperature": 0.7 });
        let result = manager.apply_chat_params_hook(params.clone()).await;
        assert_eq!(result, params, "no plugins = unchanged params");
    }

    #[tokio::test]
    async fn ask_permission_no_plugins() {
        let mut manager = PluginManager::new();
        let result = manager
            .ask_permission(
                "bash",
                &serde_json::json!({"command": "ls"}),
                "medium",
                "needs approval",
            )
            .await;
        assert!(result.is_none(), "no plugins = no decision");
    }

    #[tokio::test]
    async fn collect_system_injections_no_plugins() {
        let mut manager = PluginManager::new();
        let result = manager
            .collect_system_injections("claude-sonnet-4", "anthropic")
            .await;
        assert!(result.is_none(), "no plugins = no injections");
    }

    #[test]
    fn parse_app_capabilities_defaults_cleanly() {
        let capabilities = parse_app_capabilities(&serde_json::json!({"hooks": ["session.start"]}));
        assert_eq!(capabilities, PluginAppCapabilities::default());
    }

    #[test]
    fn parse_app_capabilities_reads_app_block() {
        let capabilities = parse_app_capabilities(&serde_json::json!({
            "app": {
                "commands": [{"name": "demo.ping", "description": "Ping"}],
                "routes": [{"path": "/status", "method": "GET", "description": "Status"}],
                "events": [{"name": "demo.updated", "description": "Updated"}],
                "mounts": [{"id": "demo.settings", "location": "settings.section", "label": "Demo", "description": "Demo settings"}]
            }
        }));
        assert_eq!(capabilities.commands.len(), 1);
        assert_eq!(capabilities.routes.len(), 1);
        assert_eq!(capabilities.events.len(), 1);
        assert_eq!(capabilities.mounts.len(), 1);
    }

    #[test]
    fn parse_app_response_supports_envelope_and_bare_result() {
        let enveloped = parse_app_response(serde_json::json!({
            "result": {"ok": true},
            "emittedEvents": [{"event": "demo.updated", "payload": {"count": 1}}]
        }));
        assert_eq!(enveloped.emitted_events.len(), 1);
        assert_eq!(enveloped.result["ok"], true);

        let bare = parse_app_response(serde_json::json!({"status": "ok"}));
        assert!(bare.emitted_events.is_empty());
        assert_eq!(bare.result["status"], "ok");
    }

    #[tokio::test]
    async fn invoke_app_command_missing_plugin() {
        let manager = PluginManager::new();
        let result = manager.get_app_command_handle("missing", "demo.ping");
        assert!(result.is_err());
    }

    #[tokio::test]
    async fn invoke_app_route_missing_plugin() {
        let manager = PluginManager::new();
        let result = manager.get_app_route_handle("missing", "GET", "/status");
        assert!(result.is_err());
    }

    #[test]
    fn plugin_status_eq() {
        assert_eq!(PluginStatus::Running, PluginStatus::Running);
        assert_eq!(PluginStatus::Stopped, PluginStatus::Stopped);
        assert_ne!(PluginStatus::Running, PluginStatus::Stopped);
        assert_eq!(
            PluginStatus::Failed("err".to_string()),
            PluginStatus::Failed("err".to_string())
        );
    }
}
