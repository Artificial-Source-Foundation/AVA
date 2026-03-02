import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'
import { resetLogger } from '../logger/logger.js'
import { createSessionManager, SessionManager } from './manager.js'
import { SessionBusyError, type SessionEvent } from './types.js'

describe('SessionManager', () => {
  let sm: SessionManager

  beforeEach(() => {
    sm = createSessionManager()
  })

  afterEach(() => {
    sm.dispose()
    resetLogger()
  })

  // ─── create ───────────────────────────────────────────────────────────

  describe('create', () => {
    it('creates a session with unique id', () => {
      const s = sm.create('test', '/tmp')
      expect(s.id).toBeTruthy()
      expect(typeof s.id).toBe('string')
    })

    it('creates a session with given name', () => {
      const s = sm.create('my session', '/tmp')
      expect(s.name).toBe('my session')
    })

    it('creates a session with undefined name', () => {
      const s = sm.create(undefined, '/tmp')
      expect(s.name).toBeUndefined()
    })

    it('creates a session with working directory', () => {
      const s = sm.create('test', '/home/user/project')
      expect(s.workingDirectory).toBe('/home/user/project')
    })

    it('initializes with empty messages', () => {
      const s = sm.create('test', '/tmp')
      expect(s.messages).toEqual([])
    })

    it('initializes with zero tool call count', () => {
      const s = sm.create('test', '/tmp')
      expect(s.toolCallCount).toBe(0)
    })

    it('initializes with active status', () => {
      const s = sm.create('test', '/tmp')
      expect(s.status).toBe('active')
    })

    it('sets createdAt and updatedAt', () => {
      const s = sm.create('test', '/tmp')
      expect(s.createdAt).toBeGreaterThan(0)
      expect(s.updatedAt).toBeGreaterThan(0)
    })

    it('increments size', () => {
      expect(sm.size).toBe(0)
      sm.create('a', '/tmp')
      expect(sm.size).toBe(1)
      sm.create('b', '/tmp')
      expect(sm.size).toBe(2)
    })
  })

  // ─── get ──────────────────────────────────────────────────────────────

  describe('get', () => {
    it('returns session by id', () => {
      const s = sm.create('test', '/tmp')
      expect(sm.get(s.id)).toBe(s)
    })

    it('returns null for unknown id', () => {
      expect(sm.get('nonexistent')).toBeNull()
    })
  })

  // ─── delete ───────────────────────────────────────────────────────────

  describe('delete', () => {
    it('removes session', () => {
      const s = sm.create('test', '/tmp')
      expect(sm.delete(s.id)).toBe(true)
      expect(sm.get(s.id)).toBeNull()
    })

    it('returns false for unknown id', () => {
      expect(sm.delete('nonexistent')).toBe(false)
    })

    it('emits session_cleared event', () => {
      const events: SessionEvent[] = []
      sm.on((e) => events.push(e))
      const s = sm.create('test', '/tmp')
      sm.delete(s.id)
      expect(events).toContainEqual({ type: 'session_cleared', sessionId: s.id })
    })

    it('decrements size', () => {
      const s = sm.create('test', '/tmp')
      expect(sm.size).toBe(1)
      sm.delete(s.id)
      expect(sm.size).toBe(0)
    })
  })

  // ─── list ─────────────────────────────────────────────────────────────

  describe('list', () => {
    it('returns empty array when no sessions', () => {
      expect(sm.list()).toEqual([])
    })

    it('returns session metadata', () => {
      const s = sm.create('test', '/tmp')
      const metas = sm.list()
      expect(metas).toHaveLength(1)
      expect(metas[0].id).toBe(s.id)
      expect(metas[0].name).toBe('test')
      expect(metas[0].messageCount).toBe(0)
      expect(metas[0].workingDirectory).toBe('/tmp')
      expect(metas[0].status).toBe('active')
    })
  })

  // ─── Messages ─────────────────────────────────────────────────────────

  describe('messages', () => {
    it('adds message to session', () => {
      const s = sm.create('test', '/tmp')
      sm.addMessage(s.id, { role: 'user', content: 'hello' })
      expect(s.messages).toHaveLength(1)
      expect(s.messages[0].content).toBe('hello')
    })

    it('emits message_added event', () => {
      const events: SessionEvent[] = []
      sm.on((e) => events.push(e))
      const s = sm.create('test', '/tmp')
      sm.addMessage(s.id, { role: 'user', content: 'hello' })
      expect(events.some((e) => e.type === 'message_added')).toBe(true)
    })

    it('updates updatedAt on addMessage', () => {
      const s = sm.create('test', '/tmp')
      const before = s.updatedAt
      // Small delay
      sm.addMessage(s.id, { role: 'user', content: 'hello' })
      expect(s.updatedAt).toBeGreaterThanOrEqual(before)
    })

    it('throws for unknown session', () => {
      expect(() => sm.addMessage('bad', { role: 'user', content: 'hi' })).toThrow(
        'Session not found'
      )
    })

    it('replaces all messages with setMessages', () => {
      const s = sm.create('test', '/tmp')
      sm.addMessage(s.id, { role: 'user', content: 'first' })
      sm.setMessages(s.id, [{ role: 'assistant', content: 'new' }])
      expect(s.messages).toHaveLength(1)
      expect(s.messages[0].content).toBe('new')
    })
  })

  // ─── State Updates ────────────────────────────────────────────────────

  describe('state updates', () => {
    it('updates token stats', () => {
      const s = sm.create('test', '/tmp')
      sm.updateTokenStats(s.id, { inputTokens: 100, outputTokens: 50 })
      expect(s.tokenStats.inputTokens).toBe(100)
      expect(s.tokenStats.outputTokens).toBe(50)
    })

    it('tracks files', () => {
      const s = sm.create('test', '/tmp')
      sm.trackFile(s.id, {
        path: '/test.ts',
        content: 'code',
        mtime: Date.now(),
        dirty: false,
      })
      expect(s.openFiles.has('/test.ts')).toBe(true)
    })

    it('untracks files', () => {
      const s = sm.create('test', '/tmp')
      sm.trackFile(s.id, {
        path: '/test.ts',
        content: 'code',
        mtime: Date.now(),
        dirty: false,
      })
      sm.untrackFile(s.id, '/test.ts')
      expect(s.openFiles.has('/test.ts')).toBe(false)
    })

    it('increments tool calls', () => {
      const s = sm.create('test', '/tmp')
      sm.incrementToolCalls(s.id)
      sm.incrementToolCalls(s.id)
      expect(s.toolCallCount).toBe(2)
    })

    it('sets status', () => {
      const s = sm.create('test', '/tmp')
      sm.setStatus(s.id, 'completed')
      expect(s.status).toBe('completed')
    })

    it('sets status with error message', () => {
      const s = sm.create('test', '/tmp')
      sm.setStatus(s.id, 'error', 'something failed')
      expect(s.status).toBe('error')
      expect(s.errorMessage).toBe('something failed')
    })

    it('emits status_changed event', () => {
      const events: SessionEvent[] = []
      sm.on((e) => events.push(e))
      const s = sm.create('test', '/tmp')
      sm.setStatus(s.id, 'paused')
      expect(events).toContainEqual({
        type: 'status_changed',
        status: 'paused',
        sessionId: s.id,
      })
    })

    it('sets env variable', () => {
      const s = sm.create('test', '/tmp')
      sm.setEnv(s.id, 'FOO', 'bar')
      expect(s.env.FOO).toBe('bar')
    })
  })

  // ─── Capacity ─────────────────────────────────────────────────────────

  describe('capacity', () => {
    it('evicts oldest session when at capacity', () => {
      const small = createSessionManager({ maxSessions: 2 })
      const s1 = small.create('first', '/tmp')
      const s2 = small.create('second', '/tmp')
      const s3 = small.create('third', '/tmp')
      expect(small.get(s1.id)).toBeNull() // evicted
      expect(small.get(s2.id)).not.toBeNull()
      expect(small.get(s3.id)).not.toBeNull()
      expect(small.size).toBe(2)
      small.dispose()
    })
  })

  // ─── Events ───────────────────────────────────────────────────────────

  describe('events', () => {
    it('unsubscribes listener', () => {
      const handler = vi.fn()
      const unsub = sm.on(handler)
      const s = sm.create('test', '/tmp')
      sm.delete(s.id)
      expect(handler).toHaveBeenCalledOnce()
      unsub()
      const s2 = sm.create('test2', '/tmp')
      sm.delete(s2.id)
      expect(handler).toHaveBeenCalledOnce()
    })
  })

  // ─── slug generation ─────────────────────────────────────────────────

  describe('slug generation', () => {
    it('auto-generates slug from name on create', () => {
      const s = sm.create('Fix the login bug', '/tmp')
      expect(s.slug).toBe('fix-login-bug')
    })

    it('does not generate slug when name is undefined', () => {
      const s = sm.create(undefined, '/tmp')
      expect(s.slug).toBeUndefined()
    })

    it('includes slug in list() metadata', () => {
      sm.create('Add user auth', '/tmp')
      const metas = sm.list()
      expect(metas[0].slug).toBe('add-user-auth')
    })
  })

  // ─── archive ────────────────────────────────────────────────────────

  describe('archive', () => {
    it('sets session status to archived', () => {
      const s = sm.create('test', '/tmp')
      sm.archive(s.id)
      expect(s.status).toBe('archived')
    })

    it('updates updatedAt timestamp', () => {
      const s = sm.create('test', '/tmp')
      const before = s.updatedAt
      sm.archive(s.id)
      expect(s.updatedAt).toBeGreaterThanOrEqual(before)
    })

    it('emits status_changed event with archived status', () => {
      const events: SessionEvent[] = []
      sm.on((e) => events.push(e))
      const s = sm.create('test', '/tmp')
      sm.archive(s.id)
      expect(events).toContainEqual({
        type: 'status_changed',
        status: 'archived',
        sessionId: s.id,
      })
    })

    it('emits session:status idle event', () => {
      const events: SessionEvent[] = []
      sm.on((e) => events.push(e))
      const s = sm.create('test', '/tmp')
      sm.archive(s.id)
      expect(events).toContainEqual({
        type: 'session:status',
        sessionId: s.id,
        status: 'idle',
      })
    })

    it('archived sessions are excluded from list()', () => {
      const s1 = sm.create('active', '/tmp')
      const s2 = sm.create('to archive', '/tmp')
      sm.archive(s2.id)
      const metas = sm.list()
      expect(metas).toHaveLength(1)
      expect(metas[0].id).toBe(s1.id)
    })

    it('throws for unknown session', () => {
      expect(() => sm.archive('nonexistent')).toThrow('Session not found')
    })
  })

  // ─── setBusy ────────────────────────────────────────────────────────

  describe('setBusy', () => {
    it('sets session status to busy', () => {
      const s = sm.create('test', '/tmp')
      sm.setBusy(s.id)
      expect(s.status).toBe('busy')
    })

    it('throws SessionBusyError if already busy', () => {
      const s = sm.create('test', '/tmp')
      sm.setBusy(s.id)
      expect(() => sm.setBusy(s.id)).toThrow(SessionBusyError)
    })

    it('thrown error has correct name and message', () => {
      const s = sm.create('test', '/tmp')
      sm.setBusy(s.id)
      try {
        sm.setBusy(s.id)
        expect.fail('should have thrown')
      } catch (err) {
        expect(err).toBeInstanceOf(SessionBusyError)
        expect((err as SessionBusyError).name).toBe('SessionBusyError')
        expect((err as SessionBusyError).message).toBe(`Session ${s.id} is busy`)
      }
    })

    it('emits status_changed and session:status events', () => {
      const events: SessionEvent[] = []
      sm.on((e) => events.push(e))
      const s = sm.create('test', '/tmp')
      sm.setBusy(s.id)
      expect(events).toContainEqual({
        type: 'status_changed',
        status: 'busy',
        sessionId: s.id,
      })
      expect(events).toContainEqual({
        type: 'session:status',
        sessionId: s.id,
        status: 'busy',
      })
    })

    it('updates updatedAt timestamp', () => {
      const s = sm.create('test', '/tmp')
      const before = s.updatedAt
      sm.setBusy(s.id)
      expect(s.updatedAt).toBeGreaterThanOrEqual(before)
    })

    it('throws for unknown session', () => {
      expect(() => sm.setBusy('nonexistent')).toThrow('Session not found')
    })
  })

  // ─── listGlobal ─────────────────────────────────────────────────────

  describe('listGlobal', () => {
    it('returns all sessions including archived', () => {
      sm.create('active', '/tmp')
      const s2 = sm.create('to archive', '/tmp')
      sm.archive(s2.id)
      const result = sm.listGlobal()
      expect(result.sessions).toHaveLength(2)
    })

    it('sorts by updatedAt descending', () => {
      const s1 = sm.create('first', '/tmp')
      const s2 = sm.create('second', '/tmp')
      // Make s1 more recent
      sm.addMessage(s1.id, { role: 'user', content: 'hello' })
      const result = sm.listGlobal()
      expect(result.sessions[0].id).toBe(s1.id)
      expect(result.sessions[1].id).toBe(s2.id)
    })

    it('returns empty when no sessions', () => {
      const result = sm.listGlobal()
      expect(result.sessions).toEqual([])
      expect(result.nextCursor).toBeNull()
    })

    it('paginates with limit', () => {
      sm.create('a', '/tmp')
      sm.create('b', '/tmp')
      sm.create('c', '/tmp')
      const result = sm.listGlobal(undefined, 2)
      expect(result.sessions).toHaveLength(2)
      expect(result.nextCursor).not.toBeNull()
    })

    it('returns nextCursor null when no more pages', () => {
      sm.create('a', '/tmp')
      sm.create('b', '/tmp')
      const result = sm.listGlobal(undefined, 10)
      expect(result.sessions).toHaveLength(2)
      expect(result.nextCursor).toBeNull()
    })

    it('uses cursor to fetch next page', () => {
      sm.create('a', '/tmp')
      sm.create('b', '/tmp')
      sm.create('c', '/tmp')
      const page1 = sm.listGlobal(undefined, 2)
      expect(page1.sessions).toHaveLength(2)
      expect(page1.nextCursor).not.toBeNull()

      const page2 = sm.listGlobal(page1.nextCursor!, 2)
      expect(page2.sessions).toHaveLength(1)
      expect(page2.nextCursor).toBeNull()
    })

    it('returns all sessions when cursor is invalid', () => {
      sm.create('a', '/tmp')
      sm.create('b', '/tmp')
      const result = sm.listGlobal('nonexistent-cursor', 10)
      // Invalid cursor resets to start
      expect(result.sessions).toHaveLength(2)
    })

    it('includes slug in paginated results', () => {
      sm.create('Fix login page', '/tmp')
      const result = sm.listGlobal()
      expect(result.sessions[0].slug).toBe('fix-login-page')
    })
  })

  // ─── setStatus session:status events ────────────────────────────────

  describe('setStatus session:status events', () => {
    it('emits session:status busy when setting status to busy', () => {
      const events: SessionEvent[] = []
      sm.on((e) => events.push(e))
      const s = sm.create('test', '/tmp')
      sm.setStatus(s.id, 'busy')
      expect(events).toContainEqual({
        type: 'session:status',
        sessionId: s.id,
        status: 'busy',
      })
    })

    it('emits session:status idle when setting status to active', () => {
      const events: SessionEvent[] = []
      sm.on((e) => events.push(e))
      const s = sm.create('test', '/tmp')
      sm.setStatus(s.id, 'completed')
      expect(events).toContainEqual({
        type: 'session:status',
        sessionId: s.id,
        status: 'idle',
      })
    })

    it('does not emit session:status for error status', () => {
      const events: SessionEvent[] = []
      sm.on((e) => events.push(e))
      const s = sm.create('test', '/tmp')
      sm.setStatus(s.id, 'error', 'something went wrong')
      const statusEvents = events.filter((e) => e.type === 'session:status')
      expect(statusEvents).toHaveLength(0)
    })
  })

  // ─── dispose ──────────────────────────────────────────────────────────

  describe('dispose', () => {
    it('clears all sessions', () => {
      sm.create('a', '/tmp')
      sm.create('b', '/tmp')
      sm.dispose()
      expect(sm.size).toBe(0)
    })
  })
})

