/**
 * Database Initialization
 *
 * Singleton connection management for the SQLite database.
 * All other db-* modules import initDatabase() from here.
 *
 * In web mode (non-Tauri), uses a fallback adapter that routes
 * operations through the AVA HTTP API.
 */

import { isTauri } from '@tauri-apps/api/core'
import { createWebDatabase } from './db-web-fallback'

// Minimal interface matching what db-* modules use from @tauri-apps/plugin-sql
interface DatabaseLike {
  select<T>(query: string, params?: unknown[]): Promise<T>
  execute(query: string, params?: unknown[]): Promise<{ rowsAffected: number }>
}

let db: DatabaseLike | null = null

/**
 * Initialize database connection and run migrations.
 * In Tauri mode, uses the SQL plugin with SQLite.
 * In web mode, returns a fallback that routes through HTTP API.
 */
export async function initDatabase(): Promise<DatabaseLike> {
  if (db) return db

  if (isTauri()) {
    // Dynamic import to avoid bundling the Tauri SQL plugin in web builds
    const { default: Database } = await import('@tauri-apps/plugin-sql')
    const { runMigrations } = await import('./migrations')
    const tauriDb = await Database.load('sqlite:ava.db')
    await runMigrations(tauriDb)
    db = tauriDb
  } else {
    // Web mode: route DB operations through the HTTP API
    db = createWebDatabase()
  }

  return db
}

/**
 * Get the raw database instance (for adapters like DesktopSessionStorage).
 * Initializes if not already done.
 */
export async function getDb(): Promise<DatabaseLike> {
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
