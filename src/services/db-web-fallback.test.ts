import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'

import { STORAGE_KEYS } from '../config/constants'

import { createWebDatabase } from './db-web-fallback'
import {
  clearAllSessionIdMappings,
  registerBackendSessionId,
  rehydrateFromLocalStorageForTesting,
} from './web-session-identity'

describe('db-web-fallback message recovery', () => {
  beforeEach(() => {
    vi.restoreAllMocks()
    clearAllSessionIdMappings()
  })

  afterEach(() => {
    vi.unstubAllGlobals()
    clearAllSessionIdMappings()
  })

  it('reconstructs durable tool-call payloads from web recovery responses on cache miss', async () => {
    registerBackendSessionId('session-front', 'session-back')

    const fetchMock = vi.fn().mockResolvedValue({
      ok: true,
      json: async () => [
        {
          id: 'assistant-final',
          role: 'assistant',
          content: 'authoritative final answer',
          timestamp: '2026-04-17T10:00:10Z',
          tool_calls: [
            {
              id: 'tool-rich-1',
              name: 'bash',
              arguments: { command: 'pwd' },
              status: 'success',
              output: '/workspace',
              startedAt: 10,
              completedAt: 20,
              contentOffset: 42,
            },
          ],
          metadata: null,
          model: 'gpt-5.4',
        },
      ],
    })
    vi.stubGlobal('fetch', fetchMock)

    const database = createWebDatabase()
    const rows = await database.select<Array<Record<string, unknown>>>(
      'SELECT * FROM messages WHERE session_id = ? ORDER BY created_at ASC',
      ['session-front']
    )

    expect(fetchMock).toHaveBeenCalledWith('/api/sessions/session-back/messages')
    expect(rows).toHaveLength(1)
    expect(rows[0]?.session_id).toBe('session-front')
    expect(JSON.parse(rows[0]?.metadata as string)).toEqual(
      expect.objectContaining({
        toolCalls: [
          expect.objectContaining({
            id: 'tool-rich-1',
            name: 'bash',
            arguments: { command: 'pwd' },
            output: '/workspace',
            startedAt: 10,
            completedAt: 20,
            contentOffset: 42,
          }),
        ],
      })
    )
  })

  it('keeps recovered rows keyed to the frontend session id when backend ids diverge', async () => {
    registerBackendSessionId('session-origin', 'session-backend')

    const fetchMock = vi.fn().mockResolvedValue({
      ok: true,
      json: async () => [
        {
          id: 'backend-user-1',
          role: 'user',
          content: 'Hello',
          created_at: 1,
          metadata: { note: 'from backend' },
        },
      ],
    })
    vi.stubGlobal('fetch', fetchMock)

    const database = createWebDatabase()
    const rows = await database.select<Array<Record<string, unknown>>>(
      'SELECT * FROM messages WHERE session_id = ? ORDER BY created_at ASC',
      ['session-origin']
    )

    expect(fetchMock).toHaveBeenCalledWith('/api/sessions/session-backend/messages')
    expect(rows[0]?.session_id).toBe('session-origin')
    expect(JSON.parse(rows[0]?.metadata as string)).toEqual(
      expect.objectContaining({ note: 'from backend' })
    )
  })
})

