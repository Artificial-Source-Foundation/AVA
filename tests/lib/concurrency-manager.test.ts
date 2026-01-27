/**
 * Tests for Delta9 Provider Concurrency Manager
 */

import { describe, it, expect, vi, beforeEach, afterEach } from 'vitest'
import {
  ProviderConcurrencyManager,
  getConcurrencyManager,
  clearConcurrencyManager,
  withConcurrencySlot,
  describeConcurrencyStatus,
} from '../../src/lib/concurrency-manager.js'

describe('ProviderConcurrencyManager', () => {
  let manager: ProviderConcurrencyManager

  beforeEach(() => {
    manager = new ProviderConcurrencyManager({
      limits: { anthropic: 2, openai: 3 },
      defaultLimit: 1,
      queueTimeout: 1000, // Short timeout for tests
    })
  })

  afterEach(() => {
    manager.clear()
  })

  describe('configuration', () => {
    it('returns configured limits', () => {
      expect(manager.getLimit('anthropic')).toBe(2)
      expect(manager.getLimit('openai')).toBe(3)
    })

    it('returns default limit for unknown providers', () => {
      expect(manager.getLimit('unknown-provider')).toBe(1)
    })

    it('allows updating limits', () => {
      manager.setLimit('anthropic', 5)
      expect(manager.getLimit('anthropic')).toBe(5)
    })

    it('extracts provider from model string', () => {
      expect(manager.extractProvider('anthropic/claude-sonnet-4')).toBe('anthropic')
      expect(manager.extractProvider('openai/gpt-4o')).toBe('openai')
      expect(manager.extractProvider('unknown')).toBe('unknown')
    })
  })

  describe('slot acquisition', () => {
    it('acquires slot immediately when available', async () => {
      const release = await manager.acquire('anthropic/claude-sonnet-4', 'session-1')
      expect(manager.getActiveCount('anthropic')).toBe(1)
      release()
      expect(manager.getActiveCount('anthropic')).toBe(0)
    })

    it('acquires multiple slots up to limit', async () => {
      const release1 = await manager.acquire('anthropic/claude-sonnet-4', 'session-1')
      const release2 = await manager.acquire('anthropic/claude-sonnet-4', 'session-2')

      expect(manager.getActiveCount('anthropic')).toBe(2)
      expect(manager.hasAvailableSlot('anthropic')).toBe(false)

      release1()
      release2()
      expect(manager.getActiveCount('anthropic')).toBe(0)
    })

    it('tryAcquire returns null when no slot available', async () => {
      const release1 = await manager.acquire('anthropic/claude-sonnet-4', 'session-1')
      const release2 = await manager.acquire('anthropic/claude-sonnet-4', 'session-2')

      const result = manager.tryAcquire('anthropic/claude-sonnet-4', 'session-3')
      expect(result).toBeNull()

      release1()
      release2()
    })

    it('tryAcquire returns releaser when slot available', () => {
      const release = manager.tryAcquire('anthropic/claude-sonnet-4', 'session-1')
      expect(release).toBeInstanceOf(Function)
      expect(manager.getActiveCount('anthropic')).toBe(1)
      release!()
      expect(manager.getActiveCount('anthropic')).toBe(0)
    })

    it('release function is idempotent', async () => {
      const release = await manager.acquire('anthropic/claude-sonnet-4', 'session-1')
      expect(manager.getActiveCount('anthropic')).toBe(1)

      release()
      expect(manager.getActiveCount('anthropic')).toBe(0)

      // Calling again should be safe
      release()
      expect(manager.getActiveCount('anthropic')).toBe(0)
    })
  })

  describe('queuing', () => {
    it('queues requests when at capacity', async () => {
      // Fill up anthropic slots (limit: 2)
      const release1 = await manager.acquire('anthropic/claude-sonnet-4', 'session-1')
      const release2 = await manager.acquire('anthropic/claude-sonnet-4', 'session-2')

      expect(manager.getQueuedCount('anthropic')).toBe(0)

      // Start a third request (will be queued)
      const promise = manager.acquire('anthropic/claude-sonnet-4', 'session-3')

      // Give it a tick to queue
      await new Promise(r => setTimeout(r, 10))
      expect(manager.getQueuedCount('anthropic')).toBe(1)

      // Release a slot - queued request should be processed
      release1()

      const release3 = await promise
      expect(manager.getActiveCount('anthropic')).toBe(2)
      expect(manager.getQueuedCount('anthropic')).toBe(0)

      release2()
      release3()
    })

    it('times out when queue takes too long', async () => {
      // Fill up slots
      const release1 = await manager.acquire('anthropic/claude-sonnet-4', 'session-1')
      const release2 = await manager.acquire('anthropic/claude-sonnet-4', 'session-2')

      // Try to acquire third (will timeout)
      const promise = manager.acquire('anthropic/claude-sonnet-4', 'session-3')

      await expect(promise).rejects.toThrow(/timeout/)

      release1()
      release2()
    })

    it('processes queue in FIFO order', async () => {
      // Fill up slots
      const release1 = await manager.acquire('anthropic/claude-sonnet-4', 'session-1')
      const release2 = await manager.acquire('anthropic/claude-sonnet-4', 'session-2')

      const order: string[] = []

      // Queue two requests
      const promise3 = manager.acquire('anthropic/claude-sonnet-4', 'session-3').then(r => {
        order.push('session-3')
        return r
      })
      const promise4 = manager.acquire('anthropic/claude-sonnet-4', 'session-4').then(r => {
        order.push('session-4')
        return r
      })

      // Release first slot
      release1()
      const release3 = await promise3

      // Release second slot
      release2()
      const release4 = await promise4

      expect(order).toEqual(['session-3', 'session-4'])

      release3()
      release4()
    })
  })

  describe('session release', () => {
    it('releases all slots for a session', async () => {
      const release1 = await manager.acquire('anthropic/claude-sonnet-4', 'session-1')
      const release2 = await manager.acquire('openai/gpt-4o', 'session-1')

      expect(manager.getActiveCount('anthropic')).toBe(1)
      expect(manager.getActiveCount('openai')).toBe(1)

      const released = manager.releaseBySession('session-1')

      expect(released).toBe(2)
      expect(manager.getActiveCount('anthropic')).toBe(0)
      expect(manager.getActiveCount('openai')).toBe(0)
    })

    it('only releases slots for the specified session', async () => {
      const release1 = await manager.acquire('anthropic/claude-sonnet-4', 'session-1')
      const release2 = await manager.acquire('anthropic/claude-sonnet-4', 'session-2')

      expect(manager.getActiveCount('anthropic')).toBe(2)

      manager.releaseBySession('session-1')

      expect(manager.getActiveCount('anthropic')).toBe(1)

      release2()
    })
  })

  describe('status', () => {
    it('returns status for all providers', async () => {
      const release1 = await manager.acquire('anthropic/claude-sonnet-4', 'session-1')
      const release2 = await manager.acquire('openai/gpt-4o', 'session-2')

      const status = manager.getStatus()

      const anthropicStatus = status.find(s => s.provider === 'anthropic')
      const openaiStatus = status.find(s => s.provider === 'openai')

      expect(anthropicStatus).toMatchObject({
        provider: 'anthropic',
        active: 1,
        queued: 0,
        limit: 2,
      })

      expect(openaiStatus).toMatchObject({
        provider: 'openai',
        active: 1,
        queued: 0,
        limit: 3,
      })

      release1()
      release2()
    })

    it('returns status for specific provider', async () => {
      const release = await manager.acquire('anthropic/claude-sonnet-4', 'session-1')

      const status = manager.getProviderStatus('anthropic')

      expect(status).toMatchObject({
        provider: 'anthropic',
        active: 1,
        queued: 0,
        limit: 2,
      })

      release()
    })

    it('returns active slots', async () => {
      const release1 = await manager.acquire('anthropic/claude-sonnet-4', 'session-1')
      const release2 = await manager.acquire('openai/gpt-4o', 'session-2')

      const slots = manager.getActiveSlots()

      expect(slots).toHaveLength(2)
      expect(slots.some(s => s.provider === 'anthropic' && s.sessionId === 'session-1')).toBe(true)
      expect(slots.some(s => s.provider === 'openai' && s.sessionId === 'session-2')).toBe(true)

      release1()
      release2()
    })
  })

  describe('clear', () => {
    it('clears all slots and rejects queued requests', async () => {
      const release1 = await manager.acquire('anthropic/claude-sonnet-4', 'session-1')
      const release2 = await manager.acquire('anthropic/claude-sonnet-4', 'session-2')

      // Queue a request
      const queuedPromise = manager.acquire('anthropic/claude-sonnet-4', 'session-3')
      await new Promise(r => setTimeout(r, 10))

      manager.clear()

      // Queued request should be rejected
      await expect(queuedPromise).rejects.toThrow(/cleared/)

      // Slots should be cleared
      expect(manager.getActiveCount('anthropic')).toBe(0)
    })
  })
})

