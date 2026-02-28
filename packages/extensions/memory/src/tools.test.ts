import type { ExtensionStorage } from '@ava/core-v2/extensions'
import { describe, expect, it } from 'vitest'
import { MemoryStore } from './store.js'
import { createMemoryTools } from './tools.js'

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

describe('memory tools', () => {
  it('creates 4 tools', () => {
    const store = new MemoryStore(createTestStorage())
    const tools = createMemoryTools(store)
    expect(tools).toHaveLength(4)
    expect(tools.map((t) => t.definition.name)).toEqual([
      'memory_write',
      'memory_read',
      'memory_list',
      'memory_delete',
    ])
  })

  it('memory_write saves and returns success', async () => {
    const store = new MemoryStore(createTestStorage())
    const tools = createMemoryTools(store)
    const write = tools.find((t) => t.definition.name === 'memory_write')!
    const result = await write.execute({ key: 'test', value: 'hello', category: 'debug' })
    expect(result.success).toBe(true)
    expect(result.output).toContain('test')
  })

  it('memory_read returns stored value', async () => {
    const store = new MemoryStore(createTestStorage())
    const tools = createMemoryTools(store)
    const write = tools.find((t) => t.definition.name === 'memory_write')!
    const read = tools.find((t) => t.definition.name === 'memory_read')!

    await write.execute({ key: 'foo', value: 'bar' })
    const result = await read.execute({ key: 'foo' })
    expect(result.success).toBe(true)
    expect(result.output).toContain('bar')
  })

  it('memory_read returns failure for missing key', async () => {
    const store = new MemoryStore(createTestStorage())
    const tools = createMemoryTools(store)
    const read = tools.find((t) => t.definition.name === 'memory_read')!
    const result = await read.execute({ key: 'missing' })
    expect(result.success).toBe(false)
  })

  it('memory_list returns all entries', async () => {
    const store = new MemoryStore(createTestStorage())
    const tools = createMemoryTools(store)
    const write = tools.find((t) => t.definition.name === 'memory_write')!
    const list = tools.find((t) => t.definition.name === 'memory_list')!

    await write.execute({ key: 'a', value: 'v1' })
    await write.execute({ key: 'b', value: 'v2' })
    const result = await list.execute({})
    expect(result.success).toBe(true)
    expect(result.output).toContain('a:')
    expect(result.output).toContain('b:')
  })

  it('memory_delete removes entry', async () => {
    const store = new MemoryStore(createTestStorage())
    const tools = createMemoryTools(store)
    const write = tools.find((t) => t.definition.name === 'memory_write')!
    const del = tools.find((t) => t.definition.name === 'memory_delete')!
    const read = tools.find((t) => t.definition.name === 'memory_read')!

    await write.execute({ key: 'temp', value: 'data' })
    const result = await del.execute({ key: 'temp' })
    expect(result.success).toBe(true)

    const readResult = await read.execute({ key: 'temp' })
    expect(readResult.success).toBe(false)
  })

  it('memory_delete returns failure for missing key', async () => {
    const store = new MemoryStore(createTestStorage())
    const tools = createMemoryTools(store)
    const del = tools.find((t) => t.definition.name === 'memory_delete')!
    const result = await del.execute({ key: 'nope' })
    expect(result.success).toBe(false)
  })
})
