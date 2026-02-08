/**
 * Compactor Tests
 *
 * Tests for compaction orchestration, strategy ordering, fallback behavior,
 * and factory functions.
 */

import { describe, expect, it, vi } from 'vitest'
import {
  Compactor,
  createAggressiveCompactor,
  createAutoCompactor,
  createCompactor,
} from './compactor.js'
import { ContextTracker } from './tracker.js'
import type { CompactionStrategy, Message } from './types.js'

// ============================================================================
// Helpers
// ============================================================================

function makeMessage(
  id: string,
  role: Message['role'],
  content: string,
  extra: Partial<Message> = {}
): Message {
  return {
    id,
    role,
    content,
    sessionId: 'test-session',
    createdAt: Date.now(),
    ...extra,
  }
}

/** Create a bunch of messages with substantial content */
function createConversation(count: number): Message[] {
  const messages: Message[] = [makeMessage('sys', 'system', 'You are a helpful assistant.')]
  for (let i = 0; i < count; i++) {
    const role = i % 2 === 0 ? 'user' : 'assistant'
    messages.push(
      makeMessage(
        `msg-${i}`,
        role as Message['role'],
        `This is message number ${i} with some filler content to increase token count.`
      )
    )
  }
  return messages
}

/** Create a mock strategy */
function mockStrategy(name: string, result: Message[] | null): CompactionStrategy {
  return {
    name,
    compact:
      result === null
        ? vi.fn().mockRejectedValue(new Error('strategy failed'))
        : vi.fn().mockResolvedValue(result),
  }
}

// ============================================================================
// Compactor Class
// ============================================================================

