# Tauri Commands

> Rust commands exposed to the SolidJS frontend via Tauri IPC

## Command Organization

| Module | File | Commands |
|--------|------|----------|
| Agent | `agent_commands.rs` | `submit_goal`, `cancel_agent`, `get_agent_status`, `resolve_approval`, `resolve_question`, `steer_agent`, `follow_up_agent`, `post_complete_agent`, `get_message_queue`, `clear_message_queue`, `retry_last_message`, `edit_and_resend`, `regenerate_response`, `undo_last_edit` |
| Session | `session_commands.rs` | `list_sessions`, `load_session`, `create_session`, `delete_session`, `rename_session`, `search_sessions` |
| Model | `model_commands.rs` | `list_models`, `get_current_model`, `switch_model` |
| Provider | `provider_commands.rs` | `list_providers` |
| Config | `config_commands.rs` | `get_config` |
| Tool | `tool_commands.rs` | `list_agent_tools` |
| MCP | `mcp_commands.rs` | `list_mcp_servers`, `reload_mcp_servers` |
| Permission | `permission_commands.rs` | `get_permission_level`, `set_permission_level`, `toggle_permission_level` |
| Context | `context_commands.rs` | `compact_context` |
| Memory | `memory.rs` | `memory_remember`, `memory_recall`, `memory_search`, `memory_recent` |
| OAuth | `oauth.rs` | `oauth_listen`, `oauth_copilot_device_start`, `oauth_copilot_device_poll` |
| PTY | `pty.rs` | `pty_spawn`, `pty_write`, `pty_resize`, `pty_kill` |
| Compute | `compute_*.rs` | `compute_repo_map`, `compute_grep`, `compute_fuzzy_replace` |
| Agent Integration | `agent_integration.rs` | `agent_run`, `agent_stream`, `execute_tool`, `list_tools` |
| Tool Execution | `tool_git.rs`, `tool_browser.rs` | `execute_git_tool`, `execute_browser_tool` |
| Extensions | `extensions.rs` | `extensions_register_native`, `extensions_register_wasm` |
| Plugins | `plugin_state.rs` | `get_plugins_state`, `set_plugins_state`, `install_plugin`, `uninstall_plugin`, `set_plugin_enabled` |
| Permissions | `permissions.rs` | `evaluate_permission` |
| Sandbox | `sandbox_landlock.rs` | `sandbox_apply_landlock` |
| Validation | `validation.rs` | `validation_validate_edit`, `validation_validate_with_retry` |
| Reflection | `reflection.rs` | `reflection_reflect_and_fix` |
| Environment | `env.rs`, `fs_scope.rs` | `get_env_var`, `allow_project_path` |
| Dev Logs | `dev_log.rs` | `append_log`, `read_latest_logs`, `cleanup_old_logs`, `get_cwd` |
| Test | `greet.rs` | `greet` |

## Key Commands by Category

### Agent Operations

| Command | Purpose | Frontend Usage |
|---------|---------|----------------|
| `submit_goal` | Start agent with a goal | Main entry point for user queries |
| `cancel_agent` | Cancel running agent | Stop button handler |
| `get_agent_status` | Check if agent is running | UI state indicator |
| `resolve_approval` | Approve/deny tool request | Approval dock callback |
| `resolve_question` | Answer agent question | Question dialog callback |
| `steer_agent` | Inject Tier 1 message | Immediate steering input |
| `follow_up_agent` | Queue Tier 2 message | Follow-up during execution |
| `post_complete_agent` | Queue Tier 3 message | Post-completion follow-up |
| `get_message_queue` | Check queue state | Queue status display |
| `clear_message_queue` | Clear queued messages | Cancel/clear button |
| `retry_last_message` | Retry last user message | Retry button |
| `edit_and_resend` | Edit message and retry | Edit mode handler |
| `regenerate_response` | Regenerate assistant response | Regenerate button |
| `undo_last_edit` | Undo last file edit | Undo button |
| `agent_run` | Legacy agent execution | Deprecated |
| `agent_stream` | Legacy streaming agent | Deprecated |

### Session Management

| Command | Purpose | Frontend Usage |
|---------|---------|----------------|
| `list_sessions` | Get recent sessions | Session list sidebar |
| `load_session` | Load full session | Open session in chat |
| `create_session` | Create new session | New chat button |
| `delete_session` | Delete session | Delete button in list |
| `rename_session` | Rename session | Inline edit in list |
| `search_sessions` | FTS search sessions | Search bar |

### Models & Providers

| Command | Purpose | Frontend Usage |
|---------|---------|----------------|
| `list_models` | Get all models from registry | Model picker |
| `get_current_model` | Get active provider/model | Status bar display |
| `switch_model` | Change active model | Model selector action |
| `list_providers` | Get configured providers | Provider tabs |

