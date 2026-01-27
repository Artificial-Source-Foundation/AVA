/**
 * Tests for Delta9 Storage Adapter
 */

import { describe, it, expect, beforeEach, afterEach } from 'vitest'
import { existsSync, mkdirSync, rmSync } from 'node:fs'
import { join } from 'node:path'
import { tmpdir } from 'node:os'
import {
  FileStorageAdapter,
  MemoryStorageAdapter,
  getStorageAdapter,
  clearStorageAdapter,
  createFileStorage,
  createMemoryStorage,
} from '../../src/lib/storage-adapter.js'

describe('FileStorageAdapter', () => {
  const testDir = join(tmpdir(), 'delta9-storage-test-' + Date.now())
  let adapter: FileStorageAdapter

  beforeEach(() => {
    mkdirSync(testDir, { recursive: true })
    adapter = new FileStorageAdapter({ baseDir: testDir })
  })

  afterEach(() => {
    if (existsSync(testDir)) {
      rmSync(testDir, { recursive: true, force: true })
    }
  })

  describe('write and read', () => {
    it('writes and reads JSON data', async () => {
      const data = { name: 'test', value: 42 }
      await adapter.write('config', data)

      const result = await adapter.read<typeof data>('config')
      expect(result).toEqual(data)
    })

    it('handles nested paths', async () => {
      const data = { nested: true }
      await adapter.write('subdir/config', data)

      const result = await adapter.read<typeof data>('subdir/config')
      expect(result).toEqual(data)
    })

    it('returns null for non-existent key', async () => {
      const result = await adapter.read('non-existent')
      expect(result).toBeNull()
    })

    it('overwrites existing data', async () => {
      await adapter.write('config', { version: 1 })
      await adapter.write('config', { version: 2 })

      const result = await adapter.read<{ version: number }>('config')
      expect(result?.version).toBe(2)
    })
  })

  describe('append', () => {
    it('appends to JSONL file', async () => {
      await adapter.append('events', { type: 'event1' })
      await adapter.append('events', { type: 'event2' })
      await adapter.append('events', { type: 'event3' })

      const result = await adapter.read<Array<{ type: string }>>('events')
      expect(result).toHaveLength(3)
      expect(result?.[0].type).toBe('event1')
      expect(result?.[2].type).toBe('event3')
    })

    it('creates file if not exists', async () => {
      await adapter.append('new-log', { line: 1 })

      const exists = await adapter.exists('new-log')
      expect(exists).toBe(true)
    })
  })

  describe('exists', () => {
    it('returns true for existing JSON file', async () => {
      await adapter.write('exists-test', { test: true })
      expect(await adapter.exists('exists-test')).toBe(true)
    })

    it('returns true for existing JSONL file', async () => {
      await adapter.append('exists-log', { line: 1 })
      expect(await adapter.exists('exists-log')).toBe(true)
    })

    it('returns false for non-existent key', async () => {
      expect(await adapter.exists('does-not-exist')).toBe(false)
    })
  })

  describe('delete', () => {
    it('deletes JSON file', async () => {
      await adapter.write('to-delete', { data: 'test' })
      expect(await adapter.exists('to-delete')).toBe(true)

      const result = await adapter.delete('to-delete')
      expect(result).toBe(true)
      expect(await adapter.exists('to-delete')).toBe(false)
    })

    it('deletes JSONL file', async () => {
      await adapter.append('log-to-delete', { line: 1 })
      expect(await adapter.exists('log-to-delete')).toBe(true)

      const result = await adapter.delete('log-to-delete')
      expect(result).toBe(true)
      expect(await adapter.exists('log-to-delete')).toBe(false)
    })

    it('returns false for non-existent key', async () => {
      const result = await adapter.delete('non-existent')
      expect(result).toBe(false)
    })
  })

  describe('list', () => {
    it('lists all keys with prefix', async () => {
      await adapter.write('data/a', { id: 'a' })
      await adapter.write('data/b', { id: 'b' })
      await adapter.write('other/c', { id: 'c' })

      const dataKeys = await adapter.list('data')
      expect(dataKeys).toHaveLength(2)
      expect(dataKeys).toContain('data/a')
      expect(dataKeys).toContain('data/b')
    })

    it('returns empty array for non-existent prefix', async () => {
      const keys = await adapter.list('non-existent')
      expect(keys).toEqual([])
    })
  })

  describe('readJsonl', () => {
    it('reads JSONL as array', async () => {
      await adapter.append('log', { n: 1 })
      await adapter.append('log', { n: 2 })
      await adapter.append('log', { n: 3 })

      const items = await adapter.readJsonl<{ n: number }>('log')
      expect(items).toHaveLength(3)
      expect(items.map((i) => i.n)).toEqual([1, 2, 3])
    })

    it('returns empty array for non-existent file', async () => {
      const items = await adapter.readJsonl('non-existent')
      expect(items).toEqual([])
    })
  })

  describe('readRaw', () => {
    it('reads raw file content', async () => {
      await adapter.write('raw-test', { key: 'value' })
      const raw = await adapter.readRaw('raw-test')

      expect(raw).toBeDefined()
      expect(raw).toContain('"key"')
      expect(raw).toContain('"value"')
    })
  })

  describe('writeRaw', () => {
    it('writes raw text content', async () => {
      await adapter.writeRaw('text-file', 'Hello, World!')
      const raw = await adapter.readRaw('text-file')

      expect(raw).toBe('Hello, World!')
    })
  })
})

