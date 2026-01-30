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

  // Add future migrations here:
  // if (currentVersion < 2) { await migrateV2(db); await recordMigration(db, 2); }
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