describe('Compactor', () => {
  // ==========================================================================
  // compact
  // ==========================================================================

  describe('compact', () => {
    it('returns original messages when under target', async () => {
      const tracker = new ContextTracker(1000000) // huge limit
      const compactor = new Compactor({ tracker })
      const messages = createConversation(4)

      // Track messages
      for (const msg of messages) {
        tracker.addMessage(msg.id, msg)
      }

      const result = await compactor.compact(messages)

      expect(result.strategyUsed).toBe('none')
      expect(result.tokensSaved).toBe(0)
      expect(result.messages).toBe(messages)
    })

    it('returns original when messages <= minMessages', async () => {
      const tracker = new ContextTracker(10) // tiny limit to force compaction need
      const compactor = new Compactor({ tracker })
      const messages = createConversation(2) // 3 total (sys + 2)

      for (const msg of messages) {
        tracker.addMessage(msg.id, msg)
      }

      const result = await compactor.compact(messages, { minMessages: 5 })

      expect(result.strategyUsed).toBe('none')
    })

    it('uses first successful strategy', async () => {
      const tracker = new ContextTracker(100) // tiny limit
      const messages = createConversation(20)

      for (const msg of messages) {
        tracker.addMessage(msg.id, msg)
      }

      const shortResult = [messages[0], messages[messages.length - 1]]
      const strategy1 = mockStrategy('first', shortResult)
      const strategy2 = mockStrategy('second', shortResult)

      const compactor = new Compactor({
        tracker,
        strategies: [strategy1, strategy2],
      })

      const result = await compactor.compact(messages)

      expect(result.strategyUsed).toBe('first')
      expect(strategy1.compact).toHaveBeenCalledOnce()
      expect(strategy2.compact).not.toHaveBeenCalled()
    })

    it('falls through to next strategy on failure', async () => {
      const tracker = new ContextTracker(100) // tiny limit
      const messages = createConversation(20)

      for (const msg of messages) {
        tracker.addMessage(msg.id, msg)
      }

      const shortResult = [messages[0], messages[messages.length - 1]]
      const failStrategy = mockStrategy('failing', null) // will throw
      const goodStrategy = mockStrategy('good', shortResult)

      const compactor = new Compactor({
        tracker,
        strategies: [failStrategy, goodStrategy],
      })

      const result = await compactor.compact(messages)

      expect(result.strategyUsed).toBe('good')
      expect(failStrategy.compact).toHaveBeenCalledOnce()
      expect(goodStrategy.compact).toHaveBeenCalledOnce()
    })

    it('uses fallback when all strategies fail', async () => {
      const tracker = new ContextTracker(100) // tiny limit
      const messages = createConversation(20)

      for (const msg of messages) {
        tracker.addMessage(msg.id, msg)
      }

      const failStrategy = mockStrategy('failing', null)

      const compactor = new Compactor({
        tracker,
        strategies: [failStrategy],
        fallbackMinMessages: 5,
      })

      const result = await compactor.compact(messages)

      expect(result.strategyUsed).toBe('fallback')
      // Should preserve system message + last 5 conversation messages
      expect(result.compactedCount).toBeLessThanOrEqual(6) // 1 system + 5
    })

    it('fallback preserves system message', async () => {
      const tracker = new ContextTracker(100)
      const messages = createConversation(20)

      for (const msg of messages) {
        tracker.addMessage(msg.id, msg)
      }

      const compactor = new Compactor({
        tracker,
        strategies: [mockStrategy('fail', null)],
        fallbackMinMessages: 3,
      })

      const result = await compactor.compact(messages)
      const systemMsgs = result.messages.filter((m) => m.role === 'system')
      expect(systemMsgs.length).toBe(1)
      expect(systemMsgs[0].content).toBe('You are a helpful assistant.')
    })

    it('reports tokens saved', async () => {
      const tracker = new ContextTracker(100)
      const messages = createConversation(20)

      for (const msg of messages) {
        tracker.addMessage(msg.id, msg)
      }

      const shortResult = [messages[0], messages[messages.length - 1]]
      const compactor = new Compactor({
        tracker,
        strategies: [mockStrategy('trim', shortResult)],
      })

      const result = await compactor.compact(messages)
      expect(result.tokensSaved).toBeGreaterThan(0)
      expect(result.originalCount).toBe(messages.length)
      expect(result.compactedCount).toBe(2)
    })

    it('skips strategy that returns empty array', async () => {
      const tracker = new ContextTracker(100)
      const messages = createConversation(20)

      for (const msg of messages) {
        tracker.addMessage(msg.id, msg)
      }

      const emptyStrategy = mockStrategy('empty', [])
      const shortResult = [messages[0], messages[messages.length - 1]]
      const goodStrategy = mockStrategy('good', shortResult)

      const compactor = new Compactor({
        tracker,
        strategies: [emptyStrategy, goodStrategy],
      })

      const result = await compactor.compact(messages)
      expect(result.strategyUsed).toBe('good')
    })
  })

  // ==========================================================================
  // needsCompaction
  // ==========================================================================

  describe('needsCompaction', () => {
    it('returns false when under target', () => {
      const tracker = new ContextTracker(100000)
      tracker.addMessage('msg', 'short')
      const compactor = new Compactor({ tracker })

      expect(compactor.needsCompaction()).toBe(false)
    })

    it('returns true when over target', () => {
      const tracker = new ContextTracker(10) // tiny limit
      tracker.addMessage('msg', 'This is a message with many tokens exceeding the tiny limit')
      const compactor = new Compactor({ tracker })

      expect(compactor.needsCompaction(1)).toBe(true)
    })
  })

  // ==========================================================================
  // getUsagePercent
  // ==========================================================================

  describe('getUsagePercent', () => {
    it('returns current usage percentage', () => {
      const tracker = new ContextTracker(100000)
      const compactor = new Compactor({ tracker })

      expect(compactor.getUsagePercent()).toBe(0)

      tracker.addMessage('msg', 'Hello')
      expect(compactor.getUsagePercent()).toBeGreaterThan(0)
    })
  })

  // ==========================================================================
  // Strategy Management
  // ==========================================================================

  describe('strategy management', () => {
    it('addStrategy appends to list', () => {
      const tracker = new ContextTracker(100000)
      const compactor = new Compactor({ tracker, strategies: [] })

      const strategy = mockStrategy('new-strategy', [])
      compactor.addStrategy(strategy)

      expect(compactor.getStrategyNames()).toContain('new-strategy')
    })

    it('insertStrategy adds at index', () => {
      const tracker = new ContextTracker(100000)
      const compactor = new Compactor({
        tracker,
        strategies: [mockStrategy('a', []), mockStrategy('c', [])],
      })

      compactor.insertStrategy(mockStrategy('b', []), 1)

      expect(compactor.getStrategyNames()).toEqual(['a', 'b', 'c'])
    })

    it('removeStrategy removes by name', () => {
      const tracker = new ContextTracker(100000)
      const compactor = new Compactor({
        tracker,
        strategies: [mockStrategy('keep', []), mockStrategy('remove', [])],
      })

      const removed = compactor.removeStrategy('remove')
      expect(removed).toBe(true)
      expect(compactor.getStrategyNames()).toEqual(['keep'])
    })

    it('removeStrategy returns false for non-existent', () => {
      const tracker = new ContextTracker(100000)
      const compactor = new Compactor({ tracker, strategies: [] })

      expect(compactor.removeStrategy('nonexistent')).toBe(false)
    })
  })
})

// ============================================================================
// Factory Functions
// ============================================================================

describe('createCompactor', () => {
  it('creates compactor with sliding window strategy', () => {
    const tracker = new ContextTracker(100000)
    const compactor = createCompactor(tracker)

    expect(compactor.getStrategyNames()).toContain('sliding-window')
  })

  it('accepts custom target percent', () => {
    const tracker = new ContextTracker(100000)
    const compactor = createCompactor(tracker, 30)

    // Verify it uses the custom target
    expect(compactor.needsCompaction(30)).toBe(false)
  })
})

describe('createAggressiveCompactor', () => {
  it('creates compactor with sliding window', () => {
    const tracker = new ContextTracker(100000)
    const compactor = createAggressiveCompactor(tracker)

    expect(compactor.getStrategyNames()).toContain('sliding-window')
  })
})

describe('createAutoCompactor', () => {
  it('returns function', () => {
    const tracker = new ContextTracker(100000)
    const compactor = createCompactor(tracker)
    const autoCompact = createAutoCompactor(compactor)

    expect(typeof autoCompact).toBe('function')
  })

  it('passes through when under threshold', async () => {
    const tracker = new ContextTracker(1000000)
    tracker.addMessage('msg', 'short')
    const compactor = createCompactor(tracker)
    const autoCompact = createAutoCompactor(compactor)

    const messages = createConversation(5)
    const result = await autoCompact(messages)

    expect(result).toBe(messages) // Same reference = no compaction
  })
})
