# ava-types

Shared type definitions used across the entire AVA crate graph. Every other crate depends on `ava-types` for error handling, message representation, streaming primitives, and tool call structures.

## Key Types

### Error System (`src/error.rs`)

`AvaError` is a unified error enum with 21 variants spanning structured and legacy categories.

```rust
pub enum AvaError {
    // Structured variants
    ProviderError(String),
    AuthenticationError(String),
    RateLimitError(String),
    TimeoutError(String),
    ModelNotFoundError(String),
    ContextLengthError(String),
    ToolError(String),
    ToolNotFound(String),
    PermissionDenied(String),
    PlatformError(String),
    SessionError(String),
    // Legacy variants
    NetworkError(String),
    ConfigError(String),
    IoError(String),
    SerializationError(String),
    NotFound(String),
    DatabaseError(String),
    ValidationError(String),
    ExtensionError(String),
    UnsupportedError(String),
    InternalError(String),
}
```

Each variant maps to an `ErrorCategory` (11 categories: Provider, Auth, RateLimit, Timeout, Model, Context, Tool, Permission, Platform, Session, Internal). The `is_retryable()` method returns `true` for Network, RateLimit, Timeout, and Provider errors. `user_message()` provides human-readable error descriptions.

**File**: `crates/ava-types/src/error.rs` (lines 1-427)

### Message (`src/message.rs`)

```rust
pub struct Message {
    pub id: Uuid,
    pub role: Role,          // System | User | Assistant | Tool
    pub content: String,
    pub timestamp: DateTime<Utc>,
    pub tool_calls: Vec<ToolCall>,
    pub tool_results: Vec<ToolResult>,
    pub tool_call_id: Option<String>,
}
```

**File**: `crates/ava-types/src/message.rs` (lines 1-94)

### Tool Types (`src/tool.rs`)

```rust
pub struct Tool {
    pub name: String,
    pub description: String,
    pub parameters: serde_json::Value,  // JSON Schema
}

pub struct ToolCall {
    pub id: String,
    pub name: String,
    pub arguments: serde_json::Value,
}

pub struct ToolResult {
    pub call_id: String,
    pub content: String,
    pub is_error: bool,
}
```

**File**: `crates/ava-types/src/tool.rs` (lines 1-68)

### Streaming (`src/lib.rs`)

`StreamChunk` is the rich streaming primitive that replaces `Stream<Item=String>`:

```rust
pub struct StreamChunk {
    pub content: Option<String>,
    pub tool_call: Option<StreamToolCall>,
    pub usage: Option<TokenUsage>,
    pub thinking: Option<String>,
    pub done: bool,
}
```

`StreamToolCall` carries partial tool call data assembled incrementally: `index`, `id`, `name`, `arguments_delta`.

**File**: `crates/ava-types/src/lib.rs` (lines 36-89)

### TokenUsage (`src/lib.rs`)

```rust
pub struct TokenUsage {
    pub input_tokens: usize,
    pub output_tokens: usize,
    pub cache_read_tokens: usize,
    pub cache_creation_tokens: usize,
}
```

Tracks Anthropic cache_read/creation tokens and OpenAI cached_tokens.

**File**: `crates/ava-types/src/lib.rs` (lines 22-33)

### ThinkingLevel (`src/lib.rs`)

Enum for extended thinking/reasoning: `Off | Low | Medium | High | Max`. Supports `cycle()` for UI toggling, `label()` for display, and `from_str_loose()` for parsing user input (accepts aliases like "l", "med", "h", "x", numeric 0-4).

**File**: `crates/ava-types/src/lib.rs` (lines 91-151)

### Session (`src/session.rs`)

```rust
pub struct Session {
    pub id: Uuid,
    pub created_at: DateTime<Utc>,
    pub updated_at: DateTime<Utc>,
    pub messages: Vec<Message>,
    pub metadata: serde_json::Value,
    pub token_usage: TokenUsage,
}
```

**File**: `crates/ava-types/src/session.rs` (lines 1-50)

### Context (`src/context.rs`)

```rust
pub struct Context {
    pub messages: Vec<Message>,
    pub token_count: usize,
    pub token_limit: usize,
}
```

**File**: `crates/ava-types/src/context.rs` (lines 1-58)

### TodoItem (`src/todo.rs`)

Task tracking with `TodoItem`, `TodoStatus` (Pending/InProgress/Completed/Cancelled), `TodoPriority` (High/Medium/Low), and `TodoState` (Arc<RwLock<Vec<TodoItem>>>).

**File**: `crates/ava-types/src/todo.rs` (lines 1-202)

## Source Files

| File | Lines | Purpose |
|------|------:|---------|
| `src/lib.rs` | 283 | TokenUsage, StreamChunk, ThinkingLevel, re-exports |
| `src/error.rs` | 427 | AvaError (21 variants), ErrorCategory, retryability |
| `src/message.rs` | 94 | Message, Role |
| `src/session.rs` | 86 | Session |
| `src/tool.rs` | 68 | Tool, ToolCall, ToolResult |
| `src/context.rs` | 58 | Context |
| `src/todo.rs` | 202 | TodoItem, TodoStatus, TodoPriority, TodoState |
