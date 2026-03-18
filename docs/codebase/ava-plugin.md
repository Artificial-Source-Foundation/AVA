# ava-plugin

> Power plugin system — external process plugins via JSON-RPC over stdio

## Public API

| Type/Function | Description |
|--------------|-------------|
| `PluginManager` | Top-level plugin lifecycle manager |
| `PluginInfo` | Summary info for a loaded plugin (name, version, status, hooks) |
| `PluginStatus` | Enum: Running, Stopped, Failed(String) |
| `PluginProcess` | Running child process communicating via JSON-RPC |
| `PluginManifest` | Parsed `plugin.toml` manifest with plugin, runtime, hooks sections |
| `PluginMeta` | Plugin identity: name, version, description, author |
| `RuntimeConfig` | Spawn configuration: command, args, env |
| `HookSubscriptions` | List of hook events the plugin subscribes to |
| `HookDispatcher` | Routes hook calls to subscribed plugins with timeout handling |
| `HookEvent` | Enum of 14 hook events (Auth, ToolBefore, SessionStart, etc.) |
| `HookRequest` / `HookResponse` | Hook call request/response types |
| `AuthMethod` | Enum: ApiKey, OAuth, DeviceCode authentication methods |
| `AuthMethodsResponse` | Plugin response listing supported auth methods |
| `AuthCredentials` | Credentials: api_key, oauth_token, refresh_token, headers |
| `discover_plugins()` | Scan directories for plugins with `plugin.toml` |
| `default_plugin_dirs()` | Returns `~/.ava/plugins/` and `.ava/plugins/` |
| `load_manifest()` | Parse `plugin.toml` from filesystem |

## Module Map

| File | Purpose |
|------|---------|
| `lib.rs` | Exports discovery, hooks, manager, manifest, runtime modules |
| `discovery.rs` | Scan plugin directories for `plugin.toml` files |
| `hooks.rs` | Hook event types, auth types, HookDispatcher for routing |
| `manager.rs` | PluginManager: load, spawn, trigger hooks, auth sub-protocol |
| `manifest.rs` | TOML parsing for plugin.toml manifests |
| `runtime.rs` | PluginProcess: spawn, JSON-RPC framing, requests/notifications |

## Dependencies

Uses: ava-types

Used by: ava-agent, ava-tui

## Key Patterns

- **JSON-RPC over stdio**: Content-Length framing like MCP; 10MB message limit
- **Plugin discovery**: Scans subdirectories for `plugin.toml` in `~/.ava/plugins/` and `.ava/plugins/`
- **Lifecycle**: `PluginManager::load_plugins()` spawns, initializes, registers hooks
- **Hook subscription**: Plugins declare hooks in manifest; `HookDispatcher` routes by wire name
- **Timeout handling**: 5 second default timeout for request/response hooks; notifications are fire-and-forget
- **Auth sub-protocol**: Three-phase auth (methods, authorize, refresh) via hook events
- **Env sanitization**: Strips sensitive env vars (API keys, tokens) before spawning plugins
- **Error resilience**: Failed plugins don't prevent others from loading; recorded with Failed status
- **Graceful shutdown**: Sends shutdown notification, waits 2s, then kills if needed
