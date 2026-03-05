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
pub mod pty;
mod tool_browser;
mod tool_git;

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
pub use reflection::reflection_reflect_and_fix;
pub use plugin_state::{
    get_plugins_state, install_plugin, set_plugin_enabled, set_plugins_state, uninstall_plugin,
};
pub use pty::{pty_kill, pty_resize, pty_spawn, pty_write};
pub use tool_browser::execute_browser_tool;
pub use tool_git::execute_git_tool;
pub use validation::{validation_validate_edit, validation_validate_with_retry};
