//! AVA Platform Abstraction
//!
//! Provides platform-specific abstractions for file system operations and shell execution.

use ava_types::Result;
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

    /// Check if path exists
    async fn exists(&self, path: &Path) -> bool;

    /// Check if path is a directory
    async fn is_directory(&self, path: &Path) -> bool;

    /// Execute a shell command
    async fn execute(&self, command: &str) -> Result<CommandOutput>;

    /// Execute a shell command with streaming output
    async fn execute_streaming(
        &self,
        command: &str,
    ) -> Result<futures::stream::BoxStream<'static, Result<String>>>;
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

    async fn exists(&self, path: &Path) -> bool {
        tokio::fs::metadata(path).await.is_ok()
    }

    async fn is_directory(&self, path: &Path) -> bool {
        tokio::fs::metadata(path)
            .await
            .map(|m| m.is_dir())
            .unwrap_or(false)
    }

    async fn execute(&self, command: &str) -> Result<CommandOutput> {
        let shell = shell::LocalShell::new();
        shell
            .execute(command, shell::ExecuteOptions::default())
            .await
    }

    async fn execute_streaming(
        &self,
        command: &str,
    ) -> Result<futures::stream::BoxStream<'static, Result<String>>> {
        let shell = shell::LocalShell::new();
        shell
            .execute_streaming(command, shell::ExecuteOptions::default())
            .await
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
    async fn test_execute_echo() {
        let platform = StandardPlatform;
        let result = platform.execute("echo 'hello world'").await.unwrap();
        assert!(result.stdout.contains("hello world"));
        assert_eq!(result.exit_code, 0);
    }
}
