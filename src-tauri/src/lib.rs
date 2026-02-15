mod commands;

use commands::{
    allow_project_path, append_log, cleanup_old_logs, get_cwd, get_env_var, get_plugins_state,
    greet, oauth_copilot_device_poll, oauth_copilot_device_start, oauth_listen,
    set_plugins_state,
};

#[cfg_attr(mobile, tauri::mobile_entry_point)]
pub fn run() {
    tauri::Builder::default()
        .plugin(tauri_plugin_opener::init())
        .plugin(tauri_plugin_sql::Builder::new().build())
        .plugin(tauri_plugin_shell::init())
        .plugin(tauri_plugin_fs::init())
        .plugin(tauri_plugin_window_state::Builder::new().build())
        .invoke_handler(tauri::generate_handler![
            greet,
            oauth_listen,
            oauth_copilot_device_start,
            oauth_copilot_device_poll,
            get_env_var,
            allow_project_path,
            append_log,
            cleanup_old_logs,
            get_cwd,
            get_plugins_state,
            set_plugins_state
        ])
        .run(tauri::generate_context!())
        .expect("error while running tauri application");
}
