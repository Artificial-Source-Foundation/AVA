import type { ExtensionStorage } from '@ava/core-v2/extensions'
import { describe, expect, it } from 'vitest'
import { MemoryStore } from './store.js'

function createTestStorage(): ExtensionStorage {
  const data = new Map<string, unknown>()
  return {
    async get<T>(key: string): Promise<T | null> {
      return (data.get(key) as T) ?? null
    },
    async set<T>(key: string, value: T): Promise<void> {
      data.set(key, value)
    },
    async delete(key: string): Promise<void> {
      data.delete(key)
    },
    async keys(): Promise<string[]> {
      return [...data.keys()]
    },
  }
}

describe('MemoryStore', () => {
  it('writes and reads a memory', async () => {
    const store = new MemoryStore(createTestStorage())
    await store.write('test-key', 'test-value', 'project')
    const entry = await store.read('test-key')
    expect(entry).not.toBeNull()
    expect(entry!.key).toBe('test-key')
    expect(entry!.value).toBe('test-value')
    expect(entry!.category).toBe('project')
  })

  it('updates existing memory', async () => {
    const store = new MemoryStore(createTestStorage())
    await store.write('key', 'v1')
    await store.write('key', 'v2')
    const entry = await store.read('key')
    expect(entry!.value).toBe('v2')
    expect(entry!.updatedAt).toBeGreaterThanOrEqual(entry!.createdAt)
  })

  it('returns null for missing key', async () => {
    const store = new MemoryStore(createTestStorage())
    expect(await store.read('nope')).toBeNull()
  })

  it('lists all memories', async () => {
    const store = new MemoryStore(createTestStorage())
    await store.write('a', 'val-a', 'project')
    await store.write('b', 'val-b', 'debug')
    const all = await store.list()
    expect(all).toHaveLength(2)
  })

  it('filters by category', async () => {
    const store = new MemoryStore(createTestStorage())
    await store.write('a', 'val-a', 'project')
    await store.write('b', 'val-b', 'debug')
    const debug = await store.list('debug')
    expect(debug).toHaveLength(1)
    expect(debug[0]!.key).toBe('b')
  })

  it('deletes a memory', async () => {
    const store = new MemoryStore(createTestStorage())
    await store.write('key', 'val')
    expect(await store.remove('key')).toBe(true)
    expect(await store.read('key')).toBeNull()
    expect(await store.remove('key')).toBe(false)
  })

  it('builds prompt section', async () => {
    const store = new MemoryStore(createTestStorage())
    await store.write('pattern', 'use PascalCase', 'project')
    await store.write('theme', 'dark mode', 'preferences')
    const section = await store.buildPromptSection()
    expect(section).toContain('<memories>')
    expect(section).toContain('pattern: use PascalCase')
    expect(section).toContain('theme: dark mode')
    expect(section).toContain('</memories>')
  })

  it('returns empty string for no memories', async () => {
    const store = new MemoryStore(createTestStorage())
    const section = await store.buildPromptSection()
    expect(section).toBe('')
  })

  it('persists across store instances with same storage', async () => {
    const storage = createTestStorage()
    const store1 = new MemoryStore(storage)
    await store1.write('key', 'persisted')

    const store2 = new MemoryStore(storage)
    const entry = await store2.read('key')
    expect(entry!.value).toBe('persisted')
  })
})
