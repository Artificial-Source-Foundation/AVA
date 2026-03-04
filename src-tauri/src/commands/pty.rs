use crate::pty::PtyManager;
use tauri::{AppHandle, State};

#[tauri::command]
pub async fn pty_spawn(
    id: String,
    cols: u16,
    rows: u16,
    cwd: Option<String>,
    state: State<'_, PtyManager>,
    app: AppHandle,
) -> Result<(), String> {
    state.spawn(id, cols, rows, cwd, app)
}

#[tauri::command]
pub async fn pty_write(
    id: String,
    data: String,
    state: State<'_, PtyManager>,
) -> Result<(), String> {
    state.write(&id, &data)
}

#[tauri::command]
pub async fn pty_resize(
    id: String,
    cols: u16,
    rows: u16,
    state: State<'_, PtyManager>,
) -> Result<(), String> {
    state.resize(&id, cols, rows)
}

#[tauri::command]
pub async fn pty_kill(id: String, state: State<'_, PtyManager>) -> Result<(), String> {
    state.kill(&id)
}
