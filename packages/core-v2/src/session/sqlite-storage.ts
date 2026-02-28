/**
 * SQLite session storage — persists sessions to a database.
 */

import type { IDatabase } from '../platform.js'
import type { SerializedSession, SessionStorage } from './storage.js'
import { deserializeSession, serializeSession } from './storage.js'
import type { SessionState } from './types.js'

const CREATE_TABLE = `
  CREATE TABLE IF NOT EXISTS sessions_v2 (
    id TEXT PRIMARY KEY,
    name TEXT,
    data TEXT NOT NULL,
    updated_at INTEGER NOT NULL
  )
`

const CREATE_INDEX = `
  CREATE INDEX IF NOT EXISTS idx_sessions_v2_updated
  ON sessions_v2(updated_at DESC)
`

export class SqliteSessionStorage implements SessionStorage {
  private initialized = false

  constructor(private db: IDatabase) {}

  private async init(): Promise<void> {
    if (this.initialized) return
    await this.db.execute(CREATE_TABLE)
    await this.db.execute(CREATE_INDEX)
    this.initialized = true
  }

  async save(session: SessionState): Promise<void> {
    await this.init()
    const serialized = serializeSession(session)
    const data = JSON.stringify(serialized)
    await this.db.execute(
      `INSERT OR REPLACE INTO sessions_v2 (id, name, data, updated_at) VALUES (?, ?, ?, ?)`,
      [session.id, session.name ?? null, data, session.updatedAt]
    )
  }

  async load(id: string): Promise<SessionState | null> {
    await this.init()
    const rows = await this.db.query<{ data: string }>(
      'SELECT data FROM sessions_v2 WHERE id = ?',
      [id]
    )
    if (rows.length === 0) return null
    return deserializeSession(JSON.parse(rows[0]!.data) as SerializedSession)
  }

  async delete(id: string): Promise<boolean> {
    await this.init()
    const rows = await this.db.query<{ id: string }>('SELECT id FROM sessions_v2 WHERE id = ?', [
      id,
    ])
    if (rows.length === 0) return false
    await this.db.execute('DELETE FROM sessions_v2 WHERE id = ?', [id])
    return true
  }

  async list(): Promise<Array<{ id: string; name?: string; updatedAt: number }>> {
    await this.init()
    const rows = await this.db.query<{ id: string; name: string | null; updated_at: number }>(
      'SELECT id, name, updated_at FROM sessions_v2 ORDER BY updated_at DESC'
    )
    return rows.map((r) => ({
      id: r.id,
      name: r.name ?? undefined,
      updatedAt: r.updated_at,
    }))
  }

  async loadAll(): Promise<SessionState[]> {
    await this.init()
    const rows = await this.db.query<{ data: string }>(
      'SELECT data FROM sessions_v2 ORDER BY updated_at DESC'
    )
    return rows.map((r) => deserializeSession(JSON.parse(r.data) as SerializedSession))
  }
}
