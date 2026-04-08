mod agent_integration;
mod compute_fuzzy;
mod compute_grep;
mod compute_repo_map;
mod dev_log;
mod env;
mod extensions;
mod fs_scope;
mod greet;
mod memory;
mod oauth;
mod permissions;
mod plugin_host;
mod plugin_state;
pub mod pty;
mod reflection;
mod sandbox_landlock;
mod tool_browser;
mod tool_git;
mod validation;

// --- New: real backend bridge commands ---
pub mod agent_commands;
pub mod config_commands;
pub mod context_commands;
mod helpers;
pub mod mcp_commands;
pub mod model_commands;
pub mod permission_commands;
pub mod provider_commands;
pub mod session_commands;
pub mod tool_commands;
pub mod updater_commands;
pub mod usage_commands;

pub use agent_integration::{agent_run, agent_stream, execute_tool, list_tools, ToolInfo};
pub use compute_fuzzy::compute_fuzzy_replace;
pub use compute_grep::compute_grep;
pub use compute_repo_map::compute_repo_map;
pub use dev_log::{append_log, cleanup_old_logs, get_cwd, read_latest_logs, set_cwd};
pub use env::get_env_var;
pub use extensions::{extensions_register_native, extensions_register_wasm};
pub use fs_scope::allow_project_path;
pub use greet::greet;
pub use memory::{memory_recall, memory_recent, memory_remember, memory_search};
pub use oauth::{oauth_copilot_device_poll, oauth_copilot_device_start, oauth_listen};
pub use permissions::evaluate_permission;
pub use plugin_host::{list_plugin_mounts, plugin_host_invoke};
pub use plugin_state::{
    get_plugins_state, install_plugin, set_plugin_enabled, set_plugins_state, uninstall_plugin,
};
pub use pty::{pty_kill, pty_resize, pty_spawn, pty_write};
pub use reflection::reflection_reflect_and_fix;
pub use sandbox_landlock::sandbox_apply_landlock;
pub use tool_browser::execute_browser_tool;
pub use tool_git::execute_git_tool;
pub use validation::{validation_validate_edit, validation_validate_with_retry};

// --- New: re-exports for bridge commands ---
pub use agent_commands::{
    cancel_agent, clear_message_queue, edit_and_resend, follow_up_agent, get_agent_status,
    get_message_queue, post_complete_agent, regenerate_response, resolve_approval, resolve_plan,
    resolve_question, retry_last_message, steer_agent, submit_goal, undo_last_edit,
};
pub use config_commands::{
    get_config, get_feature_flags, load_credentials, sync_credentials, update_feature_flags,
    update_llm_config,
};
pub use context_commands::compact_context;
pub use mcp_commands::{list_mcp_servers, reload_mcp_servers};
pub use model_commands::{get_current_model, list_models, switch_model};
pub use permission_commands::{
    get_permission_level, set_permission_level, toggle_permission_level,
};
pub use provider_commands::{discover_cli_agents, list_providers};
pub use session_commands::{
    create_session, delete_session, list_sessions, load_session, rename_session, search_sessions,
};
pub use tool_commands::list_agent_tools;
pub use updater_commands::{check_desktop_update, install_desktop_update};
pub use usage_commands::get_subscription_usage;
