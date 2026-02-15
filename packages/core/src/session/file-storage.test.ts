/**
 * File Session Storage Tests
 */

import { rm } from 'node:fs/promises'
import { tmpdir } from 'node:os'
import { join } from 'node:path'
import { afterEach, beforeEach, describe, expect, it } from 'vitest'
import { FileSessionStorage } from './file-storage.js'
import type { Checkpoint, SerializedSessionState } from './types.js'

// ============================================================================
// Test Fixtures
// ============================================================================

function createTestSession(
  overrides: Partial<SerializedSessionState> = {}
): SerializedSessionState {
  return {
    id: `session-${Date.now()}-abc123`,
    name: 'Test Session',
    messages: [],
    workingDirectory: '/tmp/test',
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
    ...overrides,
  }
}

function createTestCheckpoint(overrides: Partial<Checkpoint> = {}): Checkpoint {
  return {
    id: `checkpoint-${Date.now()}-abcd`,
    timestamp: Date.now(),
    description: 'Test checkpoint',
    messageCount: 5,
    ...overrides,
  }
}

// ============================================================================
// Tests
// ============================================================================

describe('FileSessionStorage', () => {
  let storage: FileSessionStorage
  let testDir: string

  beforeEach(async () => {
    testDir = join(tmpdir(), `ava-test-${Date.now()}-${Math.random().toString(36).slice(2, 8)}`)
    storage = new FileSessionStorage(testDir)
  })

  afterEach(async () => {
    try {
      await rm(testDir, { recursive: true })
    } catch {
      // Ignore cleanup errors
    }
  })

  // =========================================================================
  // Session CRUD
  // =========================================================================

  describe('session operations', () => {
    it('should save and load a session', async () => {
      const session = createTestSession({ id: 'session-1' })

      await storage.save(session)
      const loaded = await storage.load('session-1')

      expect(loaded).not.toBeNull()
      expect(loaded!.id).toBe('session-1')
      expect(loaded!.name).toBe('Test Session')
      expect(loaded!.status).toBe('active')
    })

    it('should return null for non-existent session', async () => {
      const loaded = await storage.load('nonexistent')
      expect(loaded).toBeNull()
    })

    it('should overwrite existing session on save', async () => {
      const session = createTestSession({ id: 'session-1', name: 'Original' })
      await storage.save(session)

      session.name = 'Updated'
      await storage.save(session)

      const loaded = await storage.load('session-1')
      expect(loaded!.name).toBe('Updated')
    })

    it('should delete a session', async () => {
      const session = createTestSession({ id: 'session-1' })
      await storage.save(session)

      await storage.delete('session-1')
      const loaded = await storage.load('session-1')
      expect(loaded).toBeNull()
    })

    it('should not throw when deleting non-existent session', async () => {
      await expect(storage.delete('nonexistent')).resolves.not.toThrow()
    })

    it('should list session IDs', async () => {
      await storage.save(createTestSession({ id: 'session-a' }))
      await storage.save(createTestSession({ id: 'session-b' }))
      await storage.save(createTestSession({ id: 'session-c' }))

      const ids = await storage.list()
      expect(ids).toHaveLength(3)
      expect(ids).toContain('session-a')
      expect(ids).toContain('session-b')
      expect(ids).toContain('session-c')
    })

    it('should return empty list when no sessions exist', async () => {
      const ids = await storage.list()
      expect(ids).toHaveLength(0)
    })
  })

  // =========================================================================
  // Checkpoint Operations
  // =========================================================================

  describe('checkpoint operations', () => {
    it('should save and load a checkpoint', async () => {
      const checkpoint = createTestCheckpoint({ id: 'cp-1' })

      await storage.saveCheckpoint('session-1', checkpoint)
      const loaded = await storage.loadCheckpoint('session-1', 'cp-1')

      expect(loaded).not.toBeNull()
      expect(loaded!.id).toBe('cp-1')
      expect(loaded!.description).toBe('Test checkpoint')
    })

    it('should return null for non-existent checkpoint', async () => {
      const loaded = await storage.loadCheckpoint('session-1', 'nonexistent')
      expect(loaded).toBeNull()
    })

    it('should delete a checkpoint', async () => {
      const checkpoint = createTestCheckpoint({ id: 'cp-1' })
      await storage.saveCheckpoint('session-1', checkpoint)

      await storage.deleteCheckpoint('session-1', 'cp-1')
      const loaded = await storage.loadCheckpoint('session-1', 'cp-1')
      expect(loaded).toBeNull()
    })

    it('should list checkpoints for a session', async () => {
      const cp1 = createTestCheckpoint({ id: 'cp-1', timestamp: 1000 })
      const cp2 = createTestCheckpoint({ id: 'cp-2', timestamp: 2000 })

      await storage.saveCheckpoint('session-1', cp1)
      await storage.saveCheckpoint('session-1', cp2)

      const metas = await storage.listCheckpoints('session-1')
      expect(metas).toHaveLength(2)
      expect(metas[0]!.id).toBe('cp-1')
      expect(metas[1]!.id).toBe('cp-2')
    })

    it('should return empty list for session with no checkpoints', async () => {
      const metas = await storage.listCheckpoints('nonexistent')
      expect(metas).toHaveLength(0)
    })

    it('should delete checkpoints when session is deleted', async () => {
      const session = createTestSession({ id: 'session-1' })
      await storage.save(session)

      await storage.saveCheckpoint('session-1', createTestCheckpoint({ id: 'cp-1' }))
      await storage.saveCheckpoint('session-1', createTestCheckpoint({ id: 'cp-2' }))

      await storage.delete('session-1')

      const metas = await storage.listCheckpoints('session-1')
      expect(metas).toHaveLength(0)
    })
  })

  // =========================================================================
  // Bulk Operations
  // =========================================================================

  describe('bulk operations', () => {
    it('should list session metas sorted by updatedAt', async () => {
      await storage.save(createTestSession({ id: 'old', updatedAt: 1000 }))
      await storage.save(createTestSession({ id: 'new', updatedAt: 3000 }))
      await storage.save(createTestSession({ id: 'mid', updatedAt: 2000 }))

      const metas = await storage.listSessionMetas()
      expect(metas).toHaveLength(3)
      expect(metas[0]!.id).toBe('new')
      expect(metas[1]!.id).toBe('mid')
      expect(metas[2]!.id).toBe('old')
    })

    it('should report storage size', async () => {
      await storage.save(createTestSession({ id: 'session-1' }))

      const size = await storage.getStorageSize()
      expect(size).toBeGreaterThan(0)
    })
  })

  // =========================================================================
  // Edge Cases
  // =========================================================================

  describe('edge cases', () => {
    it('should handle session data with messages', async () => {
      const session = createTestSession({
        id: 'session-with-messages',
        messages: [
          { id: 'msg-1', role: 'user', content: 'Hello', timestamp: Date.now() },
          { id: 'msg-2', role: 'assistant', content: 'Hi there', timestamp: Date.now() },
        ] as SerializedSessionState['messages'],
      })

      await storage.save(session)
      const loaded = await storage.load('session-with-messages')

      expect(loaded!.messages).toHaveLength(2)
      expect(loaded!.messages[0]!.content).toBe('Hello')
    })

    it('should preserve env and openFiles', async () => {
      const session = createTestSession({
        id: 'session-env',
        env: { NODE_ENV: 'test', FOO: 'bar' },
        openFiles: [
          [
            '/tmp/file.ts',
            {
              path: '/tmp/file.ts',
              content: 'test',
              mtime: Date.now(),
              dirty: false,
            },
          ],
        ],
      })

      await storage.save(session)
      const loaded = await storage.load('session-env')

      expect(loaded!.env).toEqual({ NODE_ENV: 'test', FOO: 'bar' })
      expect(loaded!.openFiles).toHaveLength(1)
      expect(loaded!.openFiles[0]![0]).toBe('/tmp/file.ts')
    })

    it('should create directories on first operation', async () => {
      const deepDir = join(testDir, 'a', 'b', 'c')
      const deepStorage = new FileSessionStorage(deepDir)

      await deepStorage.save(createTestSession({ id: 'deep-session' }))
      const loaded = await deepStorage.load('deep-session')
      expect(loaded).not.toBeNull()
    })
  })
})
