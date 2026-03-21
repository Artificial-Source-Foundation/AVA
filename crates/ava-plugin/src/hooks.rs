//! Hook event types and dispatch routing.

use serde::{Deserialize, Serialize};
use serde_json::Value;
use std::collections::HashMap;
use std::time::Duration;
use tracing::{debug, warn};

use crate::runtime::PluginProcess;

// ---------------------------------------------------------------------------
// Auth sub-protocol types
// ---------------------------------------------------------------------------

/// An authentication method that a plugin can offer for a provider.
#[derive(Debug, Clone, Serialize, Deserialize, PartialEq, Eq)]
#[serde(tag = "type", rename_all = "snake_case")]
pub enum AuthMethod {
    /// Prompt the user for an API key.
    ApiKey {
        /// Human-readable prompt to display (e.g. "Enter your Acme API key").
        prompt: String,
    },
    /// Browser-based OAuth with a local redirect.
    OAuth {
        /// The authorization URL to open in the user's browser.
        auth_url: String,
        /// Port for the local callback server.
        callback_port: u16,
    },
    /// Device-code flow (e.g. GitHub Copilot).
    DeviceCode {
        /// URL where the user enters the code.
        verification_url: String,
        /// Code to display to the user.
        user_code: String,
    },
}

/// Response from a plugin listing its supported auth methods for a provider.
#[derive(Debug, Clone, Serialize, Deserialize, PartialEq, Eq)]
pub struct AuthMethodsResponse {
    /// Provider name this auth applies to (e.g. "copilot", "acme").
    pub provider: String,
    /// Available authentication methods, ordered by preference.
    pub methods: Vec<AuthMethod>,
}

/// Credentials returned by a plugin after a successful auth or refresh flow.
#[derive(Debug, Clone, Serialize, Deserialize, PartialEq, Eq)]
pub struct AuthCredentials {
    /// Provider name (must match the request).
    pub provider: String,
    /// API key, if the flow produces one.
    #[serde(skip_serializing_if = "Option::is_none")]
    pub api_key: Option<String>,
    /// OAuth access token.
    #[serde(skip_serializing_if = "Option::is_none")]
    pub oauth_token: Option<String>,
    /// OAuth refresh token for later renewal.
    #[serde(skip_serializing_if = "Option::is_none")]
    pub refresh_token: Option<String>,
    /// Unix timestamp (seconds) when the token expires.
    #[serde(skip_serializing_if = "Option::is_none")]
    pub expires_at: Option<i64>,
    /// Extra headers to inject into LLM requests (e.g. `x-custom-auth`).
    #[serde(default)]
    pub headers: HashMap<String, String>,
}

// ---------------------------------------------------------------------------
// Hook events
// ---------------------------------------------------------------------------

/// Hook events that plugins can subscribe to.
#[derive(Debug, Clone, PartialEq, Eq, Hash, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum HookEvent {
    /// Provide credentials for a provider.
    Auth,
    /// Query available auth methods for a provider.
    AuthMethods,
    /// Execute an auth flow and return credentials.
    AuthAuthorize,
    /// Refresh expired tokens.
    AuthRefresh,
    /// Inject headers into LLM API calls.
    RequestHeaders,
    /// Intercept tool call before execution.
    ToolBefore,
    /// Intercept tool result after execution.
    ToolAfter,
    /// Modify tool definitions (description/schema) before they are sent to the LLM.
    ///
    /// Params: `{"tools": [{"name": "...", "description": "...", "parameters": {...}}, ...]}`
    /// Response: `{"tools": [...]}` (same structure, with modifications applied).
    /// This is a request/response hook (blocking).
    ToolDefinition,
    /// Modify LLM call parameters (temperature, max_tokens, etc.) before each call.
    ///
    /// Params: `{"model": "...", "temperature": 0.7, "max_tokens": 4096, ...}`
    /// Response: same structure with any fields overridden.
    /// This is a request/response hook (blocking).
    ChatParams,
    /// Allow plugins to programmatically approve or deny a permission request.
    ///
    /// Params: `{"tool": "...", "arguments": {...}, "risk_level": "...", "reason": "..."}`
    /// Response: `{"action": "allow" | "deny", "reason": "..."}`.
    /// This is a request/response hook (blocking). If no plugin responds, the normal
    /// approval flow continues.
    PermissionAsk,
    /// Inject additional text into the system prompt before each agent run.
    ///
    /// Params: `{"model": "...", "provider": "..."}`
    /// Response: `{"inject": "..."}` — text to append to the system prompt.
    /// This is a request/response hook (blocking).
    ChatSystem,
    /// Agent turn starting (notification — no response expected).
    AgentBefore,
    /// Agent turn completed (notification — no response expected).
    AgentAfter,
    /// Session created/resumed (notification).
    SessionStart,
    /// Session ended (notification).
    SessionEnd,
    /// Modify config at runtime.
    Config,
    /// Broadcast any AgentEvent (notification).
    Event,
    /// Inject env vars into bash tool.
    ShellEnv,
    /// Transform messages before sending to the LLM (request/response, blocking).
    ///
    /// Params: `{"messages": [{"role": "user|assistant", "content": "..."}]}`
    /// Response: `{"messages": [...]}` — modified message array.
    /// Lets plugins rewrite conversation history (add context, filter, translate).
    ChatMessagesTransform,
    /// Fires before context compaction starts (request/response, blocking).
    ///
    /// Params: `{"session_id": "...", "message_count": N, "token_count": N}`
    /// Response: `{"context": ["extra context strings"], "prompt": "custom compaction prompt"}`
    /// Lets plugins inject extra context or customize the compaction prompt.
    SessionCompacting,
    /// Fires on each new user message before the agent processes it (notification).
    ///
    /// Params: `{"session_id": "...", "message": {"role": "user", "content": "..."}}`
    ChatMessage,
    /// Fires when a text response finishes streaming (notification).
    ///
    /// Params: `{"session_id": "...", "content": "full response text", "token_count": N}`
    TextComplete,
    /// Fires before a slash command executes (request/response, blocking).
    ///
    /// Params: `{"command": "model", "arguments": "claude-sonnet-4"}`
    /// Response: `{"block": true, "reason": "..."}` — can block execution.
    CommandExecuteBefore,
}

