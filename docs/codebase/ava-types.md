# ava-types

> Core types shared across AVA crates: errors, messages, sessions, tools, and context attachments.

## Public API

| Type/Function | Description |
|--------------|-------------|
| `AvaError` | Unified error enum with structured variants (ProviderError, MissingApiKey, RateLimited, ToolNotFound, etc.) and legacy string variants |
| `ErrorCategory` | Error classification: Tool, System, Data, Config, Validation, Database, Timeout, NotFound, Permission, Provider, Agent |
| `Result<T>` | Type alias for `std::result::Result<T, AvaError>` |
| `Message` | Chat message with role, content, timestamp, tool calls/results, images, parent ID for threading |
| `Role` | Enum: System, User, Assistant, Tool |
| `ImageContent` | Base64-encoded image with media type for multimodal messages |
| `ImageMediaType` | Enum: Png, Jpeg, Gif, WebP with MIME type and extension detection |
| `Session` | Conversation session with messages, metadata, token usage tracking, branch head support |
| `Context` | LLM context window management with token counting and limits |
| `Tool` | Tool definition with name, description, JSON parameters schema |
| `ToolCall` | Tool invocation with ID, name, and arguments |
| `ToolResult` | Tool execution result with call_id, content, is_error flag |
| `TodoItem` | Task item with content, status, priority |
| `TodoStatus` | Enum: Pending, InProgress, Completed, Cancelled |
| `TodoPriority` | Enum: High, Medium, Low |
| `TodoState` | Thread-safe shared todo list using `Arc<RwLock<Vec<TodoItem>>>` |
| `ContextAttachment` | @-mention attachment types: File, Folder, CodebaseQuery |
| `parse_mentions()` | Parse @-mentions from text, returns (attachments, cleaned_text) |
| `MessageTier` | Priority tiers for mid-stream messages: Steering, FollowUp, PostComplete |
| `QueuedMessage` | User message queued for delivery while agent runs |
| `TokenUsage` | Token counts: input, output, cache_read, cache_creation |
| `StreamChunk` | Streaming LLM response chunk with content, tool_call, usage, thinking, done flag |
| `StreamToolCall` | Partial tool call from streaming: index, id, name, arguments_delta |
| `ThinkingLevel` | Reasoning effort: Off, Low, Medium, High, Max with cycle/label methods |

## Module Map

| File | Purpose |
|------|---------|
| `lib.rs` | Exports all modules, defines ContextAttachment, MessageTier, QueuedMessage, TokenUsage, StreamChunk, StreamToolCall, ThinkingLevel, parse_mentions() |
| `error.rs` | AvaError enum with 15+ variants, ErrorCategory, retryable checks, user-friendly messages |
| `message.rs` | Message, Role, ImageContent, ImageMediaType structs with serialization |
| `session.rs` | Session struct with message history, metadata, token usage, branch head |
| `context.rs` | Context struct for token window management |
| `tool.rs` | Tool, ToolCall, ToolResult definitions |
| `todo.rs` | TodoItem, TodoStatus, TodoPriority, TodoState for task tracking |

## Dependencies

Uses: None (only external crates: serde, serde_json, uuid, chrono, thiserror, base64, tracing)

Used by: ava-config, ava-agent, ava-tui, ava-tools, ava-llm, ava-praxis, ava-session, ava-db, ava-context, ava-mcp, ava-plugin, ava-platform, ava-cli-providers

## Key Patterns

- **Structured errors**: New code uses typed variants (e.g., `ProviderError { provider, message }`); legacy uses string payloads
- **Error categorization**: `AvaError::category()` maps variants to `ErrorCategory` for grouping
- **Retryability**: `is_retryable()` identifies transient failures (timeouts, rate limits, DB errors)
- **User messages**: `user_message()` provides actionable error text without technical noise
- **Conversions**: `From<std::io::Error>` and `From<serde_json::Error>` for ergonomic `?` usage
- **@-mention parsing**: Supports `@file:path`, `@folder:path`, `@codebase:query`, and bare `@path/to/file.rs`
- **Streaming types**: `StreamChunk`/`StreamToolCall` designed for incremental LLM response handling
- **Thinking levels**: 5-tier reasoning control with cycling and display labels
- **Thread-safe state**: `TodoState` uses `std::sync::RwLock` (not tokio) for sync TUI access
- **Serde camelCase**: All types use `#[serde(rename_all = "camelCase")]` for JSON compatibility
