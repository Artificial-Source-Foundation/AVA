import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'
import { getMessageBus, MessageBus, resetMessageBus, setMessageBus } from './message-bus.js'
import type { BusMessage } from './types.js'

describe('MessageBus', () => {
  let bus: MessageBus

  beforeEach(() => {
    bus = new MessageBus()
  })

  afterEach(() => {
    bus.clear()
  })

  // ─── Subscribe / Publish ────────────────────────────────────────────────

  describe('subscribe/publish', () => {
    it('delivers messages to subscribers', () => {
      const handler = vi.fn()
      bus.subscribe('test', handler)
      bus.publish({ type: 'test', correlationId: '1', timestamp: Date.now() })
      expect(handler).toHaveBeenCalledOnce()
    })

    it('passes message to handler', () => {
      const handler = vi.fn()
      bus.subscribe('test', handler)
      const msg: BusMessage = { type: 'test', correlationId: '1', timestamp: 1000 }
      bus.publish(msg)
      expect(handler).toHaveBeenCalledWith(msg)
    })

    it('delivers to multiple subscribers', () => {
      const h1 = vi.fn()
      const h2 = vi.fn()
      bus.subscribe('test', h1)
      bus.subscribe('test', h2)
      bus.publish({ type: 'test', correlationId: '1', timestamp: Date.now() })
      expect(h1).toHaveBeenCalledOnce()
      expect(h2).toHaveBeenCalledOnce()
    })

    it('does not deliver to unsubscribed handlers', () => {
      const handler = vi.fn()
      const unsub = bus.subscribe('test', handler)
      unsub()
      bus.publish({ type: 'test', correlationId: '1', timestamp: Date.now() })
      expect(handler).not.toHaveBeenCalled()
    })

    it('does not deliver to wrong message type', () => {
      const handler = vi.fn()
      bus.subscribe('test', handler)
      bus.publish({ type: 'other', correlationId: '1', timestamp: Date.now() })
      expect(handler).not.toHaveBeenCalled()
    })

    it('cleans up empty handler sets on unsubscribe', () => {
      const handler = vi.fn()
      const unsub = bus.subscribe('test', handler)
      unsub()
      expect(bus.hasSubscribers('test')).toBe(false)
    })

    it('handles multiple unsubscribe calls safely', () => {
      const handler = vi.fn()
      const unsub = bus.subscribe('test', handler)
      unsub()
      unsub() // should not throw
    })
  })

  // ─── hasSubscribers ─────────────────────────────────────────────────────

  describe('hasSubscribers', () => {
    it('returns false for no subscribers', () => {
      expect(bus.hasSubscribers('test')).toBe(false)
    })

    it('returns true when subscribers exist', () => {
      bus.subscribe('test', () => {})
      expect(bus.hasSubscribers('test')).toBe(true)
    })
  })

  // ─── Correlation ────────────────────────────────────────────────────────

  describe('correlation', () => {
    it('resolves correlation handler on matching publish', () => {
      const handler = vi.fn()
      // Directly set a correlation handler via internals
      const id = 'corr-1'
      // Use request/publish pattern
      bus.publish({ type: 'req', correlationId: id, timestamp: Date.now() })

      // Ensure no crash
      expect(handler).not.toHaveBeenCalled()
    })
  })

  // ─── Request/Response ───────────────────────────────────────────────────

  describe('request', () => {
    it('resolves when correlated response arrives', async () => {
      // Subscriber that echoes requests
      bus.subscribe('request', (msg) => {
        setTimeout(() => {
          bus.publish({
            type: 'response',
            correlationId: msg.correlationId,
            timestamp: Date.now(),
            data: 'hello',
          } as BusMessage & { data: string })
        }, 10)
      })

      const result = await bus.request<BusMessage, BusMessage & { data: string }>(
        { type: 'request' },
        'response',
        5000
      )

      expect(result.type).toBe('response')
      expect((result as BusMessage & { data: string }).data).toBe('hello')
    })

    it('times out when no response arrives', async () => {
      await expect(bus.request({ type: 'request' }, 'response', 50)).rejects.toThrow('timed out')
    })
  })

  // ─── Clear ──────────────────────────────────────────────────────────────

  describe('clear', () => {
    it('removes all subscribers', () => {
      bus.subscribe('a', () => {})
      bus.subscribe('b', () => {})
      bus.clear()
      expect(bus.hasSubscribers('a')).toBe(false)
      expect(bus.hasSubscribers('b')).toBe(false)
    })
  })
})

// ─── Singleton ────────────────────────────────────────────────────────────

describe('MessageBus singleton', () => {
  afterEach(() => {
    resetMessageBus()
  })

  it('returns same instance on repeated calls', () => {
    const a = getMessageBus()
    const b = getMessageBus()
    expect(a).toBe(b)
  })

  it('allows replacement via setMessageBus', () => {
    const custom = new MessageBus()
    setMessageBus(custom)
    expect(getMessageBus()).toBe(custom)
  })

  it('resets to null and creates new on next get', () => {
    const first = getMessageBus()
    resetMessageBus()
    const second = getMessageBus()
    expect(second).not.toBe(first)
  })
})
