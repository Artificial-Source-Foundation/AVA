//! AVA Logger Module
//!
//! Provides structured logging for the AVA system.

use ava_types::Result;
use std::path::PathBuf;
use std::sync::Arc;
use tokio::fs::OpenOptions;
use tokio::io::AsyncWriteExt;
use tokio::sync::mpsc;
use tracing::{debug, error, info, trace, warn};

/// Log level
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum LogLevel {
    Trace,
    Debug,
    Info,
    Warn,
    Error,
}

impl From<LogLevel> for tracing::Level {
    fn from(level: LogLevel) -> Self {
        match level {
            LogLevel::Trace => tracing::Level::TRACE,
            LogLevel::Debug => tracing::Level::DEBUG,
            LogLevel::Info => tracing::Level::INFO,
            LogLevel::Warn => tracing::Level::WARN,
            LogLevel::Error => tracing::Level::ERROR,
        }
    }
}

/// Log entry
#[derive(Debug, Clone)]
pub struct LogEntry {
    pub timestamp: chrono::DateTime<chrono::Utc>,
    pub level: LogLevel,
    pub message: String,
    pub metadata: Option<serde_json::Value>,
}

/// Metrics for tracking system performance
#[derive(Debug, Clone, Default)]
pub struct Metrics {
    pub llm_requests: u64,
    pub llm_tokens_sent: u64,
    pub llm_tokens_received: u64,
    pub tool_calls: u64,
    pub session_duration_secs: u64,
}

/// Logger with structured logging and metrics
pub struct Logger {
    log_tx: mpsc::Sender<LogEntry>,
    metrics: Arc<tokio::sync::RwLock<Metrics>>,
}

impl Logger {
    /// Initialize the logging system
    pub fn init() -> Result<()> {
        tracing_subscriber::fmt()
            .with_env_filter(tracing_subscriber::EnvFilter::from_default_env())
            .try_init()
            .map_err(|e| ava_types::AvaError::ConfigError(format!("Failed to init logger: {e}")))
    }

    /// Create a new logger instance
    pub async fn new(log_dir: PathBuf) -> Result<Self> {
        std::fs::create_dir_all(&log_dir)
            .map_err(|e| ava_types::AvaError::IoError(e.to_string()))?;

        let log_path = log_dir.join("ava.log");
        let (log_tx, mut log_rx) = mpsc::channel::<LogEntry>(1000);

        // Spawn background task to write logs
        tokio::spawn(async move {
            let file = OpenOptions::new()
                .create(true)
                .append(true)
                .open(&log_path)
                .await;

            let mut file = match file {
                Ok(f) => f,
                Err(e) => {
                    eprintln!("Failed to open log file: {e}");
                    return;
                }
            };

            while let Some(entry) = log_rx.recv().await {
                let line = format!(
                    "[{}] {:?}: {}\n",
                    entry.timestamp, entry.level, entry.message
                );
                if let Err(e) = file.write_all(line.as_bytes()).await {
                    eprintln!("Failed to write log: {e}");
                }
            }
        });

        Ok(Self {
            log_tx,
            metrics: Arc::new(tokio::sync::RwLock::new(Metrics::default())),
        })
    }

    /// Log a message
    pub async fn log(&self, level: LogLevel, message: &str) {
        let entry = LogEntry {
            timestamp: chrono::Utc::now(),
            level,
            message: message.to_string(),
            metadata: None,
        };
        let _ = self.log_tx.send(entry).await;

        // Also log via tracing
        match level {
            LogLevel::Trace => trace!("{}", message),
            LogLevel::Debug => debug!("{}", message),
            LogLevel::Info => info!("{}", message),
            LogLevel::Warn => warn!("{}", message),
            LogLevel::Error => error!("{}", message),
        }
    }

