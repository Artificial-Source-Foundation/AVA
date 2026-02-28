import { describe, expect, it, vi } from 'vitest'
import type { IDatabase } from '../platform.js'
import { SqliteSessionStorage } from './sqlite-storage.js'
import type { SessionState } from './types.js'

function createMockDatabase(): IDatabase {
  const tables = new Map<string, unknown[]>()

  return {
    async query<T>(sql: string, params?: unknown[]): Promise<T[]> {
      const rows = (tables.get('sessions_v2') as Array<Record<string, unknown>>) ?? []
      if (sql.includes('SELECT') && sql.includes('WHERE id')) {
        const id = params?.[0]
        const match = rows.filter((r) => r.id === id)
        return match as T[]
      }
      if (sql.includes('SELECT')) {
        return rows as T[]
      }
      return [] as T[]
    },
    async execute(sql: string, params?: unknown[]): Promise<void> {
      if (sql.includes('INSERT OR REPLACE')) {
        let rows = (tables.get('sessions_v2') as Array<Record<string, unknown>>) ?? []
        const [id, name, data, updated_at] = params as [string, string | null, string, number]
        rows = rows.filter((r) => r.id !== id)
        rows.push({ id, name, data, updated_at })
        tables.set('sessions_v2', rows)
      } else if (sql.includes('DELETE')) {
        let rows = (tables.get('sessions_v2') as Array<Record<string, unknown>>) ?? []
        const id = params?.[0]
        rows = rows.filter((r) => r.id !== id)
        tables.set('sessions_v2', rows)
      } else if (sql.includes('CREATE')) {
        if (!tables.has('sessions_v2')) tables.set('sessions_v2', [])
      }
    },
    async migrate(): Promise<void> {},
    async close(): Promise<void> {},
  }
}

function createTestSession(id = 'test-id'): SessionState {
  return {
    id,
    name: 'Test',
    messages: [],
    workingDirectory: '/tmp',
    toolCallCount: 0,
    tokenStats: { inputTokens: 0, outputTokens: 0, messages: new Map() },
    openFiles: new Map(),
    env: {},
    createdAt: 1000,
    updatedAt: 2000,
    status: 'active',
  }
}

describe('SqliteSessionStorage', () => {
  it('saves and loads a session', async () => {
    const db = createMockDatabase()
    const storage = new SqliteSessionStorage(db)

    await storage.save(createTestSession())
    const loaded = await storage.load('test-id')
    expect(loaded).not.toBeNull()
    expect(loaded!.id).toBe('test-id')
    expect(loaded!.tokenStats.messages).toBeInstanceOf(Map)
  })

  it('returns null for missing session', async () => {
    const db = createMockDatabase()
    const storage = new SqliteSessionStorage(db)
    const loaded = await storage.load('nope')
    expect(loaded).toBeNull()
  })

  it('deletes a session', async () => {
    const db = createMockDatabase()
    const storage = new SqliteSessionStorage(db)
    await storage.save(createTestSession())
    expect(await storage.delete('test-id')).toBe(true)
    expect(await storage.load('test-id')).toBeNull()
  })

  it('lists sessions', async () => {
    const db = createMockDatabase()
    const storage = new SqliteSessionStorage(db)
    await storage.save(createTestSession('a'))
    await storage.save(createTestSession('b'))

    const list = await storage.list()
    expect(list).toHaveLength(2)
  })

  it('loads all sessions', async () => {
    const db = createMockDatabase()
    const storage = new SqliteSessionStorage(db)
    await storage.save(createTestSession('a'))
    await storage.save(createTestSession('b'))

    const all = await storage.loadAll()
    expect(all).toHaveLength(2)
  })

  it('initializes table only once', async () => {
    const db = createMockDatabase()
    const executeSpy = vi.spyOn(db, 'execute')
    const storage = new SqliteSessionStorage(db)

    await storage.save(createTestSession('a'))
    await storage.save(createTestSession('b'))

    // CREATE TABLE + CREATE INDEX + 2 INSERTs = 4 calls
    const createCalls = executeSpy.mock.calls.filter(([sql]) => (sql as string).includes('CREATE'))
    expect(createCalls).toHaveLength(2) // table + index, only once
  })
})