impl HookEvent {
    /// The wire name used in `plugin.toml` subscriptions and JSON-RPC methods.
    pub fn wire_name(&self) -> &'static str {
        match self {
            Self::Auth => "auth",
            Self::AuthMethods => "auth.methods",
            Self::AuthAuthorize => "auth.authorize",
            Self::AuthRefresh => "auth.refresh",
            Self::RequestHeaders => "request.headers",
            Self::ToolBefore => "tool.before",
            Self::ToolAfter => "tool.after",
            Self::ToolDefinition => "tool.definition",
            Self::ChatParams => "chat.params",
            Self::PermissionAsk => "permission.ask",
            Self::ChatSystem => "chat.system",
            Self::AgentBefore => "agent.before",
            Self::AgentAfter => "agent.after",
            Self::SessionStart => "session.start",
            Self::SessionEnd => "session.end",
            Self::Config => "config",
            Self::Event => "event",
            Self::ShellEnv => "shell.env",
            Self::ChatMessagesTransform => "chat.messages.transform",
            Self::SessionCompacting => "session.compacting",
            Self::ChatMessage => "chat.message",
            Self::TextComplete => "text.complete",
            Self::CommandExecuteBefore => "command.execute.before",
        }
    }

    /// Parse a wire name back to a HookEvent.
    pub fn from_wire_name(name: &str) -> Option<Self> {
        match name {
            "auth" => Some(Self::Auth),
            "auth.methods" => Some(Self::AuthMethods),
            "auth.authorize" => Some(Self::AuthAuthorize),
            "auth.refresh" => Some(Self::AuthRefresh),
            "request.headers" => Some(Self::RequestHeaders),
            "tool.before" => Some(Self::ToolBefore),
            "tool.after" => Some(Self::ToolAfter),
            "tool.definition" => Some(Self::ToolDefinition),
            "chat.params" => Some(Self::ChatParams),
            "permission.ask" => Some(Self::PermissionAsk),
            "chat.system" => Some(Self::ChatSystem),
            "agent.before" => Some(Self::AgentBefore),
            "agent.after" => Some(Self::AgentAfter),
            "session.start" => Some(Self::SessionStart),
            "session.end" => Some(Self::SessionEnd),
            "config" => Some(Self::Config),
            "event" => Some(Self::Event),
            "shell.env" => Some(Self::ShellEnv),
            "chat.messages.transform" => Some(Self::ChatMessagesTransform),
            "session.compacting" => Some(Self::SessionCompacting),
            "chat.message" => Some(Self::ChatMessage),
            "text.complete" => Some(Self::TextComplete),
            "command.execute.before" => Some(Self::CommandExecuteBefore),
            _ => None,
        }
    }

    /// Whether this hook is a notification (fire-and-forget, no response expected).
    pub fn is_notification(&self) -> bool {
        matches!(
            self,
            Self::AgentBefore
                | Self::AgentAfter
                | Self::SessionStart
                | Self::SessionEnd
                | Self::Event
                | Self::ChatMessage
                | Self::TextComplete
        )
    }
}

