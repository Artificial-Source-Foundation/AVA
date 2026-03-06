/**
 * Migration Schemas V1–V3
 * Heavy DDL migrations for initial schema, projects, and tracking tables.
 */

import type Database from '@tauri-apps/plugin-sql'

/**
 * Version 1: Initial schema
 * - sessions, messages, agents, file_changes tables
 */
export async function migrateV1(db: Database): Promise<void> {
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

  await db.execute('CREATE INDEX IF NOT EXISTS idx_messages_session ON messages(session_id)')
  await db.execute('CREATE INDEX IF NOT EXISTS idx_messages_created ON messages(created_at)')
  await db.execute('CREATE INDEX IF NOT EXISTS idx_agents_session ON agents(session_id)')
  await db.execute('CREATE INDEX IF NOT EXISTS idx_sessions_updated ON sessions(updated_at DESC)')
  await db.execute('CREATE INDEX IF NOT EXISTS idx_sessions_status ON sessions(status)')
}

/**
 * Version 2: Add projects table and link sessions
 */
export async function migrateV2(db: Database): Promise<void> {
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

  await db.execute('CREATE INDEX IF NOT EXISTS idx_projects_directory ON projects(directory)')
  await db.execute('CREATE INDEX IF NOT EXISTS idx_projects_updated ON projects(updated_at DESC)')
  await db.execute(
    'CREATE INDEX IF NOT EXISTS idx_projects_last_opened ON projects(last_opened_at DESC)'
  )

  await db.execute(
    'ALTER TABLE sessions ADD COLUMN project_id TEXT REFERENCES projects(id) ON DELETE SET NULL'
  )
  await db.execute('CREATE INDEX IF NOT EXISTS idx_sessions_project ON sessions(project_id)')

  const defaultProjectId = 'default-project'
  const now = Date.now()
  await db.execute(
    `INSERT OR IGNORE INTO projects (id, name, directory, created_at, updated_at, last_opened_at)
     VALUES (?, ?, ?, ?, ?, ?)`,
    [defaultProjectId, 'Default Project', '~', now, now, now]
  )
  await db.execute('UPDATE sessions SET project_id = ? WHERE project_id IS NULL', [
    defaultProjectId,
  ])
}

/**
 * Version 3: Add file_operations, terminal_executions, and memory_items tables
 */
export async function migrateV3(db: Database): Promise<void> {
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
