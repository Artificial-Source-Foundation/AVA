# ava-platform

Platform abstraction traits for filesystem and shell operations. Provides both trait interfaces (for testing/mocking) and concrete implementations for local execution.

## Key Types

### Platform Trait (`src/lib.rs`)

High-level trait combining file and shell operations:

```rust
pub trait Platform: Send + Sync {
    fn read_file(&self, path: &str) -> Result<String>;
    fn write_file(&self, path: &str, content: &str) -> Result<()>;
    fn exists(&self, path: &str) -> bool;
    fn is_directory(&self, path: &str) -> bool;
    fn execute(&self, command: &str, args: &[&str], cwd: &str) -> Result<CommandOutput>;
    fn execute_streaming(&self, command: &str, args: &[&str], cwd: &str) -> Result</* stream */>;
}
```

`StandardPlatform` provides the concrete implementation using standard library I/O and `tokio::process::Command`.

### FileSystem Trait (`src/fs.rs`)

Fine-grained file system abstraction with 14 methods:

```rust
pub trait FileSystem: Send + Sync {
    fn read_file(&self, path: &Path) -> Result<String>;
    fn write_file(&self, path: &Path, content: &str) -> Result<()>;
    fn exists(&self, path: &Path) -> bool;
    fn is_directory(&self, path: &Path) -> bool;
    fn create_dir_all(&self, path: &Path) -> Result<()>;
    fn remove_file(&self, path: &Path) -> Result<()>;
    fn remove_dir_all(&self, path: &Path) -> Result<()>;
    fn list_directory(&self, path: &Path) -> Result<Vec<FileInfo>>;
    // ... and more
}
```

`FileInfo` captures path, name, is_directory, size, and modified timestamp.

`LocalFileSystem` provides the concrete implementation.

### Shell Trait (`src/shell.rs`)

```rust
pub trait Shell: Send + Sync {
    fn execute(&self, command: &str, options: ExecuteOptions) -> Result<CommandOutput>;
    fn execute_streaming(&self, command: &str, options: ExecuteOptions) -> Result</* stream */>;
}
```

`LocalShell` uses `tokio::process::Command` with configurable timeouts. `CommandOutput` contains `stdout`, `stderr`, `exit_code`, and `success`. `ExecuteOptions` specifies `cwd`, `env`, `timeout`, and `stdin`.

Streaming execution uses `mpsc` channels to deliver stdout/stderr lines in real time.

## Source Files

| File | Lines | Purpose |
|------|------:|---------|
| `src/lib.rs` | -- | Platform trait, StandardPlatform |
| `src/fs.rs` | -- | FileSystem trait, LocalFileSystem, FileInfo |
| `src/shell.rs` | -- | Shell trait, LocalShell, CommandOutput, streaming |
