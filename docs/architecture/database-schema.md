# Database Schema

> SQLite schema managed by migrations system

---

## Schema Version

Current: **v1**

Migrations are tracked in `schema_version` table and run automatically on app start.

---

## Tables

### schema_version

Tracks applied migrations.

```sql
CREATE TABLE schema_version (
  version INTEGER PRIMARY KEY,
  applied_at INTEGER NOT NULL
);
```

### sessions

Stores chat sessions.

```sql
CREATE TABLE sessions (
  id TEXT PRIMARY KEY,
  name TEXT NOT NULL,
  created_at INTEGER NOT NULL,
  updated_at INTEGER NOT NULL,
  status TEXT NOT NULL DEFAULT 'active',
  metadata TEXT
);

CREATE INDEX idx_sessions_updated ON sessions(updated_at DESC);
CREATE INDEX idx_sessions_status ON sessions(status);
```

| Column | Type | Description |
|--------|------|-------------|
| id | TEXT | UUID primary key |
| name | TEXT | Session display name |
| created_at | INTEGER | Unix timestamp |
| updated_at | INTEGER | Unix timestamp |
| status | TEXT | 'active' or 'archived' |
| metadata | TEXT | JSON blob for extra data |

### messages

Stores conversation messages.

```sql
CREATE TABLE messages (
  id TEXT PRIMARY KEY,
  session_id TEXT NOT NULL,
  role TEXT NOT NULL,
  content TEXT NOT NULL,
  tokens_used INTEGER,
  model TEXT,
  metadata TEXT,
  created_at INTEGER NOT NULL,
  FOREIGN KEY (session_id) REFERENCES sessions(id) ON DELETE CASCADE
);

CREATE INDEX idx_messages_session ON messages(session_id);
CREATE INDEX idx_messages_created ON messages(session_id, created_at);
```

| Column | Type | Description |
|--------|------|-------------|
| id | TEXT | UUID primary key |
| session_id | TEXT | FK to sessions |
| role | TEXT | 'user', 'assistant', 'system' |
| content | TEXT | Message text |
| tokens_used | INTEGER | Token count (assistant only) |
| model | TEXT | Model used (assistant only) |
| metadata | TEXT | JSON blob (edited_at, error, etc.) |
| created_at | INTEGER | Unix timestamp |

### agents

Stores agent instances (for future multi-agent support).

```sql
CREATE TABLE agents (
  id TEXT PRIMARY KEY,
  session_id TEXT NOT NULL,
  type TEXT NOT NULL,
  model TEXT NOT NULL,
  status TEXT NOT NULL DEFAULT 'idle',
  assigned_files TEXT,
  tokens_used INTEGER DEFAULT 0,
  created_at INTEGER NOT NULL,
  FOREIGN KEY (session_id) REFERENCES sessions(id) ON DELETE CASCADE
);

CREATE INDEX idx_agents_session ON agents(session_id);
```

| Column | Type | Description |
|--------|------|-------------|
| id | TEXT | UUID primary key |
| session_id | TEXT | FK to sessions |
| type | TEXT | 'commander', 'operator', 'validator' |
| model | TEXT | LLM model identifier |
| status | TEXT | 'idle', 'working', 'completed', 'failed' |
| assigned_files | TEXT | JSON array of file paths |
| tokens_used | INTEGER | Total tokens consumed |
| created_at | INTEGER | Unix timestamp |

### file_changes

Tracks file modifications (for future undo/history).

```sql
CREATE TABLE file_changes (
  id TEXT PRIMARY KEY,
  session_id TEXT NOT NULL,
  agent_id TEXT,
  file_path TEXT NOT NULL,
  change_type TEXT NOT NULL,
  old_content TEXT,
  new_content TEXT,
  created_at INTEGER NOT NULL,
  FOREIGN KEY (session_id) REFERENCES sessions(id) ON DELETE CASCADE,
  FOREIGN KEY (agent_id) REFERENCES agents(id) ON DELETE SET NULL
);

CREATE INDEX idx_file_changes_session ON file_changes(session_id);
CREATE INDEX idx_file_changes_path ON file_changes(file_path);
```

| Column | Type | Description |
|--------|------|-------------|
| id | TEXT | UUID primary key |
| session_id | TEXT | FK to sessions |
| agent_id | TEXT | FK to agents (nullable) |
| file_path | TEXT | Absolute file path |
| change_type | TEXT | 'create', 'edit', 'delete' |
| old_content | TEXT | Previous content |
| new_content | TEXT | New content |
| created_at | INTEGER | Unix timestamp |

---

## Relationships

```
sessions (1) ──────< (many) messages
    │
    └──────────────< (many) agents
    │
    └──────────────< (many) file_changes
                              │
agents (1) ────────< (many) ──┘
```

---

## Migration System

Located in `src/services/migrations.ts`:

```typescript
const SCHEMA_VERSION = 1;

export async function runMigrations(db: Database): Promise<void> {
  // 1. Create schema_version table if not exists
  // 2. Get current version
  // 3. Run pending migrations in order
  // 4. Update version
}

async function migrateV1(db: Database): Promise<void> {
  // Creates all tables and indexes
}
```

### Adding Migrations

1. Increment `SCHEMA_VERSION`
2. Add `migrateVN()` function
3. Add case to migration switch

```typescript
const SCHEMA_VERSION = 2;

async function migrateV2(db: Database): Promise<void> {
  await db.execute(`
    ALTER TABLE sessions ADD COLUMN pinned INTEGER DEFAULT 0
  `);
}
```

---

## Query Patterns

### Get sessions with stats

```sql
SELECT
  s.*,
  COUNT(m.id) as message_count,
  COALESCE(SUM(m.tokens_used), 0) as total_tokens,
  (SELECT content FROM messages
   WHERE session_id = s.id
   ORDER BY created_at DESC LIMIT 1) as last_preview
FROM sessions s
LEFT JOIN messages m ON m.session_id = s.id
WHERE s.status != 'archived'
GROUP BY s.id
ORDER BY s.updated_at DESC
```

### Cascade delete

Sessions use `ON DELETE CASCADE` for messages, agents, and file_changes.
Deleting a session automatically removes all related records.
