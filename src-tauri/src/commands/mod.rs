mod agent_integration;
mod compute_fuzzy;
mod compute_grep;
mod compute_repo_map;
mod dev_log;
mod env;
mod extensions;
mod fs_scope;
mod greet;
mod reflection;
mod oauth;
mod plugin_state;
mod validation;
mod memory;
mod permissions;
mod sandbox_landlock;
pub mod pty;
mod tool_browser;
mod tool_git;

// --- New: real backend bridge commands ---
pub mod agent_commands;
pub mod session_commands;
pub mod model_commands;
pub mod provider_commands;
pub mod config_commands;
pub mod tool_commands;
pub mod mcp_commands;
pub mod permission_commands;
pub mod context_commands;

pub use agent_integration::{
    agent_run, agent_stream, execute_tool, list_tools, ToolInfo,
};
pub use compute_fuzzy::compute_fuzzy_replace;
pub use compute_grep::compute_grep;
pub use compute_repo_map::compute_repo_map;
pub use dev_log::{append_log, cleanup_old_logs, get_cwd, read_latest_logs};
pub use env::get_env_var;
pub use extensions::{extensions_register_native, extensions_register_wasm};
pub use fs_scope::allow_project_path;
pub use greet::greet;
pub use memory::{memory_recall, memory_recent, memory_remember, memory_search};
pub use oauth::{oauth_copilot_device_poll, oauth_copilot_device_start, oauth_listen};
pub use permissions::evaluate_permission;
pub use sandbox_landlock::sandbox_apply_landlock;
pub use reflection::reflection_reflect_and_fix;
pub use plugin_state::{
    get_plugins_state, install_plugin, set_plugin_enabled, set_plugins_state, uninstall_plugin,
};
pub use pty::{pty_kill, pty_resize, pty_spawn, pty_write};
pub use tool_browser::execute_browser_tool;
pub use tool_git::execute_git_tool;
pub use validation::{validation_validate_edit, validation_validate_with_retry};

// --- New: re-exports for bridge commands ---
pub use agent_commands::{
    submit_goal, cancel_agent, get_agent_status,
    resolve_approval, resolve_question, resolve_plan,
    steer_agent, follow_up_agent, post_complete_agent, get_message_queue, clear_message_queue,
    retry_last_message, edit_and_resend, regenerate_response, undo_last_edit,
    start_praxis, get_praxis_status, cancel_praxis, steer_lead,
};
pub use session_commands::{list_sessions, load_session, create_session, delete_session, rename_session, search_sessions};
pub use model_commands::{list_models, get_current_model, switch_model};
pub use provider_commands::list_providers;
pub use config_commands::get_config;
pub use tool_commands::list_agent_tools;
pub use mcp_commands::{list_mcp_servers, reload_mcp_servers};
pub use permission_commands::{get_permission_level, set_permission_level, toggle_permission_level};
pub use context_commands::compact_context;
