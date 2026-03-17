//! Hook event types and dispatch routing.

use serde::{Deserialize, Serialize};
use serde_json::Value;
use std::collections::HashMap;
use std::time::Duration;
use tracing::{debug, warn};

use crate::runtime::PluginProcess;

/// Hook events that plugins can subscribe to.
#[derive(Debug, Clone, PartialEq, Eq, Hash, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum HookEvent {
    /// Provide credentials for a provider.
    Auth,
    /// Refresh expired tokens.
    AuthRefresh,
    /// Inject headers into LLM API calls.
    RequestHeaders,
    /// Intercept tool call before execution.
    ToolBefore,
    /// Intercept tool result after execution.
    ToolAfter,
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
}

impl HookEvent {
    /// The wire name used in `plugin.toml` subscriptions and JSON-RPC methods.
    pub fn wire_name(&self) -> &'static str {
        match self {
            Self::Auth => "auth",
            Self::AuthRefresh => "auth.refresh",
            Self::RequestHeaders => "request.headers",
            Self::ToolBefore => "tool.before",
            Self::ToolAfter => "tool.after",
            Self::AgentBefore => "agent.before",
            Self::AgentAfter => "agent.after",
            Self::SessionStart => "session.start",
            Self::SessionEnd => "session.end",
            Self::Config => "config",
            Self::Event => "event",
            Self::ShellEnv => "shell.env",
        }
    }

    /// Parse a wire name back to a HookEvent.
    pub fn from_wire_name(name: &str) -> Option<Self> {
        match name {
            "auth" => Some(Self::Auth),
            "auth.refresh" => Some(Self::AuthRefresh),
            "request.headers" => Some(Self::RequestHeaders),
            "tool.before" => Some(Self::ToolBefore),
            "tool.after" => Some(Self::ToolAfter),
            "agent.before" => Some(Self::AgentBefore),
            "agent.after" => Some(Self::AgentAfter),
            "session.start" => Some(Self::SessionStart),
            "session.end" => Some(Self::SessionEnd),
            "config" => Some(Self::Config),
            "event" => Some(Self::Event),
            "shell.env" => Some(Self::ShellEnv),
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
            HookEvent::AuthRefresh,
            HookEvent::RequestHeaders,
            HookEvent::ToolBefore,
            HookEvent::ToolAfter,
            HookEvent::AgentBefore,
            HookEvent::AgentAfter,
            HookEvent::SessionStart,
            HookEvent::SessionEnd,
            HookEvent::Config,
            HookEvent::Event,
            HookEvent::ShellEnv,
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
