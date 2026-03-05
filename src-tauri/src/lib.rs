mod app_state;
mod commands;
mod events;
mod pty;

use app_state::AppState;
use commands::{
    agent_run, agent_stream, allow_project_path, append_log, cleanup_old_logs,
    compute_fuzzy_replace, compute_grep, evaluate_permission, execute_browser_tool,
    execute_git_tool, execute_tool, extensions_register_native, extensions_register_wasm,
    get_cwd, get_env_var, get_plugins_state, greet, install_plugin, list_tools, memory_recall,
    memory_recent, memory_remember, memory_search, oauth_copilot_device_poll,
    oauth_copilot_device_start, oauth_listen, pty_kill, pty_resize, pty_spawn, pty_write,
    reflection_reflect_and_fix, set_plugin_enabled, set_plugins_state, uninstall_plugin,
    validation_validate_edit, validation_validate_with_retry,
};
use pty::PtyManager;
use tauri::Manager;

#[cfg_attr(mobile, tauri::mobile_entry_point)]
pub fn run() {
    let context = tauri::generate_context!();

    tauri::Builder::default()
        .plugin(tauri_plugin_opener::init())
        .plugin(tauri_plugin_dialog::init())
        .plugin(tauri_plugin_sql::Builder::new().build())
        .plugin(tauri_plugin_shell::init())
        .plugin(tauri_plugin_fs::init())
        .plugin(tauri_plugin_window_state::Builder::new().build())
        .manage(PtyManager::new())
        .setup(|app| {
            let app_data_dir = app
                .path()
                .app_data_dir()
                .map_err(|error| format!("failed to resolve app data directory: {error}"))?;

            let state = tauri::async_runtime::block_on(AppState::new(app_data_dir))
                .map_err(|error| format!("failed to initialize app state: {error}"))?;

            app.manage(state);
            Ok(())
        })
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
            set_plugins_state,
            install_plugin,
            uninstall_plugin,
            set_plugin_enabled,
            pty_spawn,
            pty_write,
            pty_resize,
            pty_kill,
            compute_grep,
            compute_fuzzy_replace,
            execute_tool,
            agent_run,
            agent_stream,
            list_tools,
            execute_git_tool,
            execute_browser_tool,
            memory_remember,
            memory_recall,
            memory_search,
            memory_recent,
            evaluate_permission,
            extensions_register_native,
            extensions_register_wasm,
            validation_validate_edit,
            validation_validate_with_retry,
            reflection_reflect_and_fix
        ])
        .run(context)
        .expect("error while running tauri application");
}
