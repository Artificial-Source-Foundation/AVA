/**
 * Session Resume / Selector Tests
 */

import { beforeEach, describe, expect, it, vi } from 'vitest'
import type { SessionManager } from './manager.js'
import { formatSessionTimestamp, SessionSelector } from './resume.js'
import type { SessionMeta, SessionState } from './types.js'

// ============================================================================
// Mock SessionManager
// ============================================================================

function createMockManager(sessions: SessionMeta[]): SessionManager {
  return {
    list: vi.fn(async () => [...sessions].sort((a, b) => b.updatedAt - a.updatedAt)),
    get: vi.fn(async (id: string) => {
      const meta = sessions.find((s) => s.id === id)
      if (!meta) return null
      return {
        ...meta,
        messages: [],
        openFiles: new Map(),
        env: {},
        toolCallCount: 0,
        tokenStats: {},
      } as unknown as SessionState
    }),
  } as unknown as SessionManager
}

function createMeta(overrides: Partial<SessionMeta> & { id: string }): SessionMeta {
  return {
    messageCount: 5,
    workingDirectory: '/home/user/project',
    createdAt: Date.now() - 60000,
    updatedAt: Date.now(),
    status: 'active',
    ...overrides,
  }
}

// ============================================================================
// Tests
// ============================================================================

