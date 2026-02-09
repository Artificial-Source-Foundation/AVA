use std::fs;
use std::path::Path;

/// Get the current working directory (for locating project-local logs).
#[tauri::command]
pub fn get_cwd() -> Result<String, String> {
    std::env::current_dir()
        .map(|p| p.to_string_lossy().to_string())
        .map_err(|e| e.to_string())
}

/// Append text to a log file, creating parent directories if needed.
#[tauri::command]
pub fn append_log(path: String, content: String) -> Result<(), String> {
    let p = Path::new(&path);
    if let Some(parent) = p.parent() {
        fs::create_dir_all(parent).map_err(|e| e.to_string())?;
    }
    use std::io::Write;
    let mut file = fs::OpenOptions::new()
        .create(true)
        .append(true)
        .open(p)
        .map_err(|e| e.to_string())?;
    file.write_all(content.as_bytes())
        .map_err(|e| e.to_string())
}

/// Delete log files older than `max_age_days` in the given directory.
#[tauri::command]
pub fn cleanup_old_logs(dir: String, max_age_days: u64) -> Result<u32, String> {
    let dir_path = Path::new(&dir);
    if !dir_path.exists() {
        return Ok(0);
    }

    let cutoff = std::time::SystemTime::now()
        - std::time::Duration::from_secs(max_age_days * 86400);
    let mut deleted = 0u32;

    let entries = fs::read_dir(dir_path).map_err(|e| e.to_string())?;
    for entry in entries.flatten() {
        let path = entry.path();
        if path.extension().map(|e| e == "log").unwrap_or(false) {
            if let Ok(meta) = fs::metadata(&path) {
                if let Ok(modified) = meta.modified() {
                    if modified < cutoff {
                        if fs::remove_file(&path).is_ok() {
                            deleted += 1;
                        }
                    }
                }
            }
        }
    }

    Ok(deleted)
}