describe('db-web-fallback non-message subresources', () => {
  beforeEach(() => {
    vi.restoreAllMocks()
    clearAllSessionIdMappings()
  })

  afterEach(() => {
    vi.unstubAllGlobals()
    clearAllSessionIdMappings()
  })

  it('resolves session alias for agents subresource endpoint', async () => {
    registerBackendSessionId('fe-agents-session', 'be-agents-session')

    const fetchMock = vi.fn().mockResolvedValue({
      ok: true,
      json: async () => [{ id: 'agent-1', name: 'Coder', status: 'idle' }],
    })
    vi.stubGlobal('fetch', fetchMock)

    const database = createWebDatabase()
    const rows = await database.select<Array<Record<string, unknown>>>(
      'SELECT * FROM agents WHERE session_id = ?',
      ['fe-agents-session']
    )

    // Should call the backend session ID endpoint, not frontend
    expect(fetchMock).toHaveBeenCalledWith('/api/sessions/be-agents-session/agents')
    expect(rows).toHaveLength(1)
    expect(rows[0]?.id).toBe('agent-1')
  })

  it('resolves session alias for files subresource endpoint', async () => {
    registerBackendSessionId('fe-files-session', 'be-files-session')

    const fetchMock = vi.fn().mockResolvedValue({
      ok: true,
      json: async () => [{ path: '/test.txt', content: 'test' }],
    })
    vi.stubGlobal('fetch', fetchMock)

    const database = createWebDatabase()
    await database.select<Array<Record<string, unknown>>>(
      'SELECT * FROM file_operations WHERE session_id = ?',
      ['fe-files-session']
    )

    expect(fetchMock).toHaveBeenCalledWith('/api/sessions/be-files-session/files')
  })

  it('resolves session alias for rename operation', async () => {
    registerBackendSessionId('fe-rename-session', 'be-rename-session')

    const fetchMock = vi.fn().mockResolvedValue({ ok: true })
    vi.stubGlobal('fetch', fetchMock)

    const database = createWebDatabase()
    await database.execute('UPDATE sessions SET updated_at = ?, name = ? WHERE id = ?', [
      Date.now(),
      'New Name',
      'fe-rename-session',
    ])

    expect(fetchMock).toHaveBeenCalledWith(
      '/api/sessions/be-rename-session/rename',
      expect.objectContaining({
        method: 'POST',
        body: JSON.stringify({ name: 'New Name' }),
      })
    )
  })

  it('archives mapped sessions through the backend and preserves alias resolution for later unarchive', async () => {
    registerBackendSessionId('fe-archive-session', 'be-archive-session')

    const fetchMock = vi
      .fn()
      .mockResolvedValueOnce({ ok: true, status: 200, statusText: 'OK' })
      .mockResolvedValueOnce({
        ok: true,
        json: async () => [
          {
            id: 'fe-archive-session',
            title: 'Archived session',
            status: 'archived',
            message_count: 2,
            created_at: '2026-04-18T10:00:00Z',
            updated_at: '2026-04-18T10:05:00Z',
          },
        ],
      })
      .mockResolvedValueOnce({ ok: true, status: 200, statusText: 'OK' })
    vi.stubGlobal('fetch', fetchMock)

    const database = createWebDatabase()

    await database.execute('UPDATE sessions SET updated_at = ?, status = ? WHERE id = ?', [
      Date.now(),
      'archived',
      'fe-archive-session',
    ])

    const archivedRows = await database.select<Array<Record<string, unknown>>>(
      `
      SELECT
        s.*,
        COUNT(m.id) as message_count,
        COALESCE(SUM(m.tokens_used), 0) as total_tokens,
        COALESCE(SUM(m.cost_usd), 0) as total_cost,
        (SELECT content FROM messages WHERE session_id = s.id ORDER BY created_at DESC LIMIT 1) as last_preview
      FROM sessions s
      LEFT JOIN messages m ON m.session_id = s.id
      WHERE s.status = 'archived'
      GROUP BY s.id
      ORDER BY s.updated_at DESC
    `,
      []
    )

    await database.execute('UPDATE sessions SET updated_at = ?, status = ? WHERE id = ?', [
      Date.now(),
      'active',
      'fe-archive-session',
    ])

    expect(fetchMock).toHaveBeenNthCalledWith(
      1,
      '/api/sessions/be-archive-session/archive',
      expect.objectContaining({ method: 'POST' })
    )
    expect(fetchMock).toHaveBeenNthCalledWith(2, '/api/sessions?status=archived')
    expect(archivedRows).toHaveLength(1)
    expect(archivedRows[0]?.status).toBe('archived')
    expect(fetchMock).toHaveBeenNthCalledWith(
      3,
      '/api/sessions/be-archive-session/unarchive',
      expect.objectContaining({ method: 'POST' })
    )
  })
})