describe('SessionSelector', () => {
  let sessions: SessionMeta[]
  let manager: SessionManager
  let selector: SessionSelector

  beforeEach(() => {
    sessions = [
      createMeta({
        id: 'session-1000-aaa',
        name: 'First Session',
        updatedAt: 1000,
        workingDirectory: '/home/user/project-a',
      }),
      createMeta({
        id: 'session-2000-bbb',
        name: 'Second Session',
        updatedAt: 2000,
        workingDirectory: '/home/user/project-b',
      }),
      createMeta({
        id: 'session-3000-ccc',
        name: 'Third Session',
        updatedAt: 3000,
        workingDirectory: '/home/user/project-c',
      }),
    ]
    manager = createMockManager(sessions)
    selector = new SessionSelector(manager, 'session-3000-ccc')
  })

  // =========================================================================
  // "latest" Resolution
  // =========================================================================

  describe('resolve "latest"', () => {
    it('should resolve to most recently updated session', async () => {
      const result = await selector.resolve('latest')

      expect(result.found).toBe(true)
      expect(result.sessionId).toBe('session-3000-ccc')
      expect(result.resolvedBy).toBe('latest')
    })

    it('should handle case-insensitive "latest"', async () => {
      const result = await selector.resolve('LATEST')
      expect(result.found).toBe(true)
      expect(result.sessionId).toBe('session-3000-ccc')
    })

    it('should handle "latest" with whitespace', async () => {
      const result = await selector.resolve('  latest  ')
      expect(result.found).toBe(true)
      expect(result.sessionId).toBe('session-3000-ccc')
    })
  })

  // =========================================================================
  // UUID Resolution
  // =========================================================================

  describe('resolve by UUID', () => {
    it('should resolve exact UUID match', async () => {
      const result = await selector.resolve('session-2000-bbb')

      expect(result.found).toBe(true)
      expect(result.sessionId).toBe('session-2000-bbb')
      expect(result.resolvedBy).toBe('uuid')
    })

    it('should fail for non-existent UUID', async () => {
      const result = await selector.resolve('session-9999-zzz')

      // Falls through to prefix match, which also won't match
      expect(result.found).toBe(false)
    })
  })

  // =========================================================================
  // Numeric Index Resolution
  // =========================================================================

  describe('resolve by index', () => {
    it('should resolve 1-based index (1 = most recent)', async () => {
      const result = await selector.resolve('1')

      expect(result.found).toBe(true)
      expect(result.sessionId).toBe('session-3000-ccc')
      expect(result.resolvedBy).toBe('index')
    })

    it('should resolve index 2 (second most recent)', async () => {
      const result = await selector.resolve('2')

      expect(result.found).toBe(true)
      expect(result.sessionId).toBe('session-2000-bbb')
      expect(result.resolvedBy).toBe('index')
    })

    it('should resolve index 3 (oldest)', async () => {
      const result = await selector.resolve('3')

      expect(result.found).toBe(true)
      expect(result.sessionId).toBe('session-1000-aaa')
      expect(result.resolvedBy).toBe('index')
    })

    it('should fail for out of range index', async () => {
      const result = await selector.resolve('4')

      expect(result.found).toBe(false)
      expect(result.error).toContain('out of range')
      expect(result.candidates).toBeDefined()
    })

    it('should fail for index 0', async () => {
      const result = await selector.resolve('0')

      // 0 is not > 0, so falls through to prefix match
      expect(result.found).toBe(false)
    })

    it('should fail for negative index', async () => {
      const result = await selector.resolve('-1')

      expect(result.found).toBe(false)
    })
  })

  // =========================================================================
  // Prefix Resolution
  // =========================================================================

  describe('resolve by prefix', () => {
    it('should resolve unique prefix match', async () => {
      const result = await selector.resolve('session-2000')

      expect(result.found).toBe(true)
      expect(result.sessionId).toBe('session-2000-bbb')
      expect(result.resolvedBy).toBe('prefix')
    })

    it('should fail with ambiguous prefix', async () => {
      const result = await selector.resolve('session-')

      expect(result.found).toBe(false)
      expect(result.error).toContain('Ambiguous')
      expect(result.candidates).toHaveLength(3)
    })

    it('should fail for no prefix match', async () => {
      const result = await selector.resolve('zzz-no-match')

      expect(result.found).toBe(false)
      expect(result.error).toContain('No session found')
    })
  })

  // =========================================================================
  // Edge Cases
  // =========================================================================

  describe('edge cases', () => {
    it('should fail for empty identifier', async () => {
      const result = await selector.resolve('')
      expect(result.found).toBe(false)
      expect(result.error).toContain('Empty')
    })

    it('should fail for whitespace-only identifier', async () => {
      const result = await selector.resolve('   ')
      expect(result.found).toBe(false)
      expect(result.error).toContain('Empty')
    })

    it('should handle no sessions', async () => {
      const emptyManager = createMockManager([])
      const emptySelector = new SessionSelector(emptyManager)

      const result = await emptySelector.resolve('latest')
      expect(result.found).toBe(false)
      expect(result.error).toContain('No sessions')
    })
  })

  // =========================================================================
  // List Sessions
  // =========================================================================

  describe('listSessions', () => {
    it('should return sessions with display info', async () => {
      const list = await selector.listSessions()

      expect(list).toHaveLength(3)
      // Most recent first
      expect(list[0]!.id).toBe('session-3000-ccc')
      expect(list[0]!.index).toBe(1)
      expect(list[0]!.isCurrent).toBe(true)

      expect(list[1]!.id).toBe('session-2000-bbb')
      expect(list[1]!.index).toBe(2)
      expect(list[1]!.isCurrent).toBe(false)
    })

    it('should respect limit', async () => {
      const list = await selector.listSessions(2)
      expect(list).toHaveLength(2)
    })

    it('should use session name as display name', async () => {
      const list = await selector.listSessions()
      expect(list[0]!.displayName).toBe('Third Session')
    })
  })

  // =========================================================================
  // Search
  // =========================================================================

  describe('search', () => {
    it('should find session by name', async () => {
      const result = await selector.search('Second')

      expect(result.found).toBe(true)
      expect(result.sessionId).toBe('session-2000-bbb')
      expect(result.resolvedBy).toBe('search')
    })

    it('should find session by working directory', async () => {
      const result = await selector.search('project-a')

      expect(result.found).toBe(true)
      expect(result.sessionId).toBe('session-1000-aaa')
    })

    it('should return candidates for ambiguous search', async () => {
      const result = await selector.search('project')

      expect(result.found).toBe(false)
      expect(result.candidates).toHaveLength(3)
    })

    it('should fail for no matches', async () => {
      const result = await selector.search('nonexistent')

      expect(result.found).toBe(false)
      expect(result.error).toContain('No sessions')
    })
  })

  // =========================================================================
  // Convenience Methods
  // =========================================================================

  describe('convenience methods', () => {
    it('should check existence', async () => {
      expect(await selector.exists('session-1000-aaa')).toBe(true)
      expect(await selector.exists('nonexistent')).toBe(false)
    })

    it('should get latest session', async () => {
      const latest = await selector.getLatest()

      expect(latest).not.toBeNull()
      expect(latest!.id).toBe('session-3000-ccc')
    })

    it('should return null when no sessions for getLatest', async () => {
      const emptySelector = new SessionSelector(createMockManager([]))
      const latest = await emptySelector.getLatest()
      expect(latest).toBeNull()
    })

    it('should update current session ID', async () => {
      selector.setCurrentSession('session-1000-aaa')
      const list = await selector.listSessions()

      expect(list[0]!.isCurrent).toBe(false) // session-3000-ccc
      expect(list[2]!.isCurrent).toBe(true) // session-1000-aaa
    })
  })
})

// ============================================================================
// Timestamp Formatting
// ============================================================================

describe('formatSessionTimestamp', () => {
  it('should format recent timestamps', () => {
    const now = Date.now()
    expect(formatSessionTimestamp(now)).toBe('just now')
    expect(formatSessionTimestamp(now - 30000)).toBe('just now') // 30s
    expect(formatSessionTimestamp(now - 120000)).toBe('2m ago')
    expect(formatSessionTimestamp(now - 3600000)).toBe('1h ago')
    expect(formatSessionTimestamp(now - 86400000)).toBe('1d ago')
  })

  it('should format older timestamps with date', () => {
    // 30 days ago
    const oldTimestamp = Date.now() - 30 * 86400000
    const formatted = formatSessionTimestamp(oldTimestamp)
    // Should contain month abbreviation
    expect(formatted).toMatch(/\w{3}\s+\d+/)
  })
})