/// A hook call sent to a plugin.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct HookRequest {
    /// The hook event type.
    pub event: HookEvent,
    /// Arbitrary parameters for this hook call.
    pub params: Value,
}

/// A response from a plugin for a hook call.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct HookResponse {
    /// The plugin that produced this response.
    pub plugin_name: String,
    /// The result value (if successful).
    pub result: Value,
    /// Error message (if the hook call failed).
    pub error: Option<String>,
}

/// Routes hook calls to the correct subscribed plugins.
pub struct HookDispatcher {
    /// Map from hook wire name to list of plugin names that subscribe to it.
    subscriptions: HashMap<String, Vec<String>>,
    /// Default timeout for request/response hooks.
    timeout: Duration,
}

impl HookDispatcher {
    /// Create a new dispatcher with default timeout (5 seconds).
    pub fn new() -> Self {
        Self {
            subscriptions: HashMap::new(),
            timeout: Duration::from_secs(5),
        }
    }

    /// Create a new dispatcher with a custom timeout.
    pub fn with_timeout(timeout: Duration) -> Self {
        Self {
            subscriptions: HashMap::new(),
            timeout,
        }
    }

    /// Register a plugin's hook subscriptions.
    pub fn register(&mut self, plugin_name: &str, hook_names: &[String]) {
        for hook_name in hook_names {
            self.subscriptions
                .entry(hook_name.clone())
                .or_default()
                .push(plugin_name.to_string());
        }
    }

    /// Unregister all subscriptions for a plugin.
    pub fn unregister(&mut self, plugin_name: &str) {
        for subscribers in self.subscriptions.values_mut() {
            subscribers.retain(|name| name != plugin_name);
        }
    }

    /// Get the list of plugin names subscribed to a given hook.
    pub fn subscribers(&self, event: &HookEvent) -> &[String] {
        self.subscriptions
            .get(event.wire_name())
            .map(|v| v.as_slice())
            .unwrap_or(&[])
    }

    /// Dispatch a hook event to all subscribed plugins.
    ///
    /// For notification hooks, sends fire-and-forget. For request/response hooks,
    /// waits for each plugin's response with a timeout.
    ///
    /// One plugin's failure does not prevent other plugins from being called.
    pub async fn dispatch(
        &self,
        event: &HookEvent,
        params: &Value,
        plugins: &mut HashMap<String, PluginProcess>,
    ) -> Vec<HookResponse> {
        let subscribers = self.subscribers(event);
        if subscribers.is_empty() {
            return Vec::new();
        }

        let method = format!("hook/{}", event.wire_name());
        let mut responses = Vec::new();

        for plugin_name in subscribers {
            let Some(process) = plugins.get_mut(plugin_name) else {
                warn!(
                    plugin = plugin_name,
                    hook = event.wire_name(),
                    "plugin process not found, skipping"
                );
                continue;
            };

            if event.is_notification() {
                // Fire and forget
                debug!(
                    plugin = plugin_name,
                    hook = event.wire_name(),
                    "sending notification"
                );
                process.send_notification(&method, params.clone()).await;
            } else {
                // Request/response with timeout
                debug!(
                    plugin = plugin_name,
                    hook = event.wire_name(),
                    "sending request"
                );
                match tokio::time::timeout(
                    self.timeout,
                    process.send_request(&method, params.clone()),
                )
                .await
                {
                    Ok(Ok(result)) => {
                        responses.push(HookResponse {
                            plugin_name: plugin_name.clone(),
                            result,
                            error: None,
                        });
                    }
                    Ok(Err(e)) => {
                        warn!(
                            plugin = plugin_name,
                            hook = event.wire_name(),
                            error = %e,
                            "hook call failed"
                        );
                        responses.push(HookResponse {
                            plugin_name: plugin_name.clone(),
                            result: Value::Null,
                            error: Some(e.to_string()),
                        });
                    }
                    Err(_) => {
                        warn!(
                            plugin = plugin_name,
                            hook = event.wire_name(),
                            timeout_secs = self.timeout.as_secs(),
                            "hook call timed out"
                        );
                        responses.push(HookResponse {
                            plugin_name: plugin_name.clone(),
                            result: Value::Null,
                            error: Some(format!(
                                "hook timed out after {}s",
                                self.timeout.as_secs()
                            )),
                        });
                    }
                }
            }
        }

        responses
    }
}