describe('db-web-fallback reverse alias canonicalization', () => {
  beforeEach(() => {
    vi.restoreAllMocks()
    clearAllSessionIdMappings()
  })

  afterEach(() => {
    vi.unstubAllGlobals()
    clearAllSessionIdMappings()
  })

  it('canonicalizes backend session IDs to frontend IDs in active session list', async () => {
    // Register a frontend→backend mapping (as would happen after a retry/regenerate)
    registerBackendSessionId('fe-session-abc', 'be-session-xyz')

    // Backend returns the session with its backend ID
    const fetchMock = vi.fn().mockResolvedValue({
      ok: true,
      json: async () => [
        {
          id: 'be-session-xyz', // Backend returns its own ID
          title: 'My Session',
          status: 'active',
          message_count: 5,
          created_at: '2026-04-18T10:00:00Z',
          updated_at: '2026-04-18T10:05:00Z',
        },
      ],
    })
    vi.stubGlobal('fetch', fetchMock)

    const database = createWebDatabase()
    const rows = await database.select<Array<Record<string, unknown>>>(
      `
      SELECT
        s.*,
        COUNT(m.id) as message_count
      FROM sessions s
      LEFT JOIN messages m ON m.session_id = s.id
      GROUP BY s.id
    `,
      []
    )

    // The session ID should be canonicalized back to the frontend ID
    expect(rows).toHaveLength(1)
    expect(rows[0]?.id).toBe('fe-session-abc') // NOT 'be-session-xyz'
    expect(rows[0]?.name).toBe('My Session')
  })

  it('canonicalizes backend session IDs to frontend IDs in archived session list', async () => {
    // This tests the specific scenario: archived-list responses with backend IDs
    registerBackendSessionId('fe-archived-session', 'be-archived-backend')

    const fetchMock = vi.fn().mockResolvedValue({
      ok: true,
      json: async () => [
        {
          id: 'be-archived-backend', // Backend returns backend ID in archived list
          title: 'Archived Session',
          status: 'archived',
          message_count: 3,
          created_at: '2026-04-18T10:00:00Z',
          updated_at: '2026-04-18T10:05:00Z',
        },
      ],
    })
    vi.stubGlobal('fetch', fetchMock)

    const database = createWebDatabase()
    const rows = await database.select<Array<Record<string, unknown>>>(
      `
      SELECT
        s.*,
        COUNT(m.id) as message_count
      FROM sessions s
      LEFT JOIN messages m ON m.session_id = s.id
      WHERE s.status = 'archived'
      GROUP BY s.id
    `,
      []
    )

    // Session ID should be canonicalized to frontend ID
    expect(rows).toHaveLength(1)
    expect(rows[0]?.id).toBe('fe-archived-session')
    expect(rows[0]?.status).toBe('archived')
  })

  it('passes through unchanged IDs when no reverse mapping exists', async () => {
    // No mapping registered - backend ID should be used as-is
    const fetchMock = vi.fn().mockResolvedValue({
      ok: true,
      json: async () => [
        {
          id: 'some-session-id',
          title: 'Unmapped Session',
          status: 'active',
          message_count: 1,
        },
      ],
    })
    vi.stubGlobal('fetch', fetchMock)

    const database = createWebDatabase()
    const rows = await database.select<Array<Record<string, unknown>>>(
      `
      SELECT s.* FROM sessions s
      LEFT JOIN messages m ON m.session_id = s.id
      GROUP BY s.id
    `,
      []
    )

    // ID should pass through unchanged since no mapping exists
    expect(rows).toHaveLength(1)
    expect(rows[0]?.id).toBe('some-session-id')
  })

  it('handles multiple sessions with mixed frontend/backend IDs', async () => {
    // Register only one mapping
    registerBackendSessionId('fe-mapped-session', 'be-mapped-backend')

    const fetchMock = vi.fn().mockResolvedValue({
      ok: true,
      json: async () => [
        {
          id: 'be-mapped-backend', // Has reverse mapping
          title: 'Mapped Session',
          status: 'active',
          message_count: 5,
        },
        {
          id: 'unmapped-session-id', // No mapping
          title: 'Unmapped Session',
          status: 'active',
          message_count: 3,
        },
        {
          id: 'another-unmapped', // No mapping
          title: 'Another Unmapped',
          status: 'archived',
          message_count: 1,
        },
      ],
    })
    vi.stubGlobal('fetch', fetchMock)

    const database = createWebDatabase()
    const rows = await database.select<Array<Record<string, unknown>>>(
      `
      SELECT s.* FROM sessions s
      LEFT JOIN messages m ON m.session_id = s.id
      GROUP BY s.id
    `,
      []
    )

    // First session should be canonicalized, others pass through
    expect(rows).toHaveLength(3)
    expect(rows[0]?.id).toBe('fe-mapped-session') // Canonicalized
    expect(rows[1]?.id).toBe('unmapped-session-id') // Unchanged
    expect(rows[2]?.id).toBe('another-unmapped') // Unchanged
  })

  it('canonicalizes active session list after persisted reload (reload persistence)', async () => {
    // Register a mapping and persist to localStorage
    registerBackendSessionId('fe-persisted-session', 'be-persisted-backend')
    const stored = localStorage.getItem(STORAGE_KEYS.SESSION_ID_ALIASES)
    expect(stored).toBeTruthy() // Verify persistence worked

    // Simulate module reload: clear memory and rehydrate
    clearAllSessionIdMappings()
    localStorage.setItem(STORAGE_KEYS.SESSION_ID_ALIASES, stored!)
    rehydrateFromLocalStorageForTesting()

    // Backend returns backend IDs
    const fetchMock = vi.fn().mockResolvedValue({
      ok: true,
      json: async () => [
        {
          id: 'be-persisted-backend', // Backend returns its ID
          title: 'Persisted Session',
          status: 'active',
          message_count: 3,
          created_at: '2026-04-18T10:00:00Z',
          updated_at: '2026-04-18T10:05:00Z',
        },
      ],
    })
    vi.stubGlobal('fetch', fetchMock)

    const database = createWebDatabase()
    const rows = await database.select<Array<Record<string, unknown>>>(
      `SELECT s.* FROM sessions s LEFT JOIN messages m ON m.session_id = s.id GROUP BY s.id`,
      []
    )

    // After "reload", canonicalization should still work
    expect(rows).toHaveLength(1)
    expect(rows[0]?.id).toBe('fe-persisted-session') // Canonicalized, NOT be-persisted-backend
  })

  it('canonicalizes archived session list after persisted reload (reload persistence)', async () => {
    // Register and persist a mapping
    registerBackendSessionId('fe-archived-persisted', 'be-archived-persisted')
    const stored = localStorage.getItem(STORAGE_KEYS.SESSION_ID_ALIASES)
    expect(stored).toBeTruthy()

    // Simulate reload
    clearAllSessionIdMappings()
    localStorage.setItem(STORAGE_KEYS.SESSION_ID_ALIASES, stored!)
    rehydrateFromLocalStorageForTesting()

    // Backend returns archived list with backend ID
    const fetchMock = vi.fn().mockResolvedValue({
      ok: true,
      json: async () => [
        {
          id: 'be-archived-persisted',
          title: 'Archived Persisted Session',
          status: 'archived',
          message_count: 5,
          created_at: '2026-04-18T10:00:00Z',
          updated_at: '2026-04-18T10:05:00Z',
        },
      ],
    })
    vi.stubGlobal('fetch', fetchMock)

    const database = createWebDatabase()
    const rows = await database.select<Array<Record<string, unknown>>>(
      `SELECT s.* FROM sessions s LEFT JOIN messages m ON m.session_id = s.id WHERE s.status = 'archived' GROUP BY s.id`,
      []
    )

    // After "reload", archived list canonicalization should work
    expect(rows).toHaveLength(1)
    expect(rows[0]?.id).toBe('fe-archived-persisted') // Canonicalized
    expect(rows[0]?.status).toBe('archived')
  })

  it('handles mixed sessions after persisted reload', async () => {
    // Register and persist one mapping
    registerBackendSessionId('fe-mixed-1', 'be-mixed-1')
    const stored = localStorage.getItem(STORAGE_KEYS.SESSION_ID_ALIASES)

    // Simulate reload
    clearAllSessionIdMappings()
    localStorage.setItem(STORAGE_KEYS.SESSION_ID_ALIASES, stored!)
    rehydrateFromLocalStorageForTesting()

    // Backend returns mixed IDs
    const fetchMock = vi.fn().mockResolvedValue({
      ok: true,
      json: async () => [
        {
          id: 'be-mixed-1', // Has persisted reverse mapping
          title: 'Mixed 1',
          status: 'active',
          message_count: 2,
        },
        {
          id: 'unmapped-session', // No mapping
          title: 'Unmapped',
          status: 'active',
          message_count: 1,
        },
      ],
    })
    vi.stubGlobal('fetch', fetchMock)

    const database = createWebDatabase()
    const rows = await database.select<Array<Record<string, unknown>>>(
      `SELECT s.* FROM sessions s LEFT JOIN messages m ON m.session_id = s.id GROUP BY s.id`,
      []
    )

    // After "reload", canonicalization should still work correctly
    expect(rows).toHaveLength(2)
    expect(rows[0]?.id).toBe('fe-mixed-1') // Canonicalized from persisted mapping
    expect(rows[1]?.id).toBe('unmapped-session') // Passed through unchanged
  })
})