### Tools

| Command | Purpose | Frontend Usage |
|---------|---------|----------------|
| `list_tools` | Legacy tool listing | Deprecated |
| `list_agent_tools` | List registered tools | Tool inspector |
| `execute_tool` | Execute single tool | Tool testing |
| `execute_git_tool` | Run git command | Git integration |
| `execute_browser_tool` | Browser automation | MCP browser (requires server) |

### MCP Servers

| Command | Purpose | Frontend Usage |
|---------|---------|----------------|
| `list_mcp_servers` | List MCP servers | Server status panel |
| `reload_mcp_servers` | Reload MCP config | Refresh button |

### Configuration

| Command | Purpose | Frontend Usage |
|---------|---------|----------------|
| `get_config` | Get full config JSON | Settings panel |

### Permissions

| Command | Purpose | Frontend Usage |
|---------|---------|----------------|
| `get_permission_level` | Get auto-approve status | Permission indicator |
| `set_permission_level` | Set permission level | Toggle switch |
| `toggle_permission_level` | Toggle permission mode | Quick toggle button |
| `evaluate_permission` | Check permission rules | Tool authorization |

### Context

| Command | Purpose | Frontend Usage |
|---------|---------|----------------|
| `compact_context` | Compact conversation | `/compact` command |

### Memory

| Command | Purpose | Frontend Usage |
|---------|---------|----------------|
| `memory_remember` | Store key-value | Save to memory |
| `memory_recall` | Retrieve by key | Recall memory |
| `memory_search` | Full-text search | Memory search |
| `memory_recent` | Get recent entries | Memory history |

### Compute

| Command | Purpose | Frontend Usage |
|---------|---------|----------------|
| `compute_repo_map` | Rank files by relevance | Repo map generator |
| `compute_grep` | Search files with regex | Code search |
| `compute_fuzzy_replace` | Fuzzy text replacement | Smart diff apply |

### Extensions & Plugins

| Command | Purpose | Frontend Usage |
|---------|---------|----------------|
| `extensions_register_native` | Register native extension | Extension installer |
| `extensions_register_wasm` | Register WASM extension | Extension installer |
| `get_plugins_state` | Get plugin states | Plugin manager |
| `set_plugins_state` | Bulk update states | Save preferences |
| `install_plugin` | Mark plugin installed | Install button |
| `uninstall_plugin` | Remove plugin | Uninstall button |
| `set_plugin_enabled` | Toggle plugin | Enable checkbox |

### Environment

| Command | Purpose | Frontend Usage |
|---------|---------|----------------|
| `get_env_var` | Read allowed env var | API key detection |
| `get_cwd` | Get working directory | Project detection |
| `allow_project_path` | Expand FS scope | Project open handler |

### Sandbox

| Command | Purpose | Frontend Usage |
|---------|---------|----------------|
| `sandbox_apply_landlock` | Apply Linux Landlock | Security sandbox |

### Validation

| Command | Purpose | Frontend Usage |
|---------|---------|----------------|
| `validation_validate_edit` | Validate code edit | Pre-save check |
| `validation_validate_with_retry` | Validate with fix attempts | Auto-fix loop |

### Reflection

| Command | Purpose | Frontend Usage |
|---------|---------|----------------|
| `reflection_reflect_and_fix` | Reflect on errors | Error recovery |

### OAuth

| Command | Purpose | Frontend Usage |
|---------|---------|----------------|
| `oauth_listen` | Listen for callback | OAuth flow |
| `oauth_copilot_device_start` | Start device auth | Copilot login |
| `oauth_copilot_device_poll` | Poll for token | Copilot auth |

### PTY

| Command | Purpose | Frontend Usage |
|---------|---------|----------------|
| `pty_spawn` | Spawn terminal | Terminal panel |
| `pty_write` | Send input | Terminal keyboard |
| `pty_resize` | Resize terminal | Terminal resize |
| `pty_kill` | Kill terminal | Close terminal |

### Dev Logs

| Command | Purpose | Frontend Usage |
|---------|---------|----------------|
| `append_log` | Write log entry | Logging |
| `read_latest_logs` | Read log tail | Log viewer |
| `cleanup_old_logs` | Delete old logs | Maintenance |

## Adding New Commands

1. **Create function** in appropriate `commands/*.rs` file
2. **Add to `mod.rs`** in `pub use` statement
3. **Register in `lib.rs`** in `generate_handler!` macro
4. **Call from frontend** via `invoke('command_name', args)`

## Error Handling

- Rust commands return `Result<T, String>`
- Errors serialize as strings to TypeScript
- Frontend handles with `try/catch` around `invoke()` calls
- Use `?` operator in Rust for automatic conversion via `ToString`
