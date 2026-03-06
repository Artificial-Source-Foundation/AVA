import { randomUUID } from 'node:crypto'
import { getPlatform, type IDatabase } from '../platform.js'
import { dispatchCompute } from '../platform-dispatch.js'

export interface StoredEvent {
  id: string
  sessionId: string
  timestamp: number
  type: string
  payload: Record<string, unknown>
  parentEventId?: string
}

export class EventStore {
  private readonly eventsBySession = new Map<string, StoredEvent[]>()
  private initialized = false

  constructor(private readonly db?: IDatabase) {}

  append(event: Omit<StoredEvent, 'id' | 'timestamp'>): StoredEvent {
    const stored: StoredEvent = {
      ...event,
      id: randomUUID(),
      timestamp: Date.now(),
    }

    const current = this.eventsBySession.get(stored.sessionId)
    if (current) {
      current.push(stored)
    } else {
      this.eventsBySession.set(stored.sessionId, [stored])
    }

    void this.persist(stored)
    return stored
  }

  getSession(sessionId: string): StoredEvent[] {
    return [...(this.eventsBySession.get(sessionId) ?? [])]
  }

  getByType(sessionId: string, type: string): StoredEvent[] {
    return this.getSession(sessionId).filter((event) => event.type === type)
  }

  getRange(sessionId: string, start: number, end: number): StoredEvent[] {
    return this.getSession(sessionId).filter(
      (event) => event.timestamp >= start && event.timestamp <= end
    )
  }

  export(sessionId: string): string {
    return JSON.stringify(this.getSession(sessionId))
  }

  private async persist(event: StoredEvent): Promise<void> {
    await this.ensureInitialized()

    await dispatchCompute(
      'event_store_append',
      {
        id: event.id,
        sessionId: event.sessionId,
        timestamp: event.timestamp,
        type: event.type,
        payload: JSON.stringify(event.payload),
        parentEventId: event.parentEventId ?? null,
      },
      async () => {
        await this.getDatabase().execute(
          `INSERT INTO event_log (id, session_id, timestamp, type, payload, parent_event_id)
           VALUES (?, ?, ?, ?, ?, ?)`,
          [
            event.id,
            event.sessionId,
            event.timestamp,
            event.type,
            JSON.stringify(event.payload),
            event.parentEventId ?? null,
          ]
        )
        return { ok: true }
      }
    )
  }

  private async ensureInitialized(): Promise<void> {
    if (this.initialized) return

    await dispatchCompute('event_store_init', {}, async () => {
      await this.getDatabase().execute(
        `CREATE TABLE IF NOT EXISTS event_log (
          id TEXT PRIMARY KEY,
          session_id TEXT NOT NULL,
          timestamp INTEGER NOT NULL,
          type TEXT NOT NULL,
          payload TEXT NOT NULL,
          parent_event_id TEXT
        )`
      )
      await this.getDatabase().execute(
        'CREATE INDEX IF NOT EXISTS idx_event_log_session_time ON event_log(session_id, timestamp)'
      )
      return { ok: true }
    })

    this.initialized = true
  }

  private getDatabase(): IDatabase {
    return this.db ?? getPlatform().database
  }
}
