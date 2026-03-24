use std::collections::VecDeque;
use std::fs;
use std::io::{BufRead, BufReader};
use std::path::Path;

const MAX_LOG_FILE_SIZE_BYTES: u64 = 50 * 1024 * 1024;
const MAX_ROTATED_FILES: u32 = 10;

fn rotated_path(path: &Path, index: u32) -> String {
    format!("{}.{}", path.to_string_lossy(), index)
}

fn rotate_log_if_needed(path: &Path, incoming_bytes: usize) -> Result<(), String> {
    let current_size = fs::metadata(path).map(|m| m.len()).unwrap_or(0);
    let projected_size = current_size.saturating_add(incoming_bytes as u64);
    if projected_size <= MAX_LOG_FILE_SIZE_BYTES {
        return Ok(());
    }

    for i in (1..=MAX_ROTATED_FILES).rev() {
        let current = rotated_path(path, i);
        let next = rotated_path(path, i + 1);
        let current_path = Path::new(&current);
        if current_path.exists() {
            if i == MAX_ROTATED_FILES {
                let _ = fs::remove_file(current_path);
            } else {
                let _ = fs::rename(current_path, next);
            }
        }
    }

    if path.exists() {
        fs::rename(path, rotated_path(path, 1)).map_err(|e| e.to_string())?;
    }

    Ok(())
}

/// Get the current working directory (for locating project-local logs).
#[tauri::command]
pub fn get_cwd() -> Result<String, String> {
    std::env::current_dir()
        .map(|p| p.to_string_lossy().to_string())
        .map_err(|e| e.to_string())
}

/// Change the process working directory to a new project path.
///
/// Called when the user opens or switches to a different project so that
/// relative paths, project-local config, and agent tools resolve correctly.
#[tauri::command]
pub fn set_cwd(path: String) -> Result<(), String> {
    let p = std::path::Path::new(&path);
    if !p.is_dir() {
        return Err(format!("Not a directory: {path}"));
    }
    std::env::set_current_dir(p).map_err(|e| format!("Failed to change directory to {path}: {e}"))
}

/// Append text to a log file, creating parent directories if needed.
///
/// TODO(security): This command accepts caller-supplied paths with no
/// confinement. It should validate that the resolved path falls within the
/// Tauri `app_data_dir()/logs` directory to prevent arbitrary file writes.
/// Requires an `AppHandle` parameter to call `app_handle.path().app_data_dir()`.
#[tauri::command]
pub fn append_log(path: String, content: String) -> Result<(), String> {
    let p = Path::new(&path);
    if let Some(parent) = p.parent() {
        fs::create_dir_all(parent).map_err(|e| e.to_string())?;
    }
    rotate_log_if_needed(p, content.len())?;
    use std::io::Write;
    let mut file = fs::OpenOptions::new()
        .create(true)
        .append(true)
        .open(p)
        .map_err(|e| e.to_string())?;
    file.write_all(content.as_bytes())
        .map_err(|e| e.to_string())
}

/// Read the latest N lines from a log file.
///
/// TODO(security): Same as `append_log` — confine `path` to app log directory.
#[tauri::command]
pub fn read_latest_logs(path: String, lines: usize) -> Result<String, String> {
    let p = Path::new(&path);
    if !p.exists() {
        return Ok(String::new());
    }

    if lines == 0 {
        return Ok(String::new());
    }

    let file = fs::File::open(p).map_err(|e| e.to_string())?;
    let reader = BufReader::new(file);
    let mut tail: VecDeque<String> = VecDeque::with_capacity(lines);

    for line in reader.lines() {
        let line = line.map_err(|e| e.to_string())?;
        if tail.len() == lines {
            tail.pop_front();
        }
        tail.push_back(line);
    }

    Ok(tail.into_iter().collect::<Vec<String>>().join("\n"))
}

/// Delete log files older than `max_age_days` in the given directory.
///
/// TODO(security): Same as `append_log` — confine `dir` to app log directory.
#[tauri::command]
pub fn cleanup_old_logs(dir: String, max_age_days: u64) -> Result<u32, String> {
    let dir_path = Path::new(&dir);
    if !dir_path.exists() {
        return Ok(0);
    }

    let cutoff =
        std::time::SystemTime::now() - std::time::Duration::from_secs(max_age_days * 86400);
    let mut deleted = 0u32;

    let entries = fs::read_dir(dir_path).map_err(|e| e.to_string())?;
    for entry in entries.flatten() {
        let path = entry.path();
        let is_log = path
            .file_name()
            .and_then(|n| n.to_str())
            .map(|name| name.contains(".log"))
            .unwrap_or(false);

        if is_log {
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