    /// Log with metadata
    pub async fn log_with_metadata(
        &self,
        level: LogLevel,
        message: &str,
        metadata: serde_json::Value,
    ) {
        let entry = LogEntry {
            timestamp: chrono::Utc::now(),
            level,
            message: message.to_string(),
            metadata: Some(metadata.clone()),
        };
        let _ = self.log_tx.send(entry).await;

        // Also log via tracing with metadata
        let metadata_str = metadata.to_string();
        match level {
            LogLevel::Trace => trace!("{} metadata={}", message, metadata_str),
            LogLevel::Debug => debug!("{} metadata={}", message, metadata_str),
            LogLevel::Info => info!("{} metadata={}", message, metadata_str),
            LogLevel::Warn => warn!("{} metadata={}", message, metadata_str),
            LogLevel::Error => error!("{} metadata={}", message, metadata_str),
        }
    }

    /// Log a tool call
    pub async fn log_tool_call(&self, tool: &str, duration: std::time::Duration) {
        let mut metrics = self.metrics.write().await;
        metrics.tool_calls += 1;

        self.log(
            LogLevel::Info,
            &format!("Tool '{tool}' executed in {duration:?}"),
        )
        .await;
    }

    /// Log an LLM request
    pub async fn log_llm_request(&self, tokens: usize, cost: f64) {
        let mut metrics = self.metrics.write().await;
        metrics.llm_requests += 1;
        metrics.llm_tokens_received += tokens as u64;

        self.log(
            LogLevel::Info,
            &format!("LLM request: {tokens} tokens, ${cost:.4}"),
        )
        .await;
    }

    /// Get current metrics
    pub async fn get_metrics(&self) -> Metrics {
        self.metrics.read().await.clone()
    }

    /// Update session duration
    pub async fn update_session_duration(&self, duration_secs: u64) {
        let mut metrics = self.metrics.write().await;
        metrics.session_duration_secs = duration_secs;
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use tempfile::TempDir;

    #[test]
    fn test_logger_init() {
        // Note: tracing_subscriber can only be initialized once per test process
        // This test verifies that our init() wrapper properly uses try_init()
        // and returns an error on subsequent calls rather than panicking

        // First call should succeed (or fail if another test already initialized)
        let result1 = Logger::init();

        // Second call should definitely fail gracefully
        let result2 = Logger::init();

        // At least one of the two calls should have failed (the second one if first succeeded)
        // or both failed (if another test already initialized)
        match (&result1, &result2) {
            (Ok(_), Err(_)) => (),  // Expected: first succeeds, second fails
            (Err(_), Err(_)) => (), // Also OK: both fail (already initialized elsewhere)
            (Ok(_), Ok(_)) => panic!("Both init calls succeeded - this shouldn't happen"),
            (Err(_), Ok(_)) => panic!("First failed but second succeeded - unexpected"),
        }
    }

    #[tokio::test]
    async fn test_logger_creation() {
        let temp_dir = TempDir::new().unwrap();
        let logger = Logger::new(temp_dir.path().to_path_buf()).await;
        assert!(logger.is_ok());
    }

    #[tokio::test]
    async fn test_log_message() {
        let temp_dir = TempDir::new().unwrap();
        let logger = Logger::new(temp_dir.path().to_path_buf()).await.unwrap();

        logger.log(LogLevel::Info, "Test message").await;

        // Give the background task time to write
        tokio::time::sleep(tokio::time::Duration::from_millis(100)).await;

        // Verify log file was created
        let log_path = temp_dir.path().join("ava.log");
        assert!(log_path.exists());
    }

    #[tokio::test]
    async fn test_tool_call_logging() {
        let temp_dir = TempDir::new().unwrap();
        let logger = Logger::new(temp_dir.path().to_path_buf()).await.unwrap();

        logger
            .log_tool_call("read_file", std::time::Duration::from_millis(100))
            .await;

        let metrics = logger.get_metrics().await;
        assert_eq!(metrics.tool_calls, 1);
    }

    #[tokio::test]
    async fn test_llm_logging() {
        let temp_dir = TempDir::new().unwrap();
        let logger = Logger::new(temp_dir.path().to_path_buf()).await.unwrap();

        logger.log_llm_request(1000, 0.05).await;

        let metrics = logger.get_metrics().await;
        assert_eq!(metrics.llm_requests, 1);
        assert_eq!(metrics.llm_tokens_received, 1000);
    }
}
