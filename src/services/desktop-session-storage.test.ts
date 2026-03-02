/**
 * Desktop Session Storage Tests
 *
 * Tests the adapter that bridges core-v2 SessionStorage with the desktop SQLite database.
 */

import { beforeEach, describe, expect, it, vi } from 'vitest'
import { DesktopSessionStorage } from './desktop-session-storage'

// Mock all database imports
vi.mock('./database', () => ({
  getDb: vi.fn().mockResolvedValue({
    select: vi.fn().mockResolvedValue([]),
    execute: vi.fn().mockResolvedValue({ rowsAffected: 0 }),
  }),
  createSession: vi.fn().mockResolvedValue({
    id: 'test-id',
    name: 'Test Session',
    createdAt: Date.now(),
    updatedAt: Date.now(),
    status: 'active',
  }),
  deleteSession: vi.fn().mockResolvedValue(undefined),
  getMessages: vi.fn().mockResolvedValue([]),
  getSessionsWithStats: vi.fn().mockResolvedValue([]),
  insertMessages: vi.fn().mockResolvedValue(undefined),
  deleteSessionMessages: vi.fn().mockResolvedValue(undefined),
  updateSession: vi.fn().mockResolvedValue(undefined),
}))

describe('DesktopSessionStorage', () => {
  let storage: DesktopSessionStorage

  beforeEach(() => {
    storage = new DesktopSessionStorage()
    vi.clearAllMocks()
  })

  it('implements SessionStorage interface', () => {
    expect(typeof storage.save).toBe('function')
    expect(typeof storage.load).toBe('function')
    expect(typeof storage.delete).toBe('function')
    expect(typeof storage.list).toBe('function')
    expect(typeof storage.loadAll).toBe('function')
  })

  it('list() returns session summaries', async () => {
    const { getSessionsWithStats } = await import('./database')
    vi.mocked(getSessionsWithStats).mockResolvedValue([
      {
        id: 's1',
        name: 'Session 1',
        createdAt: 1000,
        updatedAt: 2000,
        status: 'active',
        messageCount: 5,
        totalTokens: 100,
      },
      {
        id: 's2',
        name: 'Session 2',
        createdAt: 1500,
        updatedAt: 2500,
        status: 'active',
        messageCount: 3,
        totalTokens: 50,
      },
    ])

    const result = await storage.list()
    expect(result).toHaveLength(2)
    expect(result[0]).toEqual({ id: 's1', name: 'Session 1', updatedAt: 2000 })
    expect(result[1]).toEqual({ id: 's2', name: 'Session 2', updatedAt: 2500 })
  })

  it('delete() returns true on success', async () => {
    const result = await storage.delete('test-id')
    expect(result).toBe(true)
  })

  it('delete() returns false on failure', async () => {
    const { deleteSession } = await import('./database')
    vi.mocked(deleteSession).mockRejectedValueOnce(new Error('fail'))

    const result = await storage.delete('test-id')
    expect(result).toBe(false)
  })

  it('load() returns null for missing session', async () => {
    const { getDb } = await import('./database')
    vi.mocked(getDb).mockResolvedValue({
      select: vi.fn().mockResolvedValue([]),
      execute: vi.fn(),
    } as never)

    const result = await storage.load('nonexistent')
    expect(result).toBeNull()
  })
})
