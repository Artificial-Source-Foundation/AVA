/**
 * Database Migrations
 * Handles schema initialization and versioning.
 * Heavy DDL (V1–V3) lives in migration-schemas.ts.
 */

import type Database from '@tauri-apps/plugin-sql'
import { migrateV1, migrateV2, migrateV3 } from './migration-schemas'

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
    await db.execute(`
      CREATE TABLE schema_version (
        version INTEGER PRIMARY KEY,
        applied_at INTEGER NOT NULL
      )
    `)
  } else {
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

async function recordMigration(db: Database, version: number): Promise<void> {
  await db.execute('INSERT INTO schema_version (version, applied_at) VALUES (?, ?)', [
    version,
    Date.now(),
  ])
}

/**
 * Version 4: Add cost_usd and model columns to messages
 */
async function migrateV4(db: Database): Promise<void> {
  await db.execute('ALTER TABLE messages ADD COLUMN cost_usd REAL')
  await db.execute('ALTER TABLE messages ADD COLUMN model TEXT')
}

/**
 * Version 5: Add workflows table
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
 */
async function migrateV6(db: Database): Promise<void> {
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
