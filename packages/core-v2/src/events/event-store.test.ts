import { describe, expect, it } from 'vitest'
import type { IDatabase } from '../platform.js'
import { EventStore } from './event-store.js'

function createMockDatabase(): IDatabase {
  const rows: Array<Record<string, unknown>> = []

  return {
    async query<T>(sql: string, params?: unknown[]): Promise<T[]> {
      if (sql.includes('SELECT') && sql.includes('WHERE session_id')) {
        const sessionId = params?.[0]
        return rows.filter((row) => row.session_id === sessionId) as T[]
      }
      return rows as T[]
    },
    async execute(sql: string, params?: unknown[]): Promise<void> {
      if (sql.includes('INSERT INTO event_log') && params) {
        const [id, session_id, timestamp, type, payload, parent_event_id] = params
        rows.push({ id, session_id, timestamp, type, payload, parent_event_id })
      }
    },
    async migrate(): Promise<void> {},
    async close(): Promise<void> {},
  }
}

describe('EventStore', () => {
  it('appends and retrieves events by session', () => {
    const store = new EventStore(createMockDatabase())
    store.append({ sessionId: 's1', type: 'tool:before', payload: { tool: 'read' } })

    const sessionEvents = store.getSession('s1')
    expect(sessionEvents).toHaveLength(1)
    expect(sessionEvents[0]?.type).toBe('tool:before')
  })

  it('keeps sessions isolated', () => {
    const store = new EventStore(createMockDatabase())
    store.append({ sessionId: 's1', type: 'agent:start', payload: {} })
    store.append({ sessionId: 's2', type: 'agent:start', payload: {} })

    expect(store.getSession('s1')).toHaveLength(1)
    expect(store.getSession('s2')).toHaveLength(1)
  })

  it('returns events in a time range', () => {
    const store = new EventStore(createMockDatabase())
    const first = store.append({ sessionId: 's1', type: 'a', payload: {} })
    const second = store.append({ sessionId: 's1', type: 'b', payload: {} })

    const ranged = store.getRange('s1', first.timestamp, second.timestamp)

    expect(ranged.length).toBeGreaterThanOrEqual(1)
    expect(ranged.some((event) => event.type === 'b')).toBe(true)
  })

  it('exports replayable JSON', () => {
    const store = new EventStore(createMockDatabase())
    store.append({ sessionId: 's1', type: 'agent:finish', payload: { ok: true } })

    const exported = store.export('s1')
    const parsed = JSON.parse(exported) as Array<{ type: string }>

    expect(Array.isArray(parsed)).toBe(true)
    expect(parsed[0]?.type).toBe('agent:finish')
  })

  it('handles high-volume append under one second', () => {
    const store = new EventStore(createMockDatabase())
    const start = Date.now()

    for (let i = 0; i < 1000; i += 1) {
      store.append({ sessionId: 'bulk', type: 'tick', payload: { i } })
    }

    const elapsed = Date.now() - start
    expect(store.getSession('bulk')).toHaveLength(1000)
    expect(elapsed).toBeLessThan(1000)
  })
})
