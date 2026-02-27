/**
 * Tauri Database Implementation
 * Uses tauri-plugin-sql for SQLite
 */

import type { IDatabase, Migration } from '@ava/core-v2'
import Database from '@tauri-apps/plugin-sql'

export class TauriDatabase implements IDatabase {
  private db: Database | null = null

  constructor(private dbPath: string) {}

  private async getDb(): Promise<Database> {
    if (!this.db) {
      this.db = await Database.load(`sqlite:${this.dbPath}`)
    }
    return this.db
  }

  async query<T>(sql: string, params?: unknown[]): Promise<T[]> {
    const db = await this.getDb()
    return db.select<T[]>(sql, params)
  }

  async execute(sql: string, params?: unknown[]): Promise<void> {
    const db = await this.getDb()
    await db.execute(sql, params)
  }

  async migrate(migrations: Migration[]): Promise<void> {
    const db = await this.getDb()

    // Create migrations table
    await db.execute(`
      CREATE TABLE IF NOT EXISTS _migrations (
        version INTEGER PRIMARY KEY,
        name TEXT NOT NULL,
        applied_at INTEGER NOT NULL
      )
    `)

    // Get applied migrations
    const applied = await db.select<{ version: number }[]>(
      'SELECT version FROM _migrations ORDER BY version'
    )
    const appliedVersions = new Set(applied.map((m) => m.version))

    // Apply pending migrations
    for (const migration of migrations.sort((a, b) => a.version - b.version)) {
      if (!appliedVersions.has(migration.version)) {
        await db.execute(migration.up)
        await db.execute('INSERT INTO _migrations (version, name, applied_at) VALUES (?, ?, ?)', [
          migration.version,
          migration.name,
          Date.now(),
        ])
      }
    }
  }

  async close(): Promise<void> {
    if (this.db) {
      await this.db.close()
      this.db = null
    }
  }
}
