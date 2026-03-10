# Session Management

AVA persists conversations as sessions in a SQLite database. Each session
contains a sequence of messages, metadata, and optional parent-child links
for sub-agent conversations.

All session logic lives in `crates/ava-session/`.

## Session Lifecycle

### Create

`SessionManager::create()` returns a new `Session` with a fresh UUID and empty
message list (`crates/ava-session/src/lib.rs:83`). Sessions are created
in-memory -- they are not written to the database until `save()` is called.

The `AgentLoop` also creates sessions internally during `run()` via
`Session::new()` (`crates/ava-agent/src/agent_loop/mod.rs:176`).

### Save

`SessionManager::save()` (`crates/ava-session/src/lib.rs:87`) writes or
updates a session using `INSERT ... ON CONFLICT DO UPDATE`. All messages are
deleted and re-inserted within a transaction to ensure consistency.

The TUI saves sessions after each agent run completes.

### Load

`SessionManager::get(id)` loads a session by UUID
(`crates/ava-session/src/lib.rs:137`). Messages are loaded ordered by
`timestamp ASC, id ASC`.

`SessionManager::list_recent(limit)` returns the most recently updated
sessions (`line 142`), ordered by `updated_at DESC`.

### Fork

`SessionManager::fork(session)` (`crates/ava-session/src/lib.rs:163`) creates
a new session with:
- A new UUID
- All messages copied from the original
- Metadata copied, with `parent_id` set to the original session's ID

This enables branching conversations.

### Delete

`SessionManager::delete(id)` (`line 223`) removes a session and all its
messages. Returns `NotFound` if the session does not exist.

### Search

`SessionManager::search(query)` (`line 171`) performs full-text search over
message content using SQLite FTS5. Returns sessions containing matching
messages, ordered by message timestamp.

## SQLite Schema

Defined in `crates/ava-session/src/helpers.rs:5`:

### sessions Table

```sql
CREATE TABLE IF NOT EXISTS sessions (
    id TEXT PRIMARY KEY,
    created_at TEXT NOT NULL,
    updated_at TEXT NOT NULL,
    metadata TEXT NOT NULL,    -- JSON object
    parent_id TEXT             -- UUID of parent session (nullable)
);
```

### messages Table

```sql
CREATE TABLE IF NOT EXISTS messages (
    id TEXT PRIMARY KEY,
    session_id TEXT NOT NULL,
    role TEXT NOT NULL,         -- "system", "user", "assistant", "tool"
    content TEXT NOT NULL,
    timestamp TEXT NOT NULL,    -- RFC 3339
    tool_calls TEXT NOT NULL,   -- JSON array of ToolCall
    tool_results TEXT NOT NULL, -- JSON array of ToolResult
    FOREIGN KEY(session_id) REFERENCES sessions(id)
);
```

### FTS5 Index

```sql
CREATE VIRTUAL TABLE IF NOT EXISTS messages_fts USING fts5(
    content,
    content='messages',
    content_rowid='rowid'
);
```

Triggers automatically keep the FTS index in sync with the messages table:
- `messages_ai` -- After INSERT, adds content to FTS
- `messages_ad` -- After DELETE, removes content from FTS
- `messages_au` -- After UPDATE, removes old and adds new content

## Parent-Child Linking

Sessions support a parent-child relationship for sub-agent conversations:

### Setting Parent ID

When a sub-agent is spawned via the `task` tool, the sub-agent's session
gets `parent_id` set in its metadata
(`crates/ava-agent/src/stack.rs:633`):

```rust
session.metadata["is_sub_agent"] = Value::Bool(true);
if let Some(ref parent_id) = self.parent_session_id {
    session.metadata["parent_id"] = Value::String(parent_id.clone());
}
```

The TUI sets `AgentStack.parent_session_id` before calling `run()` so that
spawned sub-agents can record their lineage.

### Querying Children

`SessionManager::get_children(parent_id)` (`crates/ava-session/src/lib.rs:200`)
returns all sessions whose `parent_id` matches:

```sql
SELECT id FROM sessions WHERE parent_id = ?1 ORDER BY created_at ASC
```

## Session Metadata

The `metadata` field is a JSON object stored as text. Common fields:

| Key | Type | Description |
|---|---|---|
| `parent_id` | string | UUID of parent session (for sub-agents) |
| `is_sub_agent` | bool | Whether this session was a sub-agent run |
| `provider` | string | LLM provider name used for this session |
| `model` | string | Model name used for this session |

The `provider` and `model` fields enable model persistence: when resuming a
session, the TUI restores the model from session metadata.

## Auto-Naming

Sessions are automatically titled based on the first user message via
`generate_title()` (`crates/ava-session/src/lib.rs:35`):

- If the message starts with `/`, the command name is used (e.g., "/help")
- The first line of the message is taken
- Truncated to ~50 characters at a word boundary
- Appends "..." if truncated

Examples:
- `"Fix the login bug"` -> `"Fix the login bug"`
- `"Implement a comprehensive user authentication system with OAuth2 support and JWT tokens"` -> `"Implement a comprehensive user authentication system..."`
- `""` -> `"Untitled session"`
- `"/model openai/gpt-4"` -> `"/model openai/gpt-4"`

## Token Usage in Sessions

The `Session` struct includes `token_usage: TokenUsage` for tracking cumulative
token consumption. The `AgentLoop::run()` method sets this before returning
(`crates/ava-agent/src/agent_loop/mod.rs:258`):

```rust
session.token_usage = total_usage;
```

This enables post-run cost analysis and budget tracking.

## Database Location

The database file is located at `~/.ava/data.db` by default. This path is
derived from `AgentStackConfig.data_dir` (`crates/ava-agent/src/stack.rs:145`):

```rust
let db_path = config.data_dir.join("data.db");
```

Both sessions and memory share the same SQLite database file.

## TUI Session Features

The TUI provides session management via:

- **Session picker**: `Ctrl+L` or `/sessions` command opens a list of recent
  sessions with titles, message counts, and dates
- **Session loading**: Selecting a session restores its messages and model
  configuration
- **New session**: Available as the first option in the session picker

Session state is managed in `crates/ava-tui/src/state/messages.rs` and
`crates/ava-tui/src/widgets/session_list.rs`.

## Key Files

| File | Role |
|---|---|
| `crates/ava-session/src/lib.rs` | `SessionManager` -- CRUD, search, fork, children |
| `crates/ava-session/src/helpers.rs` | Schema SQL, role conversion, UUID/datetime parsing |
| `crates/ava-types/src/session.rs` | `Session` struct definition |
| `crates/ava-types/src/message.rs` | `Message` struct, `Role` enum |
| `crates/ava-agent/src/stack.rs` | Parent session ID management, sub-agent session persistence |
| `crates/ava-tui/src/widgets/session_list.rs` | Session picker UI |
