/**
 * Node.js Database Implementation
 * Uses better-sqlite3 for SQLite
 */

import * as fs from 'node:fs'
import * as path from 'node:path'
import type { IDatabase, Migration } from '@estela/core'
import BetterSqlite3 from 'better-sqlite3'

export class NodeDatabase implements IDatabase {
  private db: BetterSqlite3.Database | null = null

  constructor(private dbPath: string) {}

  private getDb(): BetterSqlite3.Database {
    if (!this.db) {
      // Ensure directory exists
      const dir = path.dirname(this.dbPath)
      if (!fs.existsSync(dir)) {
        fs.mkdirSync(dir, { recursive: true })
      }
      this.db = new BetterSqlite3(this.dbPath)
    }
    return this.db
  }

  async query<T>(sql: string, params?: unknown[]): Promise<T[]> {
    const db = this.getDb()
    const stmt = db.prepare(sql)
    return stmt.all(...(params ?? [])) as T[]
  }

  async execute(sql: string, params?: unknown[]): Promise<void> {
    const db = this.getDb()
    const stmt = db.prepare(sql)
    stmt.run(...(params ?? []))
  }

  async migrate(migrations: Migration[]): Promise<void> {
    const db = this.getDb()

    // Create migrations table
    db.exec(`
      CREATE TABLE IF NOT EXISTS _migrations (
        version INTEGER PRIMARY KEY,
        name TEXT NOT NULL,
        applied_at INTEGER NOT NULL
      )
    `)

    // Get applied migrations
    const applied = db.prepare('SELECT version FROM _migrations ORDER BY version').all() as {
      version: number
    }[]
    const appliedVersions = new Set(applied.map((m) => m.version))

    // Apply pending migrations in a transaction
    const applyMigration = db.transaction((migration: Migration) => {
      db.exec(migration.up)
      db.prepare('INSERT INTO _migrations (version, name, applied_at) VALUES (?, ?, ?)').run(
        migration.version,
        migration.name,
        Date.now()
      )
    })

    for (const migration of migrations.sort((a, b) => a.version - b.version)) {
      if (!appliedVersions.has(migration.version)) {
        applyMigration(migration)
      }
    }
  }

  async close(): Promise<void> {
    if (this.db) {
      this.db.close()
      this.db = null
    }
  }
}
