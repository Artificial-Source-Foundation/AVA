use tauri_plugin_fs::FsExt;

/// Expand the Tauri FS scope at runtime to include a project directory.
/// Called when the user opens or switches to a project so that the frontend
/// (file browser, code editor) and core tools can access project files.
#[tauri::command]
pub fn allow_project_path(app: tauri::AppHandle, path: String) -> Result<(), String> {
    app.fs_scope()
        .allow_directory(std::path::Path::new(&path), true)
        .map_err(|e| e.to_string())
}
