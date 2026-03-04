//! File system abstraction

use async_trait::async_trait;
use ava_types::Result;
use std::path::{Path, PathBuf};
use tokio::fs;
use tokio::io::AsyncWriteExt;

/// File information
#[derive(Debug, Clone)]
pub struct FileInfo {
    pub path: PathBuf,
    pub size: u64,
    pub is_dir: bool,
    pub modified: Option<chrono::DateTime<chrono::Utc>>,
}

/// Trait for file system operations
#[async_trait]
pub trait FileSystem: Send + Sync {
    /// Read file contents as string
    async fn read_file(&self, path: &Path) -> Result<String>;

    /// Read file contents as bytes
    async fn read_file_bytes(&self, path: &Path) -> Result<Vec<u8>>;

    /// Write content to file
    async fn write_file(&self, path: &Path, content: &str) -> Result<()>;

    /// Write bytes to file
    async fn write_file_bytes(&self, path: &Path, content: &[u8]) -> Result<()>;

    /// Check if path exists
    async fn exists(&self, path: &Path) -> bool;

    /// Check if path is a directory
    async fn is_directory(&self, path: &Path) -> bool;

    /// Get file metadata
    async fn metadata(&self, path: &Path) -> Result<FileInfo>;

    /// List directory contents
    async fn read_dir(&self, path: &Path) -> Result<Vec<FileInfo>>;

    /// Create directory and all parents
    async fn create_dir_all(&self, path: &Path) -> Result<()>;

    /// Remove file
    async fn remove_file(&self, path: &Path) -> Result<()>;

    /// Remove directory and all contents
    async fn remove_dir_all(&self, path: &Path) -> Result<()>;

    /// Copy file
    async fn copy(&self, from: &Path, to: &Path) -> Result<u64>;

    /// Rename/move file
    async fn rename(&self, from: &Path, to: &Path) -> Result<()>;
}

/// Local file system implementation
pub struct LocalFileSystem;

impl LocalFileSystem {
    pub fn new() -> Self {
        Self
    }
}

#[async_trait]
impl FileSystem for LocalFileSystem {
    async fn read_file(&self, path: &Path) -> Result<String> {
        Ok(tokio::fs::read_to_string(path).await?)
    }

    async fn read_file_bytes(&self, path: &Path) -> Result<Vec<u8>> {
        Ok(tokio::fs::read(path).await?)
    }

    async fn write_file(&self, path: &Path, content: &str) -> Result<()> {
        if let Some(parent) = path.parent() {
            tokio::fs::create_dir_all(parent).await?;
        }
        tokio::fs::write(path, content).await?;
        Ok(())
    }

    async fn write_file_bytes(&self, path: &Path, content: &[u8]) -> Result<()> {
        if let Some(parent) = path.parent() {
            tokio::fs::create_dir_all(parent).await?;
        }
        let mut file = fs::File::create(path).await?;
        file.write_all(content).await?;
        Ok(())
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

    async fn metadata(&self, path: &Path) -> Result<FileInfo> {
        let metadata = tokio::fs::metadata(path).await?;

        let modified = metadata.modified().ok().map(|t| chrono::DateTime::from(t));

        Ok(FileInfo {
            path: path.to_path_buf(),
            size: metadata.len(),
            is_dir: metadata.is_dir(),
            modified,
        })
    }

    async fn read_dir(&self, path: &Path) -> Result<Vec<FileInfo>> {
        let mut entries = vec![];
        let mut dir = tokio::fs::read_dir(path).await?;

        while let Some(entry) = dir.next_entry().await? {
            let metadata = entry.metadata().await?;

            let modified = metadata.modified().ok().map(|t| chrono::DateTime::from(t));

            entries.push(FileInfo {
                path: entry.path(),
                size: metadata.len(),
                is_dir: metadata.is_dir(),
                modified,
            });
        }

        Ok(entries)
    }

    async fn create_dir_all(&self, path: &Path) -> Result<()> {
        tokio::fs::create_dir_all(path).await?;
        Ok(())
    }

    async fn remove_file(&self, path: &Path) -> Result<()> {
        tokio::fs::remove_file(path).await?;
        Ok(())
    }

    async fn remove_dir_all(&self, path: &Path) -> Result<()> {
        tokio::fs::remove_dir_all(path).await?;
        Ok(())
    }

    async fn copy(&self, from: &Path, to: &Path) -> Result<u64> {
        Ok(tokio::fs::copy(from, to).await?)
    }

    async fn rename(&self, from: &Path, to: &Path) -> Result<()> {
        tokio::fs::rename(from, to).await?;
        Ok(())
    }
}

impl Default for LocalFileSystem {
    fn default() -> Self {
        Self::new()
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use tempfile::TempDir;

    #[tokio::test]
    async fn test_write_and_read_file() {
        let fs = LocalFileSystem::new();
        let temp_dir = TempDir::new().unwrap();
        let file_path = temp_dir.path().join("test.txt");

        fs.write_file(&file_path, "Hello, World!").await.unwrap();
        let content = fs.read_file(&file_path).await.unwrap();

        assert_eq!(content, "Hello, World!");
    }

    #[tokio::test]
    async fn test_exists() {
        let fs = LocalFileSystem::new();
        let temp_dir = TempDir::new().unwrap();
        let file_path = temp_dir.path().join("exists.txt");

        assert!(!fs.exists(&file_path).await);
        fs.write_file(&file_path, "test").await.unwrap();
        assert!(fs.exists(&file_path).await);
    }

    #[tokio::test]
    async fn test_create_and_remove_dir() {
        let fs = LocalFileSystem::new();
        let temp_dir = TempDir::new().unwrap();
        let dir_path = temp_dir.path().join("test_dir");

        fs.create_dir_all(&dir_path).await.unwrap();
        assert!(fs.is_directory(&dir_path).await);

        fs.remove_dir_all(&dir_path).await.unwrap();
        assert!(!fs.exists(&dir_path).await);
    }

    #[tokio::test]
    async fn test_copy_and_rename() {
        let fs = LocalFileSystem::new();
        let temp_dir = TempDir::new().unwrap();
        let source = temp_dir.path().join("source.txt");
        let copy_dest = temp_dir.path().join("copy.txt");
        let rename_dest = temp_dir.path().join("renamed.txt");

        fs.write_file(&source, "test content").await.unwrap();
        fs.copy(&source, &copy_dest).await.unwrap();
        assert!(fs.exists(&copy_dest).await);

        fs.rename(&copy_dest, &rename_dest).await.unwrap();
        assert!(!fs.exists(&copy_dest).await);
        assert!(fs.exists(&rename_dest).await);
    }

    #[tokio::test]
    async fn test_read_dir() {
        let fs = LocalFileSystem::new();
        let temp_dir = TempDir::new().unwrap();

        fs.write_file(&temp_dir.path().join("file1.txt"), "1")
            .await
            .unwrap();
        fs.write_file(&temp_dir.path().join("file2.txt"), "2")
            .await
            .unwrap();
        fs.create_dir_all(&temp_dir.path().join("subdir"))
            .await
            .unwrap();

        let entries = fs.read_dir(temp_dir.path()).await.unwrap();
        assert_eq!(entries.len(), 3);
    }
}