describe('MemoryStorageAdapter', () => {
  let adapter: MemoryStorageAdapter

  beforeEach(() => {
    adapter = new MemoryStorageAdapter()
  })

  afterEach(() => {
    adapter.clear()
  })

  describe('write and read', () => {
    it('stores and retrieves data', async () => {
      const data = { test: true }
      await adapter.write('key', data)

      const result = await adapter.read('key')
      expect(result).toEqual(data)
    })

    it('returns null for missing key', async () => {
      const result = await adapter.read('missing')
      expect(result).toBeNull()
    })
  })

  describe('append', () => {
    it('appends to array', async () => {
      await adapter.append('log', { n: 1 })
      await adapter.append('log', { n: 2 })

      const result = await adapter.read<Array<{ n: number }>>('log')
      expect(result).toHaveLength(2)
    })
  })

  describe('list', () => {
    it('lists keys with prefix', async () => {
      await adapter.write('data/a', 1)
      await adapter.write('data/b', 2)
      await adapter.write('other/c', 3)

      const keys = await adapter.list('data')
      expect(keys).toHaveLength(2)
    })
  })

  describe('delete', () => {
    it('deletes key', async () => {
      await adapter.write('to-delete', 'value')
      expect(await adapter.exists('to-delete')).toBe(true)

      await adapter.delete('to-delete')
      expect(await adapter.exists('to-delete')).toBe(false)
    })
  })

  describe('exists', () => {
    it('checks key existence', async () => {
      expect(await adapter.exists('key')).toBe(false)
      await adapter.write('key', 'value')
      expect(await adapter.exists('key')).toBe(true)
    })
  })

  describe('keys', () => {
    it('returns all keys', async () => {
      await adapter.write('a', 1)
      await adapter.write('b', 2)

      const keys = adapter.keys()
      expect(keys).toHaveLength(2)
      expect(keys).toContain('a')
      expect(keys).toContain('b')
    })
  })
})

describe('factory functions', () => {
  const testDir = join(tmpdir(), 'delta9-storage-factory-' + Date.now())

  beforeEach(() => {
    clearStorageAdapter()
    mkdirSync(testDir, { recursive: true })
  })

  afterEach(() => {
    clearStorageAdapter()
    if (existsSync(testDir)) {
      rmSync(testDir, { recursive: true, force: true })
    }
  })

  it('getStorageAdapter creates singleton', () => {
    const adapter1 = getStorageAdapter(testDir)
    const adapter2 = getStorageAdapter(testDir)
    expect(adapter1).toBe(adapter2)
  })

  it('createFileStorage creates new instance', () => {
    const adapter1 = createFileStorage({ baseDir: testDir })
    const adapter2 = createFileStorage({ baseDir: testDir })
    expect(adapter1).not.toBe(adapter2)
  })

  it('createMemoryStorage creates new instance', () => {
    const adapter1 = createMemoryStorage()
    const adapter2 = createMemoryStorage()
    expect(adapter1).not.toBe(adapter2)
  })
})
