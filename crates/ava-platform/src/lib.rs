//! AVA Platform Abstraction
//!
//! Provides platform-specific abstractions for file system operations and shell execution.

use ava_types::Result;
use futures::stream::BoxStream;
use std::path::Path;

pub mod fs;
pub mod shell;

pub use fs::FileSystem;
pub use shell::{CommandOutput, ExecuteOptions, Shell};

/// Platform trait for file system and shell operations
#[async_trait::async_trait]
pub trait Platform: Send + Sync {
    /// Read file contents
    async fn read_file(&self, path: &Path) -> Result<String>;

    /// Write content to file
    async fn write_file(&self, path: &Path, content: &str) -> Result<()>;

    /// Create a directory path recursively.
    async fn create_dir_all(&self, path: &Path) -> Result<()>;

    /// Check if path exists
    async fn exists(&self, path: &Path) -> bool;

    /// Check if path is a directory
    async fn is_directory(&self, path: &Path) -> bool;

    /// Execute a shell command with explicit execution options.
    async fn execute_with_options(
        &self,
        command: &str,
        options: ExecuteOptions,
    ) -> Result<CommandOutput>;

    /// Execute a shell command using default execution options.
    async fn execute(&self, command: &str) -> Result<CommandOutput> {
        self.execute_with_options(command, ExecuteOptions::default())
            .await
    }

    /// Execute a shell command with streaming output and explicit options.
    async fn execute_streaming_with_options(
        &self,
        command: &str,
        options: ExecuteOptions,
    ) -> Result<BoxStream<'static, Result<String>>>;

    /// Execute a shell command with streaming output using default options.
    async fn execute_streaming(&self, command: &str) -> Result<BoxStream<'static, Result<String>>> {
        self.execute_streaming_with_options(command, ExecuteOptions::default())
            .await
    }
}

/// Standard platform implementation using local file system and shell
pub struct StandardPlatform;

#[async_trait::async_trait]
impl Platform for StandardPlatform {
    async fn read_file(&self, path: &Path) -> Result<String> {
        Ok(tokio::fs::read_to_string(path).await?)
    }

    async fn write_file(&self, path: &Path, content: &str) -> Result<()> {
        Ok(tokio::fs::write(path, content).await?)
    }

    async fn create_dir_all(&self, path: &Path) -> Result<()> {
        Ok(tokio::fs::create_dir_all(path).await?)
    }

    async fn exists(&self, path: &Path) -> bool {
        tokio::fs::metadata(path).await.is_ok()
    }

    async fn is_directory(&self, path: &Path) -> bool {
        tokio::fs::metadata(path)
            .await
            .map(|m| m.is_dir())
            // infallible: metadata failure (e.g. ENOENT) means "not a directory"
            .unwrap_or(false)
    }

    async fn execute_with_options(
        &self,
        command: &str,
        options: ExecuteOptions,
    ) -> Result<CommandOutput> {
        let shell = shell::LocalShell::new();
        shell.execute(command, options).await
    }

    async fn execute_streaming_with_options(
        &self,
        command: &str,
        options: ExecuteOptions,
    ) -> Result<BoxStream<'static, Result<String>>> {
        let shell = shell::LocalShell::new();
        shell.execute_streaming(command, options).await
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[tokio::test]
    async fn test_platform_exists() {
        let platform = StandardPlatform;
        assert!(platform.exists(Path::new("Cargo.toml")).await);
        assert!(
            !platform
                .exists(Path::new("nonexistent_file_12345.txt"))
                .await
        );
    }

    #[tokio::test]
    async fn test_platform_is_directory() {
        let platform = StandardPlatform;
        assert!(platform.is_directory(Path::new("src")).await);
        assert!(!platform.is_directory(Path::new("Cargo.toml")).await);
    }

    #[tokio::test]
    async fn test_platform_create_dir_all() {
        let platform = StandardPlatform;
        let temp_dir = tempfile::TempDir::new().unwrap();
        let nested = temp_dir.path().join("a/b/c");
        platform.create_dir_all(&nested).await.unwrap();
        assert!(platform.is_directory(&nested).await);
    }

    #[tokio::test]
    async fn test_execute_echo() {
        let platform = StandardPlatform;
        let result = platform.execute("echo 'hello world'").await.unwrap();
        assert!(result.stdout.contains("hello world"));
        assert_eq!(result.exit_code, 0);
    }

    #[tokio::test]
    async fn test_backend_execute_with_working_directory() {
        let platform = StandardPlatform;
        let result = platform
            .execute_with_options(
                "pwd",
                ExecuteOptions {
                    working_dir: Some(std::path::PathBuf::from("/tmp")),
                    ..Default::default()
                },
            )
            .await
            .unwrap();
        assert!(result.stdout.contains("/tmp"));
    }

    #[tokio::test]
    async fn test_backend_execute_with_timeout() {
        let platform = StandardPlatform;
        let result = platform
            .execute_with_options(
                "sleep 5",
                ExecuteOptions {
                    timeout: Some(std::time::Duration::from_millis(100)),
                    ..Default::default()
                },
            )
            .await;
        assert!(matches!(result, Err(ava_types::AvaError::TimeoutError(_))));
    }
}
