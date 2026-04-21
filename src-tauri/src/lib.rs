mod app_state;
mod bridge;
mod commands;
mod events;
mod logging;
mod pty;

use app_state::AppState;
use bridge::DesktopBridge;
use commands::{
    agent_run, agent_stream, allow_project_path, append_log, cancel_agent, check_desktop_update,
    cleanup_old_logs, clear_message_queue, compact_context, compute_fuzzy_replace, compute_grep,
    compute_repo_map, create_session, delete_provider_auth, delete_session, disable_mcp_server,
    discover_cli_agents, edit_and_resend, enable_mcp_server, evaluate_permission,
    execute_browser_tool, execute_git_tool, execute_tool, extensions_register_native,
    extensions_register_wasm, follow_up_agent, get_agent_status, get_config, get_current_model,
    get_cwd, get_env_var, get_feature_flags, get_global_plugins_dir, get_message_queue,
    get_permission_level, get_plugins_state, get_state_logs_dir, greet, install_desktop_update,
    install_plugin, list_agent_tools, list_mcp_servers, list_models, list_plugin_mounts,
    list_providers, list_sessions, list_tools, load_credentials, load_session, memory_recall,
    memory_recent, memory_remember, memory_search, oauth_copilot_device_poll,
    oauth_copilot_device_start, oauth_listen, plugin_host_invoke, post_complete_agent, pty_kill,
    pty_resize, pty_spawn, pty_write, read_latest_logs, reflection_reflect_and_fix,
    regenerate_response, reload_mcp_servers, rename_session, resolve_approval, resolve_plan,
    resolve_question, retry_last_message, sandbox_apply_landlock, search_sessions,
    set_active_session, set_cwd, set_permission_level, set_plugin_enabled, set_plugins_state,
    steer_agent, store_provider_auth, submit_goal, switch_model, sync_credentials,
    toggle_permission_level, undo_last_edit, uninstall_plugin, update_feature_flags,
    update_llm_config, validation_validate_edit, validation_validate_with_retry,
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
        .plugin(tauri_plugin_updater::Builder::new().build())
        .manage(PtyManager::new())
        .setup(|app| {
            let app_data_dir = app
                .path()
                .app_data_dir()
                .map_err(|error| format!("failed to resolve app data directory: {error}"))?;

            match logging::init_backend_logging(&app_data_dir) {
                Ok(backend_logging) => {
                    app.manage(backend_logging);
                }
                Err(error) => {
                    eprintln!(
                        "[ava-desktop] backend logging initialization failed; continuing startup: {error}"
                    );
                }
            }

            // Legacy AppState (used by existing commands)
            let state = tauri::async_runtime::block_on(AppState::new(app_data_dir.clone()))
                .map_err(|error| format!("failed to initialize app state: {error}"))?;
            app.manage(state);

            // New DesktopBridge — wraps the real AgentStack.
            // Use AVA's canonical XDG data dir so credentials, config, sessions,
            // and memory stay aligned with the CLI and web surfaces.
            let ava_home = ava_config::data_dir().unwrap_or(app_data_dir.clone());
            let bridge = tauri::async_runtime::block_on(DesktopBridge::init(
                ava_home,
                app.handle().clone(),
            ))
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
            get_global_plugins_dir,
            get_state_logs_dir,
            read_latest_logs,
            get_cwd,
            set_cwd,
            get_plugins_state,
            list_plugin_mounts,
            set_plugins_state,
            install_plugin,
            uninstall_plugin,
            set_plugin_enabled,
            plugin_host_invoke,
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
            resolve_plan,
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
            set_active_session,
            list_models,
            get_current_model,
            switch_model,
            list_providers,
            discover_cli_agents,
            get_config,
            store_provider_auth,
            delete_provider_auth,
            sync_credentials,
            load_credentials,
            update_llm_config,
            update_feature_flags,
            get_feature_flags,
            list_agent_tools,
            list_mcp_servers,
            enable_mcp_server,
            disable_mcp_server,
            reload_mcp_servers,
            get_permission_level,
            set_permission_level,
            toggle_permission_level,
            compact_context,
            check_desktop_update,
            install_desktop_update,
        ])
        .run(context)
        .expect("error while running tauri application");
}
