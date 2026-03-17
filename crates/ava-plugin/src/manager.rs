//! Top-level plugin manager — owns all plugin runtimes and orchestrates lifecycle.

use ava_types::Result;
use serde_json::Value;
use std::collections::HashMap;
use std::path::PathBuf;
use tracing::{debug, info, warn};

use crate::discovery::discover_plugins;
use crate::hooks::{AuthCredentials, AuthMethodsResponse, HookDispatcher, HookEvent, HookResponse};
use crate::runtime::PluginProcess;

/// Status of a managed plugin.
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum PluginStatus {
    /// Plugin process is running and ready.
    Running,
    /// Plugin process has exited or failed.
    Stopped,
    /// Plugin failed to start.
    Failed(String),
}

/// Summary info for a loaded plugin.
#[derive(Debug, Clone)]
pub struct PluginInfo {
    /// Plugin name from manifest.
    pub name: String,
    /// Plugin version from manifest.
    pub version: String,
    /// Current status.
    pub status: PluginStatus,
    /// Hook subscriptions declared in manifest.
    pub hooks: Vec<String>,
}

/// Manages the lifecycle of all power plugins.
pub struct PluginManager {
    /// Running plugin processes, keyed by plugin name.
    processes: HashMap<String, PluginProcess>,
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
                    // Send initialize
                    let init_params = serde_json::json!({
                        "plugin": name,
                        "version": version,
                    });
                    match process.initialize(init_params).await {
                        Ok(_caps) => {
                            debug!(plugin = %name, "plugin initialized successfully");
                        }
                        Err(e) => {
                            warn!(plugin = %name, error = %e, "plugin initialization failed, continuing anyway");
                        }
                    }

                    // Register hook subscriptions
                    self.dispatcher.register(&name, &hooks);
                    self.processes.insert(name.clone(), process);
                    self.plugin_info.insert(
                        name.clone(),
                        PluginInfo {
                            name,
                            version,
                            status: PluginStatus::Running,
                            hooks,
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
            .dispatch(&event, &params, &mut self.processes)
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
            .dispatch(&HookEvent::AuthMethods, &params, &mut self.processes)
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
            .dispatch(&HookEvent::AuthAuthorize, &params, &mut self.processes)
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
            .dispatch(&HookEvent::AuthRefresh, &params, &mut self.processes)
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

    /// Gracefully shut down all running plugin processes.
    pub async fn shutdown_all(&mut self) {
        info!(count = self.processes.len(), "shutting down all plugins");
        // Collect names to avoid borrow issues
        let names: Vec<String> = self.processes.keys().cloned().collect();
        for name in &names {
            if let Some(mut process) = self.processes.remove(name) {
                debug!(plugin = %name, "shutting down plugin");
                process.shutdown().await;
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
