# ava-platform

> Platform abstraction for file system and shell operations — async traits for local and sandboxed execution.

## Public API

| Type/Function | Description |
|--------------|-------------|
| `Platform` | Async trait for file system and shell operations |
| `Platform::read_file()` | Read file contents as string |
| `Platform::write_file()` | Write string content to file |
| `Platform::create_dir_all()` | Create directory recursively |
| `Platform::exists()` | Check if path exists |
| `Platform::is_directory()` | Check if path is a directory |
| `Platform::execute()` | Execute shell command with default options |
| `Platform::execute_with_options()` | Execute with timeout, working dir, env vars, scrub_env |
| `Platform::execute_streaming()` | Execute with streaming output lines |
| `Platform::execute_streaming_with_options()` | Streaming with explicit options |
| `StandardPlatform` | Local filesystem and shell implementation |
| `FileSystem` | Async trait for file operations (read/write/metadata/dir) |
| `LocalFileSystem` | Tokio-based local filesystem implementation |
| `FileInfo` | File metadata (path, size, is_dir, modified time) |
| `Shell` | Async trait for command execution |
| `LocalShell` | Tokio process-based shell implementation |
| `CommandOutput` | Command result (stdout, stderr, exit_code, duration) |
| `ExecuteOptions` | Execution configuration (timeout, working_dir, env_vars, scrub_env) |

## Module Map

| File | Purpose |
|------|---------|
| `lib.rs` | Exports Platform trait, StandardPlatform, and re-exports fs/shell modules |
| `fs.rs` | FileSystem trait and LocalFileSystem implementation (249 lines) |
| `shell.rs` | Shell trait and LocalShell implementation with timeout/cancellation (324 lines) |

## Dependencies

Uses: ava-types

Used by: ava-tui, ava-agent, ava-tools, ava-praxis

## Key Patterns

- **Trait-based abstraction**: `Platform`, `FileSystem`, and `Shell` are async traits enabling test mocking and future sandboxed implementations
- **Timeout handling**: Shell execution uses `tokio::time::timeout` with explicit child process cleanup on timeout to prevent zombies
- **Environment scrubbing**: `ExecuteOptions.scrub_env` clears inherited environment and sets safe PATH for sandboxed execution
- **Streaming output**: `execute_streaming` spawns stdout/stderr readers into separate tasks, merges via mpsc channel
- **Cancellation**: Shell uses `Arc<Mutex<Option<Child>>>` so timeout task can kill child while wait task reaps it
- **Error mapping**: IO errors converted to `AvaError::IoError` or `AvaError::PlatformError` for consistent error types
- **Test coverage**: Extensive tests for timeout, working directory, env vars, and streaming (all async with tokio::test)
