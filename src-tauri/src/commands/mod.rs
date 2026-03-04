mod compute_fuzzy;
mod compute_grep;
mod dev_log;
mod env;
mod fs_scope;
mod greet;
mod oauth;
mod plugin_state;
pub mod pty;

pub use compute_fuzzy::compute_fuzzy_replace;
pub use compute_grep::compute_grep;
pub use dev_log::{append_log, cleanup_old_logs, get_cwd};
pub use env::get_env_var;
pub use fs_scope::allow_project_path;
pub use greet::greet;
pub use oauth::{oauth_copilot_device_poll, oauth_copilot_device_start, oauth_listen};
pub use plugin_state::{
    get_plugins_state, install_plugin, set_plugin_enabled, set_plugins_state, uninstall_plugin,
};
pub use pty::{pty_kill, pty_resize, pty_spawn, pty_write};
