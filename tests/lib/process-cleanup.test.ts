/**
 * Tests for Delta9 Process Cleanup Manager
 */

import { describe, it, expect, vi, beforeEach, afterEach } from 'vitest'
import {
  ProcessCleanupManager,
  getCleanupManager,
  registerCleanup,
  unregisterCleanup,
  shutdown,
  CleanupPriority,
} from '../../src/lib/process-cleanup.js'

describe('ProcessCleanupManager', () => {
  let manager: ProcessCleanupManager

  beforeEach(() => {
    ProcessCleanupManager.clearInstance()
    manager = new ProcessCleanupManager({
      exitOnSignal: false, // Don't exit during tests
    })
  })

  afterEach(() => {
    manager.reset()
  })

  describe('handler registration', () => {
    it('registers a handler', () => {
      const handler = vi.fn()
      manager.register({ name: 'test', priority: 50, handler })

      expect(manager.has('test')).toBe(true)
      expect(manager.getHandlerNames()).toContain('test')
    })

    it('replaces handler with same name', () => {
      const handler1 = vi.fn()
      const handler2 = vi.fn()

      manager.register({ name: 'test', priority: 50, handler: handler1 })
      manager.register({ name: 'test', priority: 50, handler: handler2 })

      expect(manager.getHandlerNames()).toHaveLength(1)
    })

    it('unregisters a handler', () => {
      manager.register({ name: 'test', priority: 50, handler: vi.fn() })
      expect(manager.has('test')).toBe(true)

      const result = manager.unregister('test')
      expect(result).toBe(true)
      expect(manager.has('test')).toBe(false)
    })

    it('returns false when unregistering non-existent handler', () => {
      const result = manager.unregister('non-existent')
      expect(result).toBe(false)
    })
  })

  describe('shutdown', () => {
    it('executes handlers in priority order', async () => {
      const order: number[] = []

      manager.register({
        name: 'high',
        priority: 10,
        handler: () => { order.push(10) },
      })
      manager.register({
        name: 'low',
        priority: 100,
        handler: () => { order.push(100) },
      })
      manager.register({
        name: 'medium',
        priority: 50,
        handler: () => { order.push(50) },
      })

      await manager.shutdown('test')

      expect(order).toEqual([10, 50, 100])
    })

    it('handles async handlers', async () => {
      const handler = vi.fn().mockImplementation(async () => {
        await new Promise(r => setTimeout(r, 10))
      })

      manager.register({ name: 'async', priority: 50, handler })
      await manager.shutdown('test')

      expect(handler).toHaveBeenCalled()
    })

    it('handles handler errors gracefully', async () => {
      const successHandler = vi.fn()
      const errorHandler = vi.fn().mockRejectedValue(new Error('test error'))

      manager.register({ name: 'error', priority: 10, handler: errorHandler })
      manager.register({ name: 'success', priority: 20, handler: successHandler })

      // Should not throw
      await manager.shutdown('test')

      // Both handlers should be called
      expect(errorHandler).toHaveBeenCalled()
      expect(successHandler).toHaveBeenCalled()
    })

    it('times out slow handlers', async () => {
      const slowHandler = vi.fn().mockImplementation(async () => {
        await new Promise(r => setTimeout(r, 10000)) // 10 seconds
      })

      manager.register({
        name: 'slow',
        priority: 50,
        handler: slowHandler,
        timeout: 100, // 100ms timeout
      })

      const start = Date.now()
      await manager.shutdown('test')
      const duration = Date.now() - start

      // Should complete quickly (timeout triggered)
      expect(duration).toBeLessThan(500)
    })

    it('prevents double shutdown', async () => {
      const handler = vi.fn()
      manager.register({ name: 'test', priority: 50, handler })

      await manager.shutdown('first')
      await manager.shutdown('second')

      // Handler only called once
      expect(handler).toHaveBeenCalledTimes(1)
    })

    it('reports shutdown state', async () => {
      expect(manager.isShutdown()).toBe(false)
      await manager.shutdown('test')
      expect(manager.isShutdown()).toBe(true)
    })
  })

  describe('reset', () => {
    it('resets shutdown state and clears handlers', async () => {
      manager.register({ name: 'test', priority: 50, handler: vi.fn() })
      await manager.shutdown('test')

      expect(manager.isShutdown()).toBe(true)
      expect(manager.has('test')).toBe(true)

      manager.reset()

      expect(manager.isShutdown()).toBe(false)
      expect(manager.has('test')).toBe(false)
    })
  })
})

describe('singleton functions', () => {
  beforeEach(() => {
    ProcessCleanupManager.clearInstance()
  })

  afterEach(() => {
    ProcessCleanupManager.clearInstance()
  })

  it('getCleanupManager returns singleton', () => {
    const manager1 = getCleanupManager()
    const manager2 = getCleanupManager()
    expect(manager1).toBe(manager2)
  })

  it('registerCleanup adds handler to singleton', () => {
    registerCleanup('test', vi.fn(), 50)
    expect(getCleanupManager().has('test')).toBe(true)
  })

  it('unregisterCleanup removes handler from singleton', () => {
    registerCleanup('test', vi.fn(), 50)
    expect(unregisterCleanup('test')).toBe(true)
    expect(getCleanupManager().has('test')).toBe(false)
  })

  it('shutdown triggers singleton shutdown', async () => {
    const handler = vi.fn()
    registerCleanup('test', handler, 50)
    await shutdown('test')
    expect(handler).toHaveBeenCalled()
  })
})

describe('CleanupPriority', () => {
  it('defines priority levels in order', () => {
    expect(CleanupPriority.CRITICAL).toBeLessThan(CleanupPriority.BACKGROUND)
    expect(CleanupPriority.BACKGROUND).toBeLessThan(CleanupPriority.STATE)
    expect(CleanupPriority.STATE).toBeLessThan(CleanupPriority.LOGGING)
    expect(CleanupPriority.LOGGING).toBeLessThan(CleanupPriority.DEFAULT)
    expect(CleanupPriority.DEFAULT).toBeLessThan(CleanupPriority.CLEANUP)
    expect(CleanupPriority.CLEANUP).toBeLessThan(CleanupPriority.FINAL)
  })
})
