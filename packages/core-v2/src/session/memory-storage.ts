/**
 * In-memory session storage — default for tests and ephemeral sessions.
 */

import type { SessionStorage } from './storage.js'
import { deserializeSession, serializeSession } from './storage.js'
import type { SessionState } from './types.js'

export class MemorySessionStorage implements SessionStorage {
  private store = new Map<string, string>()

  async save(session: SessionState): Promise<void> {
    this.store.set(session.id, JSON.stringify(serializeSession(session)))
  }

  async load(id: string): Promise<SessionState | null> {
    const raw = this.store.get(id)
    if (!raw) return null
    return deserializeSession(JSON.parse(raw))
  }

  async delete(id: string): Promise<boolean> {
    return this.store.delete(id)
  }

  async list(): Promise<Array<{ id: string; name?: string; updatedAt: number }>> {
    const entries: Array<{ id: string; name?: string; updatedAt: number }> = []
    for (const raw of this.store.values()) {
      const data = JSON.parse(raw) as { id: string; name?: string; updatedAt: number }
      entries.push({ id: data.id, name: data.name, updatedAt: data.updatedAt })
    }
    return entries
  }

  async loadAll(): Promise<SessionState[]> {
    const sessions: SessionState[] = []
    for (const raw of this.store.values()) {
      sessions.push(deserializeSession(JSON.parse(raw)))
    }
    return sessions
  }

  get size(): number {
    return this.store.size
  }
}