impl Default for HookDispatcher {
    fn default() -> Self {
        Self::new()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn hook_event_wire_name_roundtrip() {
        let events = vec![
            HookEvent::Auth,
            HookEvent::AuthMethods,
            HookEvent::AuthAuthorize,
            HookEvent::AuthRefresh,
            HookEvent::RequestHeaders,
            HookEvent::ToolBefore,
            HookEvent::ToolAfter,
            HookEvent::ToolDefinition,
            HookEvent::ChatParams,
            HookEvent::PermissionAsk,
            HookEvent::ChatSystem,
            HookEvent::AgentBefore,
            HookEvent::AgentAfter,
            HookEvent::SessionStart,
            HookEvent::SessionEnd,
            HookEvent::Config,
            HookEvent::Event,
            HookEvent::ShellEnv,
            HookEvent::ChatMessagesTransform,
            HookEvent::SessionCompacting,
            HookEvent::ChatMessage,
            HookEvent::TextComplete,
            HookEvent::CommandExecuteBefore,
        ];
        for event in &events {
            let wire = event.wire_name();
            let parsed = HookEvent::from_wire_name(wire);
            assert_eq!(parsed.as_ref(), Some(event), "roundtrip failed for {wire}");
        }
    }

    #[test]
    fn unknown_wire_name() {
        assert_eq!(HookEvent::from_wire_name("nonexistent"), None);
    }

    #[test]
    fn notification_hooks() {
        assert!(HookEvent::AgentBefore.is_notification());
        assert!(HookEvent::AgentAfter.is_notification());
        assert!(HookEvent::SessionStart.is_notification());
        assert!(HookEvent::SessionEnd.is_notification());
        assert!(HookEvent::Event.is_notification());

        assert!(!HookEvent::Auth.is_notification());
        assert!(!HookEvent::ToolBefore.is_notification());
        assert!(!HookEvent::Config.is_notification());
        // New hooks are request/response (not notifications)
        assert!(!HookEvent::ToolDefinition.is_notification());
        assert!(!HookEvent::ChatParams.is_notification());
        assert!(!HookEvent::PermissionAsk.is_notification());
        assert!(!HookEvent::ChatSystem.is_notification());
        // Final 5 hooks: 2 notifications, 3 request/response
        assert!(HookEvent::ChatMessage.is_notification());
        assert!(HookEvent::TextComplete.is_notification());
        assert!(!HookEvent::ChatMessagesTransform.is_notification());
        assert!(!HookEvent::SessionCompacting.is_notification());
        assert!(!HookEvent::CommandExecuteBefore.is_notification());
    }

    #[test]
    fn new_hook_wire_names() {
        assert_eq!(HookEvent::ToolDefinition.wire_name(), "tool.definition");
        assert_eq!(HookEvent::ChatParams.wire_name(), "chat.params");
        assert_eq!(HookEvent::PermissionAsk.wire_name(), "permission.ask");
        assert_eq!(HookEvent::ChatSystem.wire_name(), "chat.system");
    }

    #[test]
    fn new_hook_from_wire_name() {
        assert_eq!(
            HookEvent::from_wire_name("tool.definition"),
            Some(HookEvent::ToolDefinition)
        );
        assert_eq!(
            HookEvent::from_wire_name("chat.params"),
            Some(HookEvent::ChatParams)
        );
        assert_eq!(
            HookEvent::from_wire_name("permission.ask"),
            Some(HookEvent::PermissionAsk)
        );
        assert_eq!(
            HookEvent::from_wire_name("chat.system"),
            Some(HookEvent::ChatSystem)
        );
    }

    #[test]
    fn dispatcher_registration() {
        let mut dispatcher = HookDispatcher::new();
        dispatcher.register("plugin-a", &["auth".to_string(), "tool.before".to_string()]);
        dispatcher.register("plugin-b", &["auth".to_string()]);

        let auth_subs = dispatcher.subscribers(&HookEvent::Auth);
        assert_eq!(auth_subs.len(), 2);
        assert!(auth_subs.contains(&"plugin-a".to_string()));
        assert!(auth_subs.contains(&"plugin-b".to_string()));

        let tool_subs = dispatcher.subscribers(&HookEvent::ToolBefore);
        assert_eq!(tool_subs.len(), 1);
        assert_eq!(tool_subs[0], "plugin-a");

        // No subscribers for unregistered hook
        let config_subs = dispatcher.subscribers(&HookEvent::Config);
        assert!(config_subs.is_empty());
    }

    #[test]
    fn dispatcher_unregister() {
        let mut dispatcher = HookDispatcher::new();
        dispatcher.register("plugin-a", &["auth".to_string()]);
        dispatcher.register("plugin-b", &["auth".to_string()]);

        dispatcher.unregister("plugin-a");

        let subs = dispatcher.subscribers(&HookEvent::Auth);
        assert_eq!(subs.len(), 1);
        assert_eq!(subs[0], "plugin-b");
    }

    #[tokio::test]
    async fn dispatch_with_no_subscribers() {
        let dispatcher = HookDispatcher::new();
        let mut plugins = HashMap::new();
        let responses = dispatcher
            .dispatch(&HookEvent::Auth, &Value::Null, &mut plugins)
            .await;
        assert!(responses.is_empty());
    }

    #[test]
    fn auth_method_serialization_roundtrip() {
        let methods = vec![
            AuthMethod::ApiKey {
                prompt: "Enter your API key".to_string(),
            },
            AuthMethod::OAuth {
                auth_url: "https://example.com/auth".to_string(),
                callback_port: 8080,
            },
            AuthMethod::DeviceCode {
                verification_url: "https://example.com/device".to_string(),
                user_code: "ABCD-1234".to_string(),
            },
        ];
        for method in &methods {
            let json = serde_json::to_string(method).unwrap();
            let parsed: AuthMethod = serde_json::from_str(&json).unwrap();
            assert_eq!(&parsed, method);
        }
    }

    #[test]
    fn auth_method_tagged_serialization() {
        let method = AuthMethod::ApiKey {
            prompt: "key please".to_string(),
        };
        let json = serde_json::to_string(&method).unwrap();
        assert!(json.contains("\"type\":\"api_key\""));
        assert!(json.contains("\"prompt\":\"key please\""));

        let method = AuthMethod::OAuth {
            auth_url: "https://x.com/auth".to_string(),
            callback_port: 9999,
        };
        let json = serde_json::to_string(&method).unwrap();
        assert!(json.contains("\"type\":\"o_auth\""));

        let method = AuthMethod::DeviceCode {
            verification_url: "https://x.com/device".to_string(),
            user_code: "XYZ".to_string(),
        };
        let json = serde_json::to_string(&method).unwrap();
        assert!(json.contains("\"type\":\"device_code\""));
    }

    #[test]
    fn auth_methods_response_serialization() {
        let response = AuthMethodsResponse {
            provider: "acme".to_string(),
            methods: vec![AuthMethod::ApiKey {
                prompt: "Enter key".to_string(),
            }],
        };
        let json = serde_json::to_string(&response).unwrap();
        let parsed: AuthMethodsResponse = serde_json::from_str(&json).unwrap();
        assert_eq!(parsed.provider, "acme");
        assert_eq!(parsed.methods.len(), 1);
    }

    #[test]
    fn auth_credentials_serialization() {
        let creds = AuthCredentials {
            provider: "copilot".to_string(),
            api_key: None,
            oauth_token: Some("gho_abc123".to_string()),
            refresh_token: Some("ghr_xyz789".to_string()),
            expires_at: Some(1700000000),
            headers: {
                let mut h = HashMap::new();
                h.insert("x-custom".to_string(), "value".to_string());
                h
            },
        };
        let json = serde_json::to_string(&creds).unwrap();
        let parsed: AuthCredentials = serde_json::from_str(&json).unwrap();
        assert_eq!(parsed.provider, "copilot");
        assert_eq!(parsed.oauth_token.as_deref(), Some("gho_abc123"));
        assert_eq!(parsed.refresh_token.as_deref(), Some("ghr_xyz789"));
        assert_eq!(parsed.expires_at, Some(1700000000));
        assert_eq!(parsed.headers.get("x-custom").unwrap(), "value");
        // api_key was None, so it should be skipped in JSON
        assert!(!json.contains("\"api_key\""));
    }

    #[test]
    fn auth_credentials_minimal() {
        // Minimal credentials with just an API key
        let json = r#"{"provider":"test","api_key":"sk-123"}"#;
        let parsed: AuthCredentials = serde_json::from_str(json).unwrap();
        assert_eq!(parsed.provider, "test");
        assert_eq!(parsed.api_key.as_deref(), Some("sk-123"));
        assert!(parsed.oauth_token.is_none());
        assert!(parsed.headers.is_empty());
    }

    #[tokio::test]
    async fn dispatch_with_missing_process() {
        let mut dispatcher = HookDispatcher::new();
        dispatcher.register("ghost-plugin", &["auth".to_string()]);

        let mut plugins = HashMap::new();
        let responses = dispatcher
            .dispatch(&HookEvent::Auth, &Value::Null, &mut plugins)
            .await;
        // Missing process is skipped, no response
        assert!(responses.is_empty());
    }
}
