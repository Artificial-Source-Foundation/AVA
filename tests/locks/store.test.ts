/**
 * Tests for Delta9 Lock Store
 */

import { describe, it, expect, beforeEach, vi } from 'vitest'
import { LockStore, resetLockStore } from '../../src/locks/store.js'
import type { LockOwner, LockEvent } from '../../src/locks/types.js'

describe('LockStore', () => {
  let store: LockStore

  beforeEach(() => {
    resetLockStore()
    store = new LockStore({ enableAutoCleanup: false })
  })

  const owner1: LockOwner = { id: 'agent-1', name: 'Agent 1' }
  const owner2: LockOwner = { id: 'agent-2', name: 'Agent 2' }

  describe('acquire', () => {
    it('should acquire lock on unlocked file', () => {
      const result = store.acquire('/test/file.ts', { owner: owner1 })

      expect(result.success).toBe(true)
      expect(result.lock).toBeDefined()
      expect(result.lock!.filePath).toContain('file.ts')
      expect(result.lock!.owner.id).toBe('agent-1')
      expect(result.lock!.version).toBeGreaterThan(0)
    })

    it('should fail when file is locked by another owner', () => {
      store.acquire('/test/file.ts', { owner: owner1 })
      const result = store.acquire('/test/file.ts', { owner: owner2 })

      expect(result.success).toBe(false)
      expect(result.error).toContain('locked by')
      expect(result.blockedBy).toBeDefined()
      expect(result.blockedBy!.owner.id).toBe('agent-1')
    })

    it('should extend lock for same owner', () => {
      const first = store.acquire('/test/file.ts', { owner: owner1 })
      const second = store.acquire('/test/file.ts', { owner: owner1 })

      expect(second.success).toBe(true)
      expect(second.lock!.version).toBeGreaterThan(first.lock!.version)
    })

    it('should respect expectedVersion for CAS', () => {
      const first = store.acquire('/test/file.ts', { owner: owner1 })
      const wrongVersion = store.acquire('/test/file.ts', {
        owner: owner1,
        expectedVersion: first.lock!.version + 999,
      })

      expect(wrongVersion.success).toBe(false)
      expect(wrongVersion.error).toContain('Version mismatch')
    })

    it('should auto-release expired locks', async () => {
      store.acquire('/test/file.ts', { owner: owner1, ttlMs: 10 })

      // Wait for lock to expire
      await new Promise((resolve) => setTimeout(resolve, 20))

      const result = store.acquire('/test/file.ts', { owner: owner2 })
      expect(result.success).toBe(true)
    })

    it('should enforce max locks per owner', () => {
      const store = new LockStore({ maxLocksPerOwner: 2, enableAutoCleanup: false })

      store.acquire('/test/file1.ts', { owner: owner1 })
      store.acquire('/test/file2.ts', { owner: owner1 })
      const third = store.acquire('/test/file3.ts', { owner: owner1 })

      expect(third.success).toBe(false)
      expect(third.error).toContain('max locks')
    })

    it('should include reason in lock', () => {
      const result = store.acquire('/test/file.ts', {
        owner: owner1,
        reason: 'Editing for feature X',
      })

      expect(result.lock!.reason).toBe('Editing for feature X')
    })
  })

  describe('release', () => {
    it('should release owned lock', () => {
      store.acquire('/test/file.ts', { owner: owner1 })
      const result = store.release('/test/file.ts', { owner: owner1 })

      expect(result.success).toBe(true)
      expect(store.isLocked('/test/file.ts')).toBe(false)
    })

    it('should fail to release lock owned by another', () => {
      store.acquire('/test/file.ts', { owner: owner1 })
      const result = store.release('/test/file.ts', { owner: owner2 })

      expect(result.success).toBe(false)
      expect(result.error).toContain('Cannot release')
    })

    it('should allow force release', () => {
      store.acquire('/test/file.ts', { owner: owner1 })
      const result = store.release('/test/file.ts', { owner: owner2, force: true })

      expect(result.success).toBe(true)
      expect(store.isLocked('/test/file.ts')).toBe(false)
    })

    it('should succeed on already unlocked file', () => {
      const result = store.release('/test/file.ts', { owner: owner1 })

      expect(result.success).toBe(true)
    })

    it('should respect expectedVersion for CAS', () => {
      const lock = store.acquire('/test/file.ts', { owner: owner1 })
      const result = store.release('/test/file.ts', {
        owner: owner1,
        expectedVersion: lock.lock!.version + 999,
      })

      expect(result.success).toBe(false)
      expect(result.error).toContain('Version mismatch')
    })
  })

  describe('isLocked', () => {
    it('should return false for unlocked file', () => {
      expect(store.isLocked('/test/file.ts')).toBe(false)
    })

    it('should return true for locked file', () => {
      store.acquire('/test/file.ts', { owner: owner1 })

      expect(store.isLocked('/test/file.ts')).toBe(true)
    })

    it('should return false for expired lock', async () => {
      store.acquire('/test/file.ts', { owner: owner1, ttlMs: 10 })

      await new Promise((resolve) => setTimeout(resolve, 20))

      expect(store.isLocked('/test/file.ts')).toBe(false)
    })
  })

  describe('getLock', () => {
    it('should return null for unlocked file', () => {
      expect(store.getLock('/test/file.ts')).toBeNull()
    })

    it('should return lock info for locked file', () => {
      store.acquire('/test/file.ts', { owner: owner1, reason: 'Testing' })
      const lock = store.getLock('/test/file.ts')

      expect(lock).toBeDefined()
      expect(lock!.owner.id).toBe('agent-1')
      expect(lock!.reason).toBe('Testing')
    })
  })

  describe('getLocksForOwner', () => {
    it('should return empty array for owner with no locks', () => {
      const locks = store.getLocksForOwner('unknown')

      expect(locks).toEqual([])
    })

    it('should return all locks for owner', () => {
      store.acquire('/test/file1.ts', { owner: owner1 })
      store.acquire('/test/file2.ts', { owner: owner1 })
      store.acquire('/test/file3.ts', { owner: owner2 })

      const locks = store.getLocksForOwner('agent-1')

      expect(locks).toHaveLength(2)
      expect(locks.every((l) => l.owner.id === 'agent-1')).toBe(true)
    })
  })

  describe('getAllLocks', () => {
    it('should return empty array when no locks', () => {
      const locks = store.getAllLocks()

      expect(locks).toEqual([])
    })

    it('should return all active locks', () => {
      store.acquire('/test/file1.ts', { owner: owner1 })
      store.acquire('/test/file2.ts', { owner: owner2 })

      const locks = store.getAllLocks()

      expect(locks).toHaveLength(2)
    })
  })

  describe('releaseAllForOwner', () => {
    it('should release all locks for owner', () => {
      store.acquire('/test/file1.ts', { owner: owner1 })
      store.acquire('/test/file2.ts', { owner: owner1 })
      store.acquire('/test/file3.ts', { owner: owner2 })

      const count = store.releaseAllForOwner('agent-1')

      expect(count).toBe(2)
      expect(store.getLocksForOwner('agent-1')).toHaveLength(0)
      expect(store.getLocksForOwner('agent-2')).toHaveLength(1)
    })
  })

  describe('acquireMultiple', () => {
    it('should acquire all locks atomically', () => {
      const result = store.acquireMultiple(['/test/file1.ts', '/test/file2.ts', '/test/file3.ts'], { owner: owner1 })

      expect(result.success).toBe(true)
      expect(store.getLocksForOwner('agent-1')).toHaveLength(3)
    })

    it('should rollback on partial failure', () => {
      store.acquire('/test/file2.ts', { owner: owner2 })

      const result = store.acquireMultiple(['/test/file1.ts', '/test/file2.ts', '/test/file3.ts'], { owner: owner1 })

      expect(result.success).toBe(false)
      expect(result.blockedBy).toBeDefined()
      // Rollback should have released file1 and file3
      expect(store.getLocksForOwner('agent-1')).toHaveLength(0)
    })
  })

  describe('releaseMultiple', () => {
    it('should release multiple locks', () => {
      store.acquire('/test/file1.ts', { owner: owner1 })
      store.acquire('/test/file2.ts', { owner: owner1 })

      const result = store.releaseMultiple(['/test/file1.ts', '/test/file2.ts'], { owner: owner1 })

      expect(result.success).toBe(true)
      expect(store.getAllLocks()).toHaveLength(0)
    })
  })

  describe('events', () => {
    it('should emit acquired event', () => {
      const events: LockEvent[] = []
      store.on((event) => events.push(event))

      store.acquire('/test/file.ts', { owner: owner1 })

      expect(events).toHaveLength(1)
      expect(events[0].type).toBe('acquired')
      expect(events[0].owner.id).toBe('agent-1')
    })

    it('should emit released event', () => {
      const events: LockEvent[] = []
      store.on((event) => events.push(event))

      store.acquire('/test/file.ts', { owner: owner1 })
      store.release('/test/file.ts', { owner: owner1 })

      expect(events).toHaveLength(2)
      expect(events[1].type).toBe('released')
    })

    it('should emit extended event', () => {
      const events: LockEvent[] = []
      store.on((event) => events.push(event))

      store.acquire('/test/file.ts', { owner: owner1 })
      store.acquire('/test/file.ts', { owner: owner1 })

      expect(events).toHaveLength(2)
      expect(events[1].type).toBe('extended')
    })

    it('should emit blocked event', () => {
      const events: LockEvent[] = []
      store.on((event) => events.push(event))

      store.acquire('/test/file.ts', { owner: owner1 })
      store.acquire('/test/file.ts', { owner: owner2 })

      expect(events.find((e) => e.type === 'blocked')).toBeDefined()
    })

    it('should emit expired event', async () => {
      const events: LockEvent[] = []
      store.on((event) => events.push(event))

      store.acquire('/test/file.ts', { owner: owner1, ttlMs: 10 })

      await new Promise((resolve) => setTimeout(resolve, 20))

      // Trigger expiration check
      store.isLocked('/test/file.ts')

      expect(events.find((e) => e.type === 'expired')).toBeDefined()
    })

    it('should support removing listeners', () => {
      const events: LockEvent[] = []
      const listener = (event: LockEvent) => events.push(event)

      store.on(listener)
      store.acquire('/test/file1.ts', { owner: owner1 })

      store.off(listener)
      store.acquire('/test/file2.ts', { owner: owner1 })

      expect(events).toHaveLength(1)
    })
  })

  describe('cleanupExpired', () => {
    it('should clean up expired locks', async () => {
      store.acquire('/test/file1.ts', { owner: owner1, ttlMs: 10 })
      store.acquire('/test/file2.ts', { owner: owner1, ttlMs: 10 })
      store.acquire('/test/file3.ts', { owner: owner1, ttlMs: 60000 })

      await new Promise((resolve) => setTimeout(resolve, 20))

      const count = store.cleanupExpired()

      expect(count).toBe(2)
      expect(store.getAllLocks()).toHaveLength(1)
    })
  })

  describe('getStats', () => {
    it('should return correct statistics', () => {
      store.acquire('/test/file1.ts', { owner: owner1 })
      store.acquire('/test/file2.ts', { owner: owner1 })
      store.acquire('/test/file3.ts', { owner: owner2 })

      const stats = store.getStats()

      expect(stats.totalLocks).toBe(3)
      expect(stats.ownerCounts.get('agent-1')).toBe(2)
      expect(stats.ownerCounts.get('agent-2')).toBe(1)
      expect(stats.oldestLock).toBeDefined()
      expect(stats.newestLock).toBeDefined()
    })
  })

  describe('destroy', () => {
    it('should clear all state', () => {
      store.acquire('/test/file.ts', { owner: owner1 })
      store.on(() => {})

      store.destroy()

      expect(store.getAllLocks()).toHaveLength(0)
    })
  })

  describe('path normalization', () => {
    it('should normalize relative paths', () => {
      store.acquire('./file.ts', { owner: owner1 })
      const isLocked = store.isLocked('./file.ts')

      expect(isLocked).toBe(true)
    })

    it('should treat equivalent paths as same file', () => {
      store.acquire('/test/./file.ts', { owner: owner1 })
      const isLocked = store.isLocked('/test/file.ts')

      expect(isLocked).toBe(true)
    })
  })
})