// ─── Factory ──────────────────────────────────────────────────────────────

describe('createSessionManager', () => {
  it('creates with default config', () => {
    const sm = createSessionManager()
    expect(sm).toBeInstanceOf(SessionManager)
    sm.dispose()
  })

  it('creates with custom config', () => {
    const sm = createSessionManager({ maxSessions: 5 })
    expect(sm).toBeInstanceOf(SessionManager)
    sm.dispose()
  })
})

// ─── Storage-backed behavior ─────────────────────────────────────────────

describe('SessionManager with storage', () => {
  it('saves session to storage', async () => {
    const { MemorySessionStorage } = await import('./memory-storage.js')
    const storage = new MemorySessionStorage()
    const sm = createSessionManager({ storage })
    const s = sm.create('test', '/tmp')
    await sm.save(s.id)
    expect(storage.size).toBe(1)
    sm.dispose()
  })

  it('loads session from storage', async () => {
    const { MemorySessionStorage } = await import('./memory-storage.js')
    const storage = new MemorySessionStorage()
    const sm1 = createSessionManager({ storage })
    const s = sm1.create('test', '/tmp')
    sm1.addMessage(s.id, { role: 'user', content: 'hello' })
    await sm1.save(s.id)
    sm1.dispose()

    const sm2 = createSessionManager({ storage })
    const loaded = await sm2.loadSession(s.id)
    expect(loaded).not.toBeNull()
    expect(loaded!.messages).toHaveLength(1)
    sm2.dispose()
  })

  it('loads all sessions from storage', async () => {
    const { MemorySessionStorage } = await import('./memory-storage.js')
    const storage = new MemorySessionStorage()
    const sm1 = createSessionManager({ storage })
    const s1 = sm1.create('a', '/tmp')
    const s2 = sm1.create('b', '/tmp')
    await sm1.save(s1.id)
    await sm1.save(s2.id)
    sm1.dispose()

    const sm2 = createSessionManager({ storage })
    const count = await sm2.loadFromStorage()
    expect(count).toBe(2)
    expect(sm2.size).toBe(2)
    sm2.dispose()
  })

  it('emits session_saved event', async () => {
    const { MemorySessionStorage } = await import('./memory-storage.js')
    const storage = new MemorySessionStorage()
    const sm = createSessionManager({ storage })
    const events: SessionEvent[] = []
    sm.on((e) => events.push(e))
    const s = sm.create('test', '/tmp')
    await sm.save(s.id)
    expect(events.some((e) => e.type === 'session_saved')).toBe(true)
    sm.dispose()
  })

  it('save is no-op without storage', async () => {
    const sm = createSessionManager()
    const s = sm.create('test', '/tmp')
    await sm.save(s.id) // should not throw
    sm.dispose()
  })
})
