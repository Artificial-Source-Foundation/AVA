/**
 * Database Migrations
 * Handles schema initialization and versioning
 */

import type Database from '@tauri-apps/plugin-sql'

interface TableInfo {
  name: string
}

/**
 * Run all pending migrations
 */
export async function runMigrations(db: Database): Promise<void> {
  // Check if schema_version table exists
  const tables = await db.select<TableInfo[]>(
    "SELECT name FROM sqlite_master WHERE type='table' AND name='schema_version'"
  )

  let currentVersion = 0

  if (tables.length === 0) {
    // Create schema version table
    await db.execute(`
      CREATE TABLE schema_version (
        version INTEGER PRIMARY KEY,
        applied_at INTEGER NOT NULL
      )
    `)
  } else {
    // Get current version
    const versions = await db.select<{ version: number }[]>(
      'SELECT MAX(version) as version FROM schema_version'
    )
    currentVersion = versions[0]?.version || 0
  }

  // Run migrations sequentially
  if (currentVersion < 1) {
    await migrateV1(db)
    await recordMigration(db, 1)
  }

  if (currentVersion < 2) {
    await migrateV2(db)
    await recordMigration(db, 2)
  }

  if (currentVersion < 3) {
    await migrateV3(db)
    await recordMigration(db, 3)
  }

  if (currentVersion < 4) {
    await migrateV4(db)
    await recordMigration(db, 4)
  }

  if (currentVersion < 5) {
    await migrateV5(db)
    await recordMigration(db, 5)
  }

  if (currentVersion < 6) {
    await migrateV6(db)
    await recordMigration(db, 6)
  }

  if (currentVersion < 7) {
    await migrateV7(db)
    await recordMigration(db, 7)
  }
}

/**
 * Record that a migration was applied
 */
async function recordMigration(db: Database, version: number): Promise<void> {
  await db.execute('INSERT INTO schema_version (version, applied_at) VALUES (?, ?)', [
    version,
    Date.now(),
  ])
}

/**
 * Version 1: Initial schema
 * - sessions, messages, agents, file_changes tables
 */
async function migrateV1(db: Database): Promise<void> {
  // Sessions table
  await db.execute(`
    CREATE TABLE IF NOT EXISTS sessions (
      id TEXT PRIMARY KEY,
      name TEXT NOT NULL,
      created_at INTEGER NOT NULL,
      updated_at INTEGER NOT NULL,
      status TEXT NOT NULL DEFAULT 'active',
      metadata TEXT
    )
  `)

  // Messages table
  await db.execute(`
    CREATE TABLE IF NOT EXISTS messages (
      id TEXT PRIMARY KEY,
      session_id TEXT NOT NULL,
      role TEXT NOT NULL,
      content TEXT NOT NULL,
      agent_id TEXT,
      created_at INTEGER NOT NULL,
      tokens_used INTEGER DEFAULT 0,
      metadata TEXT,
      FOREIGN KEY (session_id) REFERENCES sessions(id) ON DELETE CASCADE
    )
  `)

  // Agents table
  await db.execute(`
    CREATE TABLE IF NOT EXISTS agents (
      id TEXT PRIMARY KEY,
      session_id TEXT NOT NULL,
      type TEXT NOT NULL,
      status TEXT NOT NULL DEFAULT 'idle',
      model TEXT NOT NULL,
      created_at INTEGER NOT NULL,
      completed_at INTEGER,
      assigned_files TEXT,
      task_description TEXT,
      result TEXT,
      FOREIGN KEY (session_id) REFERENCES sessions(id) ON DELETE CASCADE
    )
  `)

  // File changes table
  await db.execute(`
    CREATE TABLE IF NOT EXISTS file_changes (
      id TEXT PRIMARY KEY,
      session_id TEXT NOT NULL,
      agent_id TEXT NOT NULL,
      file_path TEXT NOT NULL,
      change_type TEXT NOT NULL,
      old_content TEXT,
      new_content TEXT,
      created_at INTEGER NOT NULL,
      reverted INTEGER DEFAULT 0,
      FOREIGN KEY (session_id) REFERENCES sessions(id) ON DELETE CASCADE,
      FOREIGN KEY (agent_id) REFERENCES agents(id) ON DELETE CASCADE
    )
  `)

  // Create indexes for common queries
  await db.execute('CREATE INDEX IF NOT EXISTS idx_messages_session ON messages(session_id)')
  await db.execute('CREATE INDEX IF NOT EXISTS idx_messages_created ON messages(created_at)')
  await db.execute('CREATE INDEX IF NOT EXISTS idx_agents_session ON agents(session_id)')
  await db.execute('CREATE INDEX IF NOT EXISTS idx_sessions_updated ON sessions(updated_at DESC)')
  await db.execute('CREATE INDEX IF NOT EXISTS idx_sessions_status ON sessions(status)')
}

