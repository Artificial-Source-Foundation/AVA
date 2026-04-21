use std::fs::OpenOptions;
use std::path::{Path, PathBuf};
use std::sync::Mutex;

use tracing_appender::non_blocking::WorkerGuard;
use tracing_subscriber::layer::SubscriberExt;
use tracing_subscriber::util::SubscriberInitExt;
use tracing_subscriber::Layer;

const BACKEND_LOG_FILE_NAME: &str = "desktop-backend.log";
const MAX_BACKEND_LOG_FILE_SIZE_BYTES: u64 = 50 * 1024 * 1024;
const MAX_BACKEND_ROTATED_FILES: u32 = 10;
const DEFAULT_LOG_FILTER: &str =
    "info,ava=debug,ava_lib=debug,ava_agent=info,ava_llm=info,ava_tools=info,ava_mcp=info";

pub struct BackendLoggingState {
    #[allow(dead_code)]
    guard: Mutex<Option<WorkerGuard>>,
    #[allow(dead_code)]
    log_dir: PathBuf,
}

fn rotated_path(path: &Path, index: u32) -> String {
    format!("{}.{}", path.to_string_lossy(), index)
}

fn rotate_backend_log_if_needed(path: &Path, incoming_bytes: usize) -> Result<(), String> {
    let current_size = std::fs::metadata(path)
        .map(|metadata| metadata.len())
        .unwrap_or(0);
    if current_size.saturating_add(incoming_bytes as u64) <= MAX_BACKEND_LOG_FILE_SIZE_BYTES {
        return Ok(());
    }

    for i in (1..=MAX_BACKEND_ROTATED_FILES).rev() {
        let current = rotated_path(path, i);
        let next = rotated_path(path, i + 1);
        let current_path = Path::new(&current);
        if current_path.exists() {
            if i == MAX_BACKEND_ROTATED_FILES {
                let _ = std::fs::remove_file(current_path);
            } else {
                let _ = std::fs::rename(current_path, next);
            }
        }
    }

    if path.exists() {
        std::fs::rename(path, rotated_path(path, 1))
            .map_err(|error| format!("failed to rotate backend log file: {error}"))?;
    }

    Ok(())
}

struct RotatingBackendLogWriter {
    path: PathBuf,
}

impl std::io::Write for RotatingBackendLogWriter {
    fn write(&mut self, buf: &[u8]) -> std::io::Result<usize> {
        rotate_backend_log_if_needed(&self.path, buf.len()).map_err(std::io::Error::other)?;

        let mut file = OpenOptions::new()
            .create(true)
            .append(true)
            .open(&self.path)?;
        file.write_all(buf)?;
        Ok(buf.len())
    }

    fn flush(&mut self) -> std::io::Result<()> {
        Ok(())
    }
}

pub fn init_backend_logging(_app_data_dir: &Path) -> Result<BackendLoggingState, String> {
    let log_dir = ava_config::logs_dir()
        .map_err(|error| format!("failed to resolve backend log directory: {error}"))?;
    std::fs::create_dir_all(&log_dir)
        .map_err(|error| format!("failed to create backend log directory: {error}"))?;

    let log_file = log_dir.join(BACKEND_LOG_FILE_NAME);
    rotate_backend_log_if_needed(&log_file, 0)?;

    let file_appender = RotatingBackendLogWriter {
        path: log_file.clone(),
    };
    let (file_writer, guard) = tracing_appender::non_blocking(file_appender);

    let filter = tracing_subscriber::EnvFilter::try_from_default_env()
        .unwrap_or_else(|_| tracing_subscriber::EnvFilter::new(DEFAULT_LOG_FILTER));

    let file_layer = tracing_subscriber::fmt::layer()
        .with_writer(file_writer)
        .with_target(true)
        .with_file(true)
        .with_line_number(true)
        .with_ansi(false)
        .with_filter(filter);

    tracing_subscriber::registry()
        .with(file_layer)
        .try_init()
        .map_err(|error| format!("failed to initialize backend tracing subscriber: {error}"))?;

    tracing::info!(
        log_dir = %log_dir.display(),
        log_file = %log_file.display(),
        "Desktop backend logging initialized"
    );

    Ok(BackendLoggingState {
        guard: Mutex::new(Some(guard)),
        log_dir,
    })
}
