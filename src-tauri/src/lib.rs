mod app_state;
mod bridge;
mod commands;
mod events;
mod pty;

use app_state::AppState;
use bridge::DesktopBridge;
use commands::{
    // Existing commands
    agent_run, agent_stream, allow_project_path, append_log, cleanup_old_logs,
    compute_fuzzy_replace, compute_grep, compute_repo_map, evaluate_permission, execute_browser_tool,
    execute_git_tool, execute_tool, extensions_register_native, extensions_register_wasm,
    get_cwd, get_env_var, get_plugins_state, greet, install_plugin, list_tools, memory_recall,
    memory_recent, memory_remember, memory_search, oauth_copilot_device_poll,
    oauth_copilot_device_start, oauth_listen, pty_kill, pty_resize, pty_spawn, pty_write,
    read_latest_logs, reflection_reflect_and_fix, sandbox_apply_landlock, set_plugin_enabled,
    set_plugins_state, uninstall_plugin, validation_validate_edit, validation_validate_with_retry,
    // New bridge commands
    submit_goal, cancel_agent, get_agent_status,
    resolve_approval, resolve_question,
    steer_agent, follow_up_agent, post_complete_agent, get_message_queue, clear_message_queue,
    retry_last_message, edit_and_resend, regenerate_response, undo_last_edit,
    list_sessions, load_session, create_session, delete_session, rename_session, search_sessions,
    list_models, get_current_model, switch_model,
    list_providers,
    get_config,
    list_agent_tools,
    list_mcp_servers, reload_mcp_servers,
    get_permission_level, set_permission_level, toggle_permission_level,
    compact_context,
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

            // Legacy AppState (used by existing commands)
            let state = tauri::async_runtime::block_on(AppState::new(app_data_dir.clone()))
                .map_err(|error| format!("failed to initialize app state: {error}"))?;
            app.manage(state);

            // New DesktopBridge — wraps the real AgentStack.
            // Use ~/.ava as the data dir (same as the TUI) so credentials, config,
            // sessions, and memory are shared between CLI and desktop.
            let ava_home = std::env::var("HOME")
                .map(std::path::PathBuf::from)
                .unwrap_or_else(|_| app_data_dir.clone())
                .join(".ava");
            let bridge = tauri::async_runtime::block_on(DesktopBridge::init(ava_home))
                .map_err(|error| format!("failed to initialize desktop bridge: {error}"))?;
            app.manage(bridge);

            Ok(())
        })
        .invoke_handler(tauri::generate_handler![
            // --- Existing commands (unchanged) ---
            greet,
            oauth_listen,
            oauth_copilot_device_start,
            oauth_copilot_device_poll,
            get_env_var,
            allow_project_path,
            append_log,
            cleanup_old_logs,
            read_latest_logs,
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
            compute_repo_map,
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
            sandbox_apply_landlock,
            extensions_register_native,
            extensions_register_wasm,
            validation_validate_edit,
            validation_validate_with_retry,
            reflection_reflect_and_fix,
            // --- New: real backend bridge ---
            submit_goal,
            cancel_agent,
            get_agent_status,
            resolve_approval,
            resolve_question,
            steer_agent,
            follow_up_agent,
            post_complete_agent,
            get_message_queue,
            clear_message_queue,
            retry_last_message,
            edit_and_resend,
            regenerate_response,
            undo_last_edit,
            list_sessions,
            load_session,
            create_session,
            delete_session,
            rename_session,
            search_sessions,
            list_models,
            get_current_model,
            switch_model,
            list_providers,
            get_config,
            list_agent_tools,
            list_mcp_servers,
            reload_mcp_servers,
            get_permission_level,
            set_permission_level,
            toggle_permission_level,
            compact_context,
        ])
        .run(context)
        .expect("error while running tauri application");
}
