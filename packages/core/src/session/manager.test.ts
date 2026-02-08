/**
 * Session Manager Tests
 *
 * Tests for SessionManager class: create/get/save/delete lifecycle,
 * LRU caching, message management, checkpoints, forking, events, and cleanup.
 */

import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'
import type { Message } from '../context/types.js'
import { createSessionManager, SessionManager } from './manager.js'
import type { Checkpoint, CheckpointMeta, SerializedSessionState, SessionStorage } from './types.js'

// ============================================================================
// Helpers
// ============================================================================

function makeMessage(id: string, role: Message['role'], content: string): Message {
  return {
    id,
    role,
    content,
    sessionId: 'test-session',
    createdAt: Date.now(),
  }
}

/** Create a mock storage backend */
function createMockStorage(): SessionStorage & {
  saved: Map<string, SerializedSessionState>
  checkpoints: Map<string, Checkpoint>
} {
  const saved = new Map<string, SerializedSessionState>()
  const checkpoints = new Map<string, Checkpoint>()

  return {
    saved,
    checkpoints,
    save: vi.fn(async (session: SerializedSessionState) => {
      saved.set(session.id, session)
    }),
    load: vi.fn(async (sessionId: string) => {
      return saved.get(sessionId) ?? null
    }),
    delete: vi.fn(async (sessionId: string) => {
      saved.delete(sessionId)
    }),
    list: vi.fn(async () => {
      return Array.from(saved.keys())
    }),
    saveCheckpoint: vi.fn(async (_sessionId: string, checkpoint: Checkpoint) => {
      checkpoints.set(checkpoint.id, checkpoint)
    }),
    loadCheckpoint: vi.fn(async (_sessionId: string, checkpointId: string) => {
      return checkpoints.get(checkpointId) ?? null
    }),
    deleteCheckpoint: vi.fn(async (_sessionId: string, checkpointId: string) => {
      checkpoints.delete(checkpointId)
    }),
    listCheckpoints: vi.fn(async (_sessionId: string) => {
      return Array.from(checkpoints.values()).map((cp) => ({
        id: cp.id,
        timestamp: cp.timestamp,
        description: cp.description,
        messageCount: cp.messageCount,
      })) as CheckpointMeta[]
    }),
  }
}

// ============================================================================
// Session Lifecycle
// ============================================================================

