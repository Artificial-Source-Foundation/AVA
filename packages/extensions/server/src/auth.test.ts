import type { IncomingMessage } from 'node:http'
import { afterEach, describe, expect, it, vi } from 'vitest'
import {
  addToken,
  generateToken,
  loadTokens,
  removeToken,
  saveTokens,
  validateRequest,
} from './auth.js'

// Mock fs
vi.mock('node:fs/promises', () => ({
  readFile: vi.fn(),
  writeFile: vi.fn(),
  mkdir: vi.fn(),
}))

describe('auth', () => {
  afterEach(() => {
    vi.restoreAllMocks()
  })

  describe('generateToken', () => {
    it('generates a token with ava_ prefix', () => {
      const entry = generateToken('test')
      expect(entry.token).toMatch(/^ava_[0-9a-f]+$/)
    })

    it('generates unique tokens', () => {
      const t1 = generateToken('a')
      const t2 = generateToken('b')
      expect(t1.token).not.toBe(t2.token)
    })

    it('uses provided name', () => {
      const entry = generateToken('my-key')
      expect(entry.name).toBe('my-key')
    })

    it('defaults to "default" name', () => {
      const entry = generateToken()
      expect(entry.name).toBe('default')
    })

    it('sets createdAt timestamp', () => {
      const before = Date.now()
      const entry = generateToken()
      expect(entry.createdAt).toBeGreaterThanOrEqual(before)
    })
  })

  describe('loadTokens', () => {
    it('returns empty store when file does not exist', async () => {
      const { readFile } = await import('node:fs/promises')
      vi.mocked(readFile).mockRejectedValue(new Error('ENOENT'))

      const store = await loadTokens('/tmp/tokens.json')
      expect(store.tokens).toEqual([])
    })

    it('parses existing token file', async () => {
      const { readFile } = await import('node:fs/promises')
      const data = JSON.stringify({ tokens: [{ token: 'ava_abc', name: 'test', createdAt: 1 }] })
      vi.mocked(readFile).mockResolvedValue(data)

      const store = await loadTokens('/tmp/tokens.json')
      expect(store.tokens).toHaveLength(1)
      expect(store.tokens[0].token).toBe('ava_abc')
    })
  })

  describe('saveTokens', () => {
    it('writes token store to file', async () => {
      const { writeFile, mkdir } = await import('node:fs/promises')
      vi.mocked(mkdir).mockResolvedValue(undefined)
      vi.mocked(writeFile).mockResolvedValue(undefined)

      const store = { tokens: [{ token: 'ava_xyz', name: 'test', createdAt: 1 }] }
      await saveTokens('/tmp/tokens.json', store)

      expect(writeFile).toHaveBeenCalled()
    })
  })

  describe('validateRequest', () => {
    it('rejects requests without Authorization header', async () => {
      const req = { headers: {} } as IncomingMessage
      const valid = await validateRequest(req, '/tmp/tokens.json')
      expect(valid).toBe(false)
    })

    it('rejects requests with non-Bearer auth', async () => {
      const req = { headers: { authorization: 'Basic abc' } } as IncomingMessage
      const valid = await validateRequest(req, '/tmp/tokens.json')
      expect(valid).toBe(false)
    })

    it('validates matching token', async () => {
      const { readFile } = await import('node:fs/promises')
      const data = JSON.stringify({ tokens: [{ token: 'ava_valid', name: 'test', createdAt: 1 }] })
      vi.mocked(readFile).mockResolvedValue(data)

      const req = { headers: { authorization: 'Bearer ava_valid' } } as IncomingMessage
      const valid = await validateRequest(req, '/tmp/tokens.json')
      expect(valid).toBe(true)
    })

    it('rejects invalid token', async () => {
      const { readFile } = await import('node:fs/promises')
      const data = JSON.stringify({ tokens: [{ token: 'ava_valid', name: 'test', createdAt: 1 }] })
      vi.mocked(readFile).mockResolvedValue(data)

      const req = { headers: { authorization: 'Bearer ava_wrong' } } as IncomingMessage
      const valid = await validateRequest(req, '/tmp/tokens.json')
      expect(valid).toBe(false)
    })
  })

  describe('addToken', () => {
    it('adds a token and saves', async () => {
      const { readFile, writeFile, mkdir } = await import('node:fs/promises')
      vi.mocked(readFile).mockResolvedValue(JSON.stringify({ tokens: [] }))
      vi.mocked(mkdir).mockResolvedValue(undefined)
      vi.mocked(writeFile).mockResolvedValue(undefined)

      const entry = await addToken('/tmp/tokens.json', 'new-key')
      expect(entry.name).toBe('new-key')
      expect(entry.token).toMatch(/^ava_/)
      expect(writeFile).toHaveBeenCalled()
    })
  })

  describe('removeToken', () => {
    it('removes existing token', async () => {
      const { readFile, writeFile, mkdir } = await import('node:fs/promises')
      const data = JSON.stringify({ tokens: [{ token: 'ava_rm', name: 'test', createdAt: 1 }] })
      vi.mocked(readFile).mockResolvedValue(data)
      vi.mocked(mkdir).mockResolvedValue(undefined)
      vi.mocked(writeFile).mockResolvedValue(undefined)

      const removed = await removeToken('/tmp/tokens.json', 'ava_rm')
      expect(removed).toBe(true)
    })

    it('returns false for nonexistent token', async () => {
      const { readFile } = await import('node:fs/promises')
      vi.mocked(readFile).mockResolvedValue(JSON.stringify({ tokens: [] }))

      const removed = await removeToken('/tmp/tokens.json', 'ava_nope')
      expect(removed).toBe(false)
    })
  })
})
