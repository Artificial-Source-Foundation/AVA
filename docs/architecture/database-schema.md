# Database Schema

> SQLite schema for the application

---

## Tables

### sessions

Tracks coding sessions and their state.

```sql
CREATE TABLE sessions (
  id TEXT PRIMARY KEY,
  project_path TEXT NOT NULL,
  created_at DATETIME DEFAULT CURRENT_TIMESTAMP,
  updated_at DATETIME DEFAULT CURRENT_TIMESTAMP,
  commander_context TEXT,      -- JSON blob of commander's state
  status TEXT DEFAULT 'active' -- active, paused, completed
);
```

### messages

Stores conversation history.

```sql
CREATE TABLE messages (
  id TEXT PRIMARY KEY,
  session_id TEXT NOT NULL,
  agent_id TEXT,
  role TEXT NOT NULL,          -- user, assistant, system, tool
  content TEXT NOT NULL,
  tool_calls TEXT,             -- JSON array
  tokens_used INTEGER,
  created_at DATETIME DEFAULT CURRENT_TIMESTAMP,
  FOREIGN KEY (session_id) REFERENCES sessions(id)
);
```

### agents

Tracks spawned agents and their state.

```sql
CREATE TABLE agents (
  id TEXT PRIMARY KEY,
  session_id TEXT NOT NULL,
  type TEXT NOT NULL,          -- commander, operator, validator
  model TEXT NOT NULL,
  assigned_files TEXT,         -- JSON array
  status TEXT DEFAULT 'idle',
  tokens_used INTEGER DEFAULT 0,
  created_at DATETIME DEFAULT CURRENT_TIMESTAMP,
  FOREIGN KEY (session_id) REFERENCES sessions(id)
);
```

### file_changes

Tracks all file modifications for undo/history.

```sql
CREATE TABLE file_changes (
  id TEXT PRIMARY KEY,
  session_id TEXT NOT NULL,
  agent_id TEXT NOT NULL,
  file_path TEXT NOT NULL,
  change_type TEXT NOT NULL,   -- create, edit, delete
  old_content TEXT,
  new_content TEXT,
  created_at DATETIME DEFAULT CURRENT_TIMESTAMP,
  FOREIGN KEY (session_id) REFERENCES sessions(id),
  FOREIGN KEY (agent_id) REFERENCES agents(id)
);
```

### documentation

Stores auto-generated documentation for context management.

```sql
CREATE TABLE documentation (
  id TEXT PRIMARY KEY,
  session_id TEXT NOT NULL,
  doc_type TEXT NOT NULL,      -- architecture, file_map, session_summary
  content TEXT NOT NULL,
  updated_at DATETIME DEFAULT CURRENT_TIMESTAMP,
  FOREIGN KEY (session_id) REFERENCES sessions(id)
);
```

---

## Indexes

```sql
-- Fast session lookups
CREATE INDEX idx_messages_session ON messages(session_id);
CREATE INDEX idx_agents_session ON agents(session_id);
CREATE INDEX idx_file_changes_session ON file_changes(session_id);

-- Fast file change lookups
CREATE INDEX idx_file_changes_path ON file_changes(file_path);
```