describe('SessionManager', () => {
  let manager: SessionManager
  let storage: ReturnType<typeof createMockStorage>

  beforeEach(() => {
    storage = createMockStorage()
    manager = new SessionManager({ storage, maxSessions: 5 })
  })

  afterEach(async () => {
    await manager.dispose()
  })

  // ==========================================================================
  // Create
  // ==========================================================================

  describe('create', () => {
    it('creates a new session with correct defaults', async () => {
      const session = await manager.create('Test Session', '/home/user/project')

      expect(session.id).toMatch(/^session-/)
      expect(session.name).toBe('Test Session')
      expect(session.workingDirectory).toBe('/home/user/project')
      expect(session.messages).toEqual([])
      expect(session.status).toBe('active')
      expect(session.toolCallCount).toBe(0)
      expect(session.createdAt).toBeGreaterThan(0)
      expect(session.updatedAt).toBeGreaterThan(0)
    })

    it('creates session with undefined name', async () => {
      const session = await manager.create(undefined, '/path')
      expect(session.name).toBeUndefined()
    })

    it('auto-persists to storage when no auto-save timer', async () => {
      await manager.create('Persisted', '/path')

      expect(storage.save).toHaveBeenCalledOnce()
    })

    it('generates unique IDs', async () => {
      const s1 = await manager.create('One', '/path')
      const s2 = await manager.create('Two', '/path')

      expect(s1.id).not.toBe(s2.id)
    })
  })

  // ==========================================================================
  // Get
  // ==========================================================================

  describe('get', () => {
    it('returns session from cache', async () => {
      const session = await manager.create('Test', '/path')
      const retrieved = await manager.get(session.id)

      expect(retrieved).not.toBeNull()
      expect(retrieved!.id).toBe(session.id)
    })

    it('returns null for non-existent session', async () => {
      const result = await manager.get('nonexistent')
      expect(result).toBeNull()
    })

    it('loads from storage when not in cache', async () => {
      // Create a session in storage directly
      const serialized: SerializedSessionState = {
        id: 'stored-session',
        messages: [],
        workingDirectory: '/path',
        toolCallCount: 0,
        tokenStats: {
          messages: [],
          total: 0,
          limit: 200000,
          remaining: 200000,
          percentUsed: 0,
        },
        openFiles: [],
        env: {},
        createdAt: Date.now(),
        updatedAt: Date.now(),
        status: 'active',
      }
      storage.saved.set('stored-session', serialized)

      // New manager (no cache)
      const freshManager = new SessionManager({ storage, maxSessions: 5 })
      const session = await freshManager.get('stored-session')

      expect(session).not.toBeNull()
      expect(session!.id).toBe('stored-session')
      expect(storage.load).toHaveBeenCalledWith('stored-session')

      await freshManager.dispose()
    })
  })

  // ==========================================================================
  // Save
  // ==========================================================================

  describe('save', () => {
    it('saves session to storage', async () => {
      const session = await manager.create('Test', '/path')
      vi.mocked(storage.save).mockClear()

      await manager.save(session.id)

      expect(storage.save).toHaveBeenCalledOnce()
      expect(storage.save).toHaveBeenCalledWith(expect.objectContaining({ id: session.id }))
    })

    it('does nothing for non-existent session', async () => {
      vi.mocked(storage.save).mockClear()
      await manager.save('nonexistent')

      expect(storage.save).not.toHaveBeenCalled()
    })

    it('emits session_saved event', async () => {
      const session = await manager.create('Test', '/path')
      const listener = vi.fn()
      manager.on(listener)

      await manager.save(session.id)

      expect(listener).toHaveBeenCalledWith(
        expect.objectContaining({ type: 'session_saved', sessionId: session.id })
      )
    })
  })

  // ==========================================================================
  // Delete
  // ==========================================================================

  describe('delete', () => {
    it('removes session from cache and storage', async () => {
      const session = await manager.create('Test', '/path')
      await manager.delete(session.id)

      const result = await manager.get(session.id)
      expect(result).toBeNull()
      expect(storage.delete).toHaveBeenCalledWith(session.id)
    })

    it('emits session_cleared event', async () => {
      const session = await manager.create('Test', '/path')
      const listener = vi.fn()
      manager.on(listener)

      await manager.delete(session.id)

      expect(listener).toHaveBeenCalledWith(
        expect.objectContaining({ type: 'session_cleared', sessionId: session.id })
      )
    })
  })

  // ==========================================================================
  // List
  // ==========================================================================

  describe('list', () => {
    it('returns metadata for all sessions', async () => {
      await manager.create('First', '/path1')
      await manager.create('Second', '/path2')

      const list = await manager.list()

      expect(list).toHaveLength(2)
      expect(list[0]!.name).toBeDefined()
      expect(list[0]!.messageCount).toBe(0)
    })

    it('returns sessions sorted by updatedAt (most recent first)', async () => {
      const s1 = await manager.create('Old', '/path')
      // Slight delay to ensure different updatedAt
      await new Promise((r) => setTimeout(r, 10))
      const s2 = await manager.create('New', '/path')

      const list = await manager.list()
      expect(list[0]!.id).toBe(s2.id)
      expect(list[1]!.id).toBe(s1.id)
    })

    it('includes sessions from storage not in cache', async () => {
      const serialized: SerializedSessionState = {
        id: 'storage-only',
        name: 'From Storage',
        messages: [],
        workingDirectory: '/path',
        toolCallCount: 0,
        tokenStats: {
          messages: [],
          total: 0,
          limit: 200000,
          remaining: 200000,
          percentUsed: 0,
        },
        openFiles: [],
        env: {},
        createdAt: Date.now() - 10000,
        updatedAt: Date.now() - 10000,
        status: 'completed',
      }
      storage.saved.set('storage-only', serialized)

      const list = await manager.list()
      const storageSession = list.find((m) => m.id === 'storage-only')
      expect(storageSession).toBeDefined()
      expect(storageSession!.name).toBe('From Storage')
    })
  })

  // ==========================================================================
  // LRU Eviction
  // ==========================================================================

  describe('LRU eviction', () => {
    it('evicts oldest sessions when max exceeded', async () => {
      const tinyManager = new SessionManager({ storage, maxSessions: 2 })

      const s1 = await tinyManager.create('First', '/p')
      await tinyManager.create('Second', '/p')
      await tinyManager.create('Third', '/p')

      // s1 should have been evicted from cache
      // (but still in storage since mock storage keeps it)
      await tinyManager.get(s1.id)
      // Should load from storage since evicted from cache
      expect(storage.load).toHaveBeenCalledWith(s1.id)

      await tinyManager.dispose()
    })
  })

  // ==========================================================================
  // Message Management
  // ==========================================================================

  describe('addMessage', () => {
    it('adds message to session', async () => {
      const session = await manager.create('Test', '/path')
      const msg = makeMessage('msg-1', 'user', 'Hello')

      manager.addMessage(session.id, msg)

      const updated = await manager.get(session.id)
      expect(updated!.messages).toHaveLength(1)
      expect(updated!.messages[0]!.content).toBe('Hello')
    })

    it('emits message_added event', async () => {
      const session = await manager.create('Test', '/path')
      const listener = vi.fn()
      manager.on(listener)

      const msg = makeMessage('msg-1', 'user', 'Hello')
      manager.addMessage(session.id, msg)

      expect(listener).toHaveBeenCalledWith(
        expect.objectContaining({
          type: 'message_added',
          messageId: 'msg-1',
          sessionId: session.id,
        })
      )
    })

    it('throws for non-existent session', async () => {
      const msg = makeMessage('msg-1', 'user', 'Hello')
      expect(() => manager.addMessage('nonexistent', msg)).toThrow('Session not found')
    })
  })

  describe('removeMessage', () => {
    it('removes message from session', async () => {
      const session = await manager.create('Test', '/path')
      manager.addMessage(session.id, makeMessage('msg-1', 'user', 'Hello'))
      manager.addMessage(session.id, makeMessage('msg-2', 'assistant', 'Hi'))

      const removed = manager.removeMessage(session.id, 'msg-1')

      expect(removed).toBe(true)
      const updated = await manager.get(session.id)
      expect(updated!.messages).toHaveLength(1)
      expect(updated!.messages[0]!.id).toBe('msg-2')
    })

    it('returns false for non-existent message', async () => {
      const session = await manager.create('Test', '/path')
      expect(manager.removeMessage(session.id, 'nonexistent')).toBe(false)
    })

    it('returns false for non-existent session', () => {
      expect(manager.removeMessage('nonexistent', 'msg-1')).toBe(false)
    })

    it('emits message_removed event', async () => {
      const session = await manager.create('Test', '/path')
      manager.addMessage(session.id, makeMessage('msg-1', 'user', 'Hello'))
      const listener = vi.fn()
      manager.on(listener)

      manager.removeMessage(session.id, 'msg-1')

      expect(listener).toHaveBeenCalledWith(
        expect.objectContaining({
          type: 'message_removed',
          messageId: 'msg-1',
        })
      )
    })
  })

  describe('setMessages', () => {
    it('replaces all messages', async () => {
      const session = await manager.create('Test', '/path')
      manager.addMessage(session.id, makeMessage('msg-1', 'user', 'Hello'))
      manager.addMessage(session.id, makeMessage('msg-2', 'assistant', 'Hi'))

      const newMessages = [makeMessage('msg-3', 'user', 'New conversation')]
      manager.setMessages(session.id, newMessages)

      const updated = await manager.get(session.id)
      expect(updated!.messages).toHaveLength(1)
      expect(updated!.messages[0]!.id).toBe('msg-3')
    })

    it('throws for non-existent session', () => {
      expect(() => manager.setMessages('nonexistent', [])).toThrow('Session not found')
    })
  })

  // ==========================================================================
  // State Updates
  // ==========================================================================

  describe('state updates', () => {
    it('setStatus updates status and emits event', async () => {
      const session = await manager.create('Test', '/path')
      const listener = vi.fn()
      manager.on(listener)

      manager.setStatus(session.id, 'paused')

      const updated = await manager.get(session.id)
      expect(updated!.status).toBe('paused')
      expect(listener).toHaveBeenCalledWith(
        expect.objectContaining({ type: 'status_changed', status: 'paused' })
      )
    })

    it('setStatus stores error message', async () => {
      const session = await manager.create('Test', '/path')
      manager.setStatus(session.id, 'error', 'Something went wrong')

      const updated = await manager.get(session.id)
      expect(updated!.status).toBe('error')
      expect(updated!.errorMessage).toBe('Something went wrong')
    })

    it('incrementToolCalls increments counter', async () => {
      const session = await manager.create('Test', '/path')
      manager.incrementToolCalls(session.id)
      manager.incrementToolCalls(session.id)

      const updated = await manager.get(session.id)
      expect(updated!.toolCallCount).toBe(2)
    })

    it('trackFile / untrackFile manages open files', async () => {
      const session = await manager.create('Test', '/path')
      manager.trackFile(session.id, {
        path: '/src/main.ts',
        content: 'console.log("hi")',
        mtime: Date.now(),
        dirty: false,
      })

      let updated = await manager.get(session.id)
      expect(updated!.openFiles.has('/src/main.ts')).toBe(true)

      manager.untrackFile(session.id, '/src/main.ts')

      updated = await manager.get(session.id)
      expect(updated!.openFiles.has('/src/main.ts')).toBe(false)
    })

    it('setEnv sets environment variable', async () => {
      const session = await manager.create('Test', '/path')
      manager.setEnv(session.id, 'NODE_ENV', 'test')

      const updated = await manager.get(session.id)
      expect(updated!.env.NODE_ENV).toBe('test')
    })

    it('updateTokenStats updates stats', async () => {
      const session = await manager.create('Test', '/path')
      const newStats = {
        messages: new Map([['msg-1', 100]]),
        total: 100,
        limit: 200000,
        remaining: 199900,
        percentUsed: 0.05,
      }
      manager.updateTokenStats(session.id, newStats)

      const updated = await manager.get(session.id)
      expect(updated!.tokenStats.total).toBe(100)
    })
  })

  // ==========================================================================
  // Checkpoints
  // ==========================================================================

  describe('createCheckpoint', () => {
    it('creates checkpoint with snapshot', async () => {
      const session = await manager.create('Test', '/path')
      manager.addMessage(session.id, makeMessage('msg-1', 'user', 'Hello'))

      const checkpoint = await manager.createCheckpoint(session.id, 'Before refactor')

      expect(checkpoint.id).toMatch(/^checkpoint-/)
      expect(checkpoint.description).toBe('Before refactor')
      expect(checkpoint.messageCount).toBe(1)
      expect(checkpoint.stateSnapshot).toBeDefined()
    })

    it('stores checkpoint in session', async () => {
      const session = await manager.create('Test', '/path')
      const checkpoint = await manager.createCheckpoint(session.id, 'cp1')

      const updated = await manager.get(session.id)
      expect(updated!.checkpoint?.id).toBe(checkpoint.id)
      expect(updated!.checkpointIds).toContain(checkpoint.id)
    })

    it('persists checkpoint to storage', async () => {
      const session = await manager.create('Test', '/path')
      await manager.createCheckpoint(session.id, 'cp1')

      expect(storage.saveCheckpoint).toHaveBeenCalledOnce()
    })

    it('emits checkpoint_created event', async () => {
      const session = await manager.create('Test', '/path')
      const listener = vi.fn()
      manager.on(listener)

      const checkpoint = await manager.createCheckpoint(session.id, 'cp1')

      expect(listener).toHaveBeenCalledWith(
        expect.objectContaining({
          type: 'checkpoint_created',
          checkpointId: checkpoint.id,
          sessionId: session.id,
        })
      )
    })

    it('throws for non-existent session', async () => {
      await expect(manager.createCheckpoint('nonexistent', 'cp1')).rejects.toThrow(
        'Session not found'
      )
    })
  })

  describe('rollbackToCheckpoint', () => {
    it('restores session state from checkpoint', async () => {
      const session = await manager.create('Test', '/path')
      manager.addMessage(session.id, makeMessage('msg-1', 'user', 'Original'))

      const checkpoint = await manager.createCheckpoint(session.id, 'Before change')

      // Make changes after checkpoint
      manager.addMessage(session.id, makeMessage('msg-2', 'user', 'After checkpoint'))
      manager.setEnv(session.id, 'KEY', 'value')

      // Rollback
      await manager.rollbackToCheckpoint(session.id, checkpoint.id)

      const restored = await manager.get(session.id)
      expect(restored!.messages).toHaveLength(1)
      expect(restored!.messages[0]!.content).toBe('Original')
    })

    it('emits checkpoint_restored event', async () => {
      const session = await manager.create('Test', '/path')
      const checkpoint = await manager.createCheckpoint(session.id, 'cp1')
      const listener = vi.fn()
      manager.on(listener)

      await manager.rollbackToCheckpoint(session.id, checkpoint.id)

      expect(listener).toHaveBeenCalledWith(
        expect.objectContaining({
          type: 'checkpoint_restored',
          checkpointId: checkpoint.id,
        })
      )
    })

    it('throws for non-existent checkpoint', async () => {
      const session = await manager.create('Test', '/path')
      await expect(manager.rollbackToCheckpoint(session.id, 'nonexistent')).rejects.toThrow(
        'Checkpoint not found'
      )
    })
  })

  describe('listCheckpoints', () => {
    it('returns checkpoint metadata from storage', async () => {
      const session = await manager.create('Test', '/path')
      await manager.createCheckpoint(session.id, 'First')
      await manager.createCheckpoint(session.id, 'Second')

      const list = await manager.listCheckpoints(session.id)

      expect(list).toHaveLength(2)
      expect(list[0]!.description).toBeDefined()
    })

    it('returns single checkpoint from memory when no storage', async () => {
      const noStorageManager = new SessionManager({ maxSessions: 5 })
      const session = await noStorageManager.create('Test', '/path')
      await noStorageManager.createCheckpoint(session.id, 'Memory only')

      const list = await noStorageManager.listCheckpoints(session.id)
      expect(list).toHaveLength(1)

      await noStorageManager.dispose()
    })

    it('returns empty for session with no checkpoints', async () => {
      const noStorageManager = new SessionManager({ maxSessions: 5 })
      const session = await noStorageManager.create('Test', '/path')

      const list = await noStorageManager.listCheckpoints(session.id)
      expect(list).toEqual([])

      await noStorageManager.dispose()
    })
  })

  describe('deleteCheckpoint', () => {
    it('removes checkpoint from session and storage', async () => {
      const session = await manager.create('Test', '/path')
      const checkpoint = await manager.createCheckpoint(session.id, 'To delete')

      await manager.deleteCheckpoint(session.id, checkpoint.id)

      const updated = await manager.get(session.id)
      expect(updated!.checkpoint).toBeUndefined()
      expect(updated!.checkpointIds).not.toContain(checkpoint.id)
      expect(storage.deleteCheckpoint).toHaveBeenCalledWith(session.id, checkpoint.id)
    })
  })

  // ==========================================================================
  // Fork
  // ==========================================================================

  describe('fork', () => {
    it('creates forked session from checkpoint', async () => {
      const session = await manager.create('Original', '/path')
      manager.addMessage(session.id, makeMessage('msg-1', 'user', 'Hello'))
      const checkpoint = await manager.createCheckpoint(session.id, 'Fork point')

      const forked = await manager.fork(session.id, {
        checkpointId: checkpoint.id,
      })

      expect(forked.id).not.toBe(session.id)
      expect(forked.parentId).toBe(session.id)
      expect(forked.name).toContain('fork #')
      expect(forked.messages).toHaveLength(1)
      expect(forked.status).toBe('active')
    })

    it('fork emits session_forked event', async () => {
      const session = await manager.create('Original', '/path')
      const checkpoint = await manager.createCheckpoint(session.id, 'Fork')
      const listener = vi.fn()
      manager.on(listener)

      const forked = await manager.fork(session.id, { checkpointId: checkpoint.id })

      expect(listener).toHaveBeenCalledWith(
        expect.objectContaining({
          type: 'session_forked',
          sessionId: forked.id,
          parentId: session.id,
        })
      )
    })

    it('fork with custom name', async () => {
      const session = await manager.create('Original', '/path')
      const checkpoint = await manager.createCheckpoint(session.id, 'Fork')

      const forked = await manager.fork(session.id, {
        checkpointId: checkpoint.id,
        name: 'My Fork',
      })

      expect(forked.name).toBe('My Fork')
    })

    it('fork truncates at messageId', async () => {
      const session = await manager.create('Original', '/path')
      manager.addMessage(session.id, makeMessage('msg-1', 'user', 'First'))
      manager.addMessage(session.id, makeMessage('msg-2', 'assistant', 'Second'))
      manager.addMessage(session.id, makeMessage('msg-3', 'user', 'Third'))
      const checkpoint = await manager.createCheckpoint(session.id, 'Fork')

      const forked = await manager.fork(session.id, {
        checkpointId: checkpoint.id,
        messageId: 'msg-2',
      })

      expect(forked.messages).toHaveLength(2)
      expect(forked.messages[1]!.id).toBe('msg-2')
    })

    it('throws for non-existent session', async () => {
      await expect(manager.fork('nonexistent', { checkpointId: 'cp' })).rejects.toThrow(
        'Session not found'
      )
    })

    it('throws for non-existent checkpoint', async () => {
      const session = await manager.create('Original', '/path')

      await expect(manager.fork(session.id, { checkpointId: 'nonexistent' })).rejects.toThrow(
        'Checkpoint not found'
      )
    })
  })

  // ==========================================================================
  // Events
  // ==========================================================================

  describe('events', () => {
    it('on() returns unsubscribe function', async () => {
      const session = await manager.create('Test', '/path')
      const listener = vi.fn()
      const unsub = manager.on(listener)

      manager.addMessage(session.id, makeMessage('msg-1', 'user', 'Hello'))
      expect(listener).toHaveBeenCalledOnce()

      unsub()
      manager.addMessage(session.id, makeMessage('msg-2', 'user', 'World'))
      expect(listener).toHaveBeenCalledOnce() // not called again
    })

    it('listener error does not prevent other listeners', async () => {
      const session = await manager.create('Test', '/path')
      const errorListener = vi.fn(() => {
        throw new Error('boom')
      })
      const goodListener = vi.fn()
      manager.on(errorListener)
      manager.on(goodListener)

      manager.addMessage(session.id, makeMessage('msg-1', 'user', 'Hello'))

      expect(errorListener).toHaveBeenCalledOnce()
      expect(goodListener).toHaveBeenCalledOnce()
    })
  })

  // ==========================================================================
  // Serialization
  // ==========================================================================

  describe('serialization round-trip', () => {
    it('preserves session state through serialize/deserialize', async () => {
      const session = await manager.create('Roundtrip Test', '/home/user/project')
      manager.addMessage(session.id, makeMessage('msg-1', 'user', 'Hello'))
      manager.trackFile(session.id, {
        path: '/src/main.ts',
        content: 'code',
        mtime: 1000,
        dirty: false,
      })
      manager.setEnv(session.id, 'NODE_ENV', 'test')

      // Save forces serialization
      await manager.save(session.id)

      // Create fresh manager and load
      const freshManager = new SessionManager({ storage, maxSessions: 5 })
      const loaded = await freshManager.get(session.id)

      expect(loaded).not.toBeNull()
      expect(loaded!.name).toBe('Roundtrip Test')
      expect(loaded!.messages).toHaveLength(1)
      expect(loaded!.openFiles.has('/src/main.ts')).toBe(true)
      expect(loaded!.env.NODE_ENV).toBe('test')

      await freshManager.dispose()
    })
  })

  // ==========================================================================
  // Cleanup
  // ==========================================================================

  describe('dispose', () => {
    it('saves dirty sessions on dispose', async () => {
      const session = await manager.create('Dirty', '/path')
      manager.addMessage(session.id, makeMessage('msg-1', 'user', 'Unsaved'))

      vi.mocked(storage.save).mockClear()
      await manager.dispose()

      expect(storage.save).toHaveBeenCalled()
    })
  })
})

// ============================================================================
// Factory
// ============================================================================

describe('createSessionManager', () => {
  it('creates independent instance', () => {
    const a = createSessionManager()
    const b = createSessionManager()
    expect(a).not.toBe(b)
  })

  it('accepts config', () => {
    const manager = createSessionManager({ maxSessions: 3 })
    expect(manager).toBeInstanceOf(SessionManager)
  })
})