/**
 * Version 2: Add projects table and link sessions
 * - projects table for workspace organization
 * - project_id foreign key on sessions
 * - Default project for orphan sessions
 */
async function migrateV2(db: Database): Promise<void> {
  // Create projects table
  await db.execute(`
    CREATE TABLE IF NOT EXISTS projects (
      id TEXT PRIMARY KEY,
      name TEXT NOT NULL,
      directory TEXT NOT NULL UNIQUE,
      icon TEXT,
      git_branch TEXT,
      git_root_commit TEXT,
      created_at INTEGER NOT NULL,
      updated_at INTEGER NOT NULL,
      last_opened_at INTEGER,
      is_favorite INTEGER DEFAULT 0
    )
  `)

  // Create indexes for projects
  await db.execute('CREATE INDEX IF NOT EXISTS idx_projects_directory ON projects(directory)')
  await db.execute('CREATE INDEX IF NOT EXISTS idx_projects_updated ON projects(updated_at DESC)')
  await db.execute(
    'CREATE INDEX IF NOT EXISTS idx_projects_last_opened ON projects(last_opened_at DESC)'
  )

  // Add project_id to sessions (nullable for existing sessions)
  await db.execute(
    'ALTER TABLE sessions ADD COLUMN project_id TEXT REFERENCES projects(id) ON DELETE SET NULL'
  )

  // Create index for session-project lookups
  await db.execute('CREATE INDEX IF NOT EXISTS idx_sessions_project ON sessions(project_id)')

  // Create default project for orphan sessions
  const defaultProjectId = 'default-project'
  const now = Date.now()
  await db.execute(
    `INSERT OR IGNORE INTO projects (id, name, directory, created_at, updated_at, last_opened_at)
     VALUES (?, ?, ?, ?, ?, ?)`,
    [defaultProjectId, 'Default Project', '~', now, now, now]
  )

  // Migrate existing sessions to default project
  await db.execute('UPDATE sessions SET project_id = ? WHERE project_id IS NULL', [
    defaultProjectId,
  ])
}

/**
 * Version 3: Add file_operations, terminal_executions, and memory_items tables
 * - Track file read/write/edit/delete operations
 * - Track terminal command executions with output
 * - Track context memory items for token management
 */
