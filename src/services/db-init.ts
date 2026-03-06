/**
 * Database Initialization
 *
 * Singleton connection management for the SQLite database.
 * All other db-* modules import initDatabase() from here.
 */

import Database from '@tauri-apps/plugin-sql'
import { runMigrations } from './migrations'

let db: Database | null = null

/**
 * Initialize database connection and run migrations
 */
export async function initDatabase(): Promise<Database> {
  if (db) return db

  db = await Database.load('sqlite:ava.db')
  await runMigrations(db)

  return db
}

/**
 * Get the raw database instance (for adapters like DesktopSessionStorage).
 * Initializes if not already done.
 */
export async function getDb(): Promise<Database> {
  return initDatabase()
}

/**
 * Update session's updated_at timestamp.
 * Used by message operations to keep session freshness.
 */
export async function touchSession(id: string): Promise<void> {
  const database = await initDatabase()
  await database.execute('UPDATE sessions SET updated_at = ? WHERE id = ?', [Date.now(), id])
}
