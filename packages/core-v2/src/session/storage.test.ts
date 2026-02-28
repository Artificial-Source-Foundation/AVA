import { describe, expect, it } from 'vitest'
import { MemorySessionStorage } from './memory-storage.js'
import { deserializeSession, serializeSession } from './storage.js'
import type { SessionState } from './types.js'

function createTestSession(overrides?: Partial<SessionState>): SessionState {
  return {
    id: 'test-id',
    name: 'Test Session',
    messages: [{ role: 'user', content: 'hello' }],
    workingDirectory: '/tmp/test',
    toolCallCount: 3,
    tokenStats: {
      inputTokens: 100,
      outputTokens: 50,
      messages: new Map([
        ['msg1', 10],
        ['msg2', 20],
      ]),
    },
    openFiles: new Map([
      ['file.ts', { path: 'file.ts', content: 'code', mtime: 1000, dirty: false }],
    ]),
    env: { NODE_ENV: 'test' },
    createdAt: 1000,
    updatedAt: 2000,
    status: 'active',
    ...overrides,
  }
}

describe('serializeSession / deserializeSession', () => {
  it('round-trips a session correctly', () => {
    const session = createTestSession()
    const serialized = serializeSession(session)
    const deserialized = deserializeSession(serialized)

    expect(deserialized.id).toBe(session.id)
    expect(deserialized.name).toBe(session.name)
    expect(deserialized.messages).toEqual(session.messages)
    expect(deserialized.tokenStats.inputTokens).toBe(100)
    expect(deserialized.tokenStats.messages).toBeInstanceOf(Map)
    expect(deserialized.tokenStats.messages.get('msg1')).toBe(10)
    expect(deserialized.openFiles).toBeInstanceOf(Map)
    expect(deserialized.openFiles.size).toBe(1)
  })

  it('converts Maps to plain objects in serialized form', () => {
    const session = createTestSession()
    const serialized = serializeSession(session)

    expect(serialized.tokenStats.messages).toEqual({ msg1: 10, msg2: 20 })
    expect(typeof serialized.openFiles).toBe('object')
    expect(serialized.openFiles).not.toBeInstanceOf(Map)
  })
})

describe('MemorySessionStorage', () => {
  it('saves and loads a session', async () => {
    const storage = new MemorySessionStorage()
    const session = createTestSession()
    await storage.save(session)

    const loaded = await storage.load('test-id')
    expect(loaded).not.toBeNull()
    expect(loaded!.id).toBe('test-id')
    expect(loaded!.tokenStats.messages).toBeInstanceOf(Map)
  })

  it('returns null for missing session', async () => {
    const storage = new MemorySessionStorage()
    const loaded = await storage.load('nonexistent')
    expect(loaded).toBeNull()
  })

  it('deletes a session', async () => {
    const storage = new MemorySessionStorage()
    await storage.save(createTestSession())
    expect(await storage.delete('test-id')).toBe(true)
    expect(await storage.load('test-id')).toBeNull()
    expect(await storage.delete('test-id')).toBe(false)
  })

  it('lists sessions', async () => {
    const storage = new MemorySessionStorage()
    await storage.save(createTestSession({ id: 'a', name: 'A', updatedAt: 100 }))
    await storage.save(createTestSession({ id: 'b', name: 'B', updatedAt: 200 }))

    const list = await storage.list()
    expect(list).toHaveLength(2)
    expect(list.map((s) => s.id).sort()).toEqual(['a', 'b'])
  })

  it('loads all sessions', async () => {
    const storage = new MemorySessionStorage()
    await storage.save(createTestSession({ id: 'a' }))
    await storage.save(createTestSession({ id: 'b' }))

    const all = await storage.loadAll()
    expect(all).toHaveLength(2)
    expect(all[0]!.tokenStats.messages).toBeInstanceOf(Map)
  })
})