describe('singleton functions', () => {
  beforeEach(() => {
    clearConcurrencyManager()
  })

  afterEach(() => {
    clearConcurrencyManager()
  })

  it('getConcurrencyManager returns singleton', () => {
    const manager1 = getConcurrencyManager()
    const manager2 = getConcurrencyManager()
    expect(manager1).toBe(manager2)
  })

  it('clearConcurrencyManager resets singleton', () => {
    const manager1 = getConcurrencyManager()
    clearConcurrencyManager()
    const manager2 = getConcurrencyManager()
    expect(manager1).not.toBe(manager2)
  })
})

describe('withConcurrencySlot', () => {
  let manager: ProviderConcurrencyManager

  beforeEach(() => {
    manager = new ProviderConcurrencyManager({
      limits: { anthropic: 2 },
      defaultLimit: 1,
      queueTimeout: 1000,
    })
  })

  afterEach(() => {
    manager.clear()
  })

  it('acquires and releases slot around function execution', async () => {
    expect(manager.getActiveCount('anthropic')).toBe(0)

    await withConcurrencySlot(
      'anthropic/claude-sonnet-4',
      'session-1',
      async () => {
        expect(manager.getActiveCount('anthropic')).toBe(1)
        return 'result'
      },
      manager
    )

    expect(manager.getActiveCount('anthropic')).toBe(0)
  })

  it('releases slot even on error', async () => {
    expect(manager.getActiveCount('anthropic')).toBe(0)

    await expect(
      withConcurrencySlot(
        'anthropic/claude-sonnet-4',
        'session-1',
        async () => {
          throw new Error('test error')
        },
        manager
      )
    ).rejects.toThrow('test error')

    expect(manager.getActiveCount('anthropic')).toBe(0)
  })
})

describe('describeConcurrencyStatus', () => {
  it('describes empty status', () => {
    const description = describeConcurrencyStatus([])
    expect(description).toBe('No active providers')
  })

  it('describes provider status with utilization', () => {
    const status = [
      { provider: 'anthropic', active: 2, queued: 1, limit: 4 },
      { provider: 'openai', active: 0, queued: 0, limit: 5 },
    ]

    const description = describeConcurrencyStatus(status)

    expect(description).toContain('anthropic: 2/4 (50% utilized, 1 queued)')
    expect(description).toContain('openai: 0/5 (0% utilized, 0 queued)')
  })
})
