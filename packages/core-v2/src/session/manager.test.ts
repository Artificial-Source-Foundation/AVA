import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'
import { resetLogger } from '../logger/logger.js'
import { createSessionManager, SessionManager } from './manager.js'
import type { SessionEvent } from './types.js'

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