async function migrateV3(db: Database): Promise<void> {
  // File operations table
  await db.execute(`
    CREATE TABLE IF NOT EXISTS file_operations (
      id TEXT PRIMARY KEY,
      session_id TEXT NOT NULL,
      agent_id TEXT,
      agent_name TEXT,
      type TEXT NOT NULL,
      file_path TEXT NOT NULL,
      timestamp INTEGER NOT NULL,
      lines INTEGER,
      lines_added INTEGER,
      lines_removed INTEGER,
      is_new INTEGER DEFAULT 0,
      original_content TEXT,
      new_content TEXT,
      FOREIGN KEY (session_id) REFERENCES sessions(id) ON DELETE CASCADE
    )
  `)

  // Terminal executions table
  await db.execute(`
    CREATE TABLE IF NOT EXISTS terminal_executions (
      id TEXT PRIMARY KEY,
      session_id TEXT NOT NULL,
      agent_id TEXT,
      agent_name TEXT,
      command TEXT NOT NULL,
      output TEXT NOT NULL DEFAULT '',
      status TEXT NOT NULL DEFAULT 'running',
      exit_code INTEGER,
      started_at INTEGER NOT NULL,
      completed_at INTEGER,
      cwd TEXT,
      FOREIGN KEY (session_id) REFERENCES sessions(id) ON DELETE CASCADE
    )
  `)

  // Memory items table
  await db.execute(`
    CREATE TABLE IF NOT EXISTS memory_items (
      id TEXT PRIMARY KEY,
      session_id TEXT NOT NULL,
      type TEXT NOT NULL,
      title TEXT NOT NULL,
      preview TEXT NOT NULL,
      tokens INTEGER NOT NULL DEFAULT 0,
      created_at INTEGER NOT NULL,
      source TEXT,
      FOREIGN KEY (session_id) REFERENCES sessions(id) ON DELETE CASCADE
    )
  `)

  // Create indexes for efficient queries
  await db.execute(
    'CREATE INDEX IF NOT EXISTS idx_file_operations_session ON file_operations(session_id)'
  )
  await db.execute(
    'CREATE INDEX IF NOT EXISTS idx_file_operations_timestamp ON file_operations(timestamp DESC)'
  )
  await db.execute(
    'CREATE INDEX IF NOT EXISTS idx_terminal_executions_session ON terminal_executions(session_id)'
  )
  await db.execute(
    'CREATE INDEX IF NOT EXISTS idx_terminal_executions_started ON terminal_executions(started_at DESC)'
  )
  await db.execute(
    'CREATE INDEX IF NOT EXISTS idx_memory_items_session ON memory_items(session_id)'
  )
  await db.execute(
    'CREATE INDEX IF NOT EXISTS idx_memory_items_created ON memory_items(created_at DESC)'
  )
}

/**
 * Version 4: Add cost_usd and model columns to messages
 * - Track per-message cost for budget visibility
 * - Track which model generated each message
 */
async function migrateV4(db: Database): Promise<void> {
  await db.execute('ALTER TABLE messages ADD COLUMN cost_usd REAL')
  await db.execute('ALTER TABLE messages ADD COLUMN model TEXT')
}

/**
 * Version 5: Add workflows table
 * - Reusable workflow recipes extracted from sessions
 */
async function migrateV5(db: Database): Promise<void> {
  await db.execute(`
    CREATE TABLE IF NOT EXISTS workflows (
      id TEXT PRIMARY KEY,
      project_id TEXT,
      name TEXT NOT NULL,
      description TEXT NOT NULL DEFAULT '',
      tags TEXT,
      prompt TEXT NOT NULL,
      created_at INTEGER NOT NULL,
      updated_at INTEGER NOT NULL,
      usage_count INTEGER DEFAULT 0,
      source_session_id TEXT,
      FOREIGN KEY (project_id) REFERENCES projects(id) ON DELETE SET NULL
    )
  `)
  await db.execute('CREATE INDEX IF NOT EXISTS idx_workflows_project ON workflows(project_id)')
}

/**
 * Version 6: Add session columns for core-v2 integration
 * - parent_session_id for session forking/branching (fixes crash bug)
 * - slug for human-readable session identifiers
 * - busy_since for tracking active agent execution
 */
async function migrateV6(db: Database): Promise<void> {
  // parent_session_id may already exist if createSession() was called before migration
  // Use try/catch since ALTER TABLE ADD COLUMN fails if column exists
  try {
    await db.execute('ALTER TABLE sessions ADD COLUMN parent_session_id TEXT')
  } catch {
    // Column already exists — safe to ignore
  }
  await db.execute('ALTER TABLE sessions ADD COLUMN slug TEXT')
  await db.execute('ALTER TABLE sessions ADD COLUMN busy_since INTEGER')
  await db.execute('CREATE INDEX IF NOT EXISTS idx_sessions_slug ON sessions(slug)')
  await db.execute('CREATE INDEX IF NOT EXISTS idx_sessions_parent ON sessions(parent_session_id)')
}

/**
 * Version 7: Plugin tracking table
 * - Persistent plugin install state (replaces localStorage-only approach)
 */
async function migrateV7(db: Database): Promise<void> {
  await db.execute(`
    CREATE TABLE IF NOT EXISTS plugin_installs (
      name TEXT PRIMARY KEY,
      version TEXT NOT NULL,
      installed_at INTEGER NOT NULL,
      source TEXT,
      enabled INTEGER DEFAULT 1
    )
  `)
}
