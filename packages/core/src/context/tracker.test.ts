/**
 * Context Tracker Tests
 *
 * Tests for token counting, message tracking, compaction thresholds,
 * and context window management.
 */

import { describe, expect, it } from 'vitest'
import type { ChatMessage } from '../types/llm.js'
import {
  ContextTracker,
  countMessagesTokens,
  countMessageTokens,
  countTokens,
  createContextTracker,
} from './tracker.js'

// ============================================================================
// Helpers
// ============================================================================

function makeMsg(role: ChatMessage['role'], content: string): ChatMessage {
  return { role, content }
}

// ============================================================================
// Token Counting Utilities
// ============================================================================

describe('countTokens', () => {
  it('returns 0 for empty string', () => {
    expect(countTokens('')).toBe(0)
  })

  it('returns positive count for non-empty string', () => {
    expect(countTokens('Hello, world!')).toBeGreaterThan(0)
  })

  it('longer strings produce more tokens', () => {
    const short = countTokens('hi')
    const long = countTokens(
      'This is a significantly longer piece of text for tokenization testing'
    )
    expect(long).toBeGreaterThan(short)
  })
})

describe('countMessageTokens', () => {
  it('includes structure overhead', () => {
    const contentOnly = countTokens('Hello')
    const withOverhead = countMessageTokens(makeMsg('user', 'Hello'))
    expect(withOverhead).toBeGreaterThan(contentOnly)
  })

  it('handles empty content', () => {
    const tokens = countMessageTokens(makeMsg('user', ''))
    // Should just be the overhead (4 tokens)
    expect(tokens).toBe(4)
  })
})

describe('countMessagesTokens', () => {
  it('includes array overhead', () => {
    const singleMsg = countMessageTokens(makeMsg('user', 'hi'))
    const arrayOf1 = countMessagesTokens([makeMsg('user', 'hi')])
    // Array overhead is 3 tokens
    expect(arrayOf1).toBe(singleMsg + 3)
  })

  it('sums multiple messages', () => {
    const messages = [makeMsg('user', 'Hello'), makeMsg('assistant', 'Hi there')]
    const total = countMessagesTokens(messages)
    expect(total).toBeGreaterThan(0)
  })
})

// ============================================================================
// ContextTracker
// ============================================================================

describe('ContextTracker', () => {
  // ==========================================================================
  // Constructor
  // ==========================================================================

  describe('constructor', () => {
    it('initializes with correct limit', () => {
      const tracker = new ContextTracker(200000)
      const stats = tracker.getStats()

      expect(stats.limit).toBe(200000)
      expect(stats.total).toBe(0)
      expect(stats.remaining).toBe(200000)
      expect(stats.percentUsed).toBe(0)
    })
  })

  // ==========================================================================
  // addMessage / removeMessage
  // ==========================================================================

  describe('addMessage', () => {
    it('tracks string content', () => {
      const tracker = new ContextTracker(100000)
      const tokens = tracker.addMessage('msg-1', 'Hello world')

      expect(tokens).toBeGreaterThan(0)
      expect(tracker.getStats().total).toBe(tokens)
      expect(tracker.getMessageTokens('msg-1')).toBe(tokens)
    })

    it('tracks ChatMessage content', () => {
      const tracker = new ContextTracker(100000)
      const tokens = tracker.addMessage('msg-1', makeMsg('user', 'Hello world'))

      expect(tokens).toBeGreaterThan(0)
      expect(tracker.getStats().total).toBe(tokens)
    })

    it('updates totals correctly', () => {
      const tracker = new ContextTracker(100000)
      tracker.addMessage('msg-1', 'Hello')
      tracker.addMessage('msg-2', 'World')

      const stats = tracker.getStats()
      expect(stats.messages.size).toBe(2)
      expect(stats.total).toBeGreaterThan(0)
      expect(stats.remaining).toBeLessThan(100000)
      expect(stats.percentUsed).toBeGreaterThan(0)
    })

    it('replaces existing message when same ID used', () => {
      const tracker = new ContextTracker(100000)
      tracker.addMessage('msg-1', 'short')
      const initialTotal = tracker.getStats().total

      tracker.addMessage('msg-1', 'a much longer replacement string')
      expect(tracker.getStats().total).toBeGreaterThan(initialTotal)
      expect(tracker.getStats().messages.size).toBe(1)
    })
  })

  describe('addMessages', () => {
    it('adds multiple messages at once', () => {
      const tracker = new ContextTracker(100000)
      const total = tracker.addMessages([
        ['msg-1', 'Hello'],
        ['msg-2', 'World'],
      ])

      expect(total).toBeGreaterThan(0)
      expect(tracker.getStats().messages.size).toBe(2)
    })
  })

  describe('removeMessage', () => {
    it('removes tracked message', () => {
      const tracker = new ContextTracker(100000)
      tracker.addMessage('msg-1', 'Hello')
      tracker.removeMessage('msg-1')

      expect(tracker.getStats().total).toBe(0)
      expect(tracker.getStats().messages.size).toBe(0)
    })

    it('removes non-existent message without error', () => {
      const tracker = new ContextTracker(100000)
      tracker.removeMessage('nonexistent')
      expect(tracker.getStats().total).toBe(0)
    })
  })

  describe('removeMessages', () => {
    it('removes multiple messages', () => {
      const tracker = new ContextTracker(100000)
      tracker.addMessage('msg-1', 'Hello')
      tracker.addMessage('msg-2', 'World')
      tracker.addMessage('msg-3', 'Foo')

      tracker.removeMessages(['msg-1', 'msg-2'])
      expect(tracker.getStats().messages.size).toBe(1)
    })
  })

  // ==========================================================================
  // updateMessage
  // ==========================================================================

  describe('updateMessage', () => {
    it('updates existing message', () => {
      const tracker = new ContextTracker(100000)
      tracker.addMessage('msg-1', 'short')

      const newTokens = tracker.updateMessage('msg-1', 'much longer replacement text here')
      expect(newTokens).toBeDefined()
      expect(newTokens!).toBeGreaterThan(0)
    })

    it('returns undefined for non-existent message', () => {
      const tracker = new ContextTracker(100000)
      const result = tracker.updateMessage('nonexistent', 'content')
      expect(result).toBeUndefined()
    })
  })

  // ==========================================================================
  // clear
  // ==========================================================================

  describe('clear', () => {
    it('removes all tracked messages', () => {
      const tracker = new ContextTracker(100000)
      tracker.addMessage('msg-1', 'Hello')
      tracker.addMessage('msg-2', 'World')

      tracker.clear()

      const stats = tracker.getStats()
      expect(stats.total).toBe(0)
      expect(stats.messages.size).toBe(0)
      expect(stats.remaining).toBe(100000)
      expect(stats.percentUsed).toBe(0)
    })
  })

  // ==========================================================================
  // getStats
  // ==========================================================================

  describe('getStats', () => {
    it('returns a copy (not a reference)', () => {
      const tracker = new ContextTracker(100000)
      tracker.addMessage('msg-1', 'Hello')

      const stats1 = tracker.getStats()
      const stats2 = tracker.getStats()
      expect(stats1).not.toBe(stats2)
      expect(stats1.messages).not.toBe(stats2.messages)
    })

    it('calculates percentUsed correctly', () => {
      const tracker = new ContextTracker(100)
      // Force add a message by checking what tokens we get
      tracker.addMessage('msg-1', 'Hello world this is a test message')
      const stats = tracker.getStats()

      const expectedPercent = (stats.total / 100) * 100
      expect(stats.percentUsed).toBeCloseTo(expectedPercent, 5)
    })
  })

  // ==========================================================================
  // shouldCompact
  // ==========================================================================

  describe('shouldCompact', () => {
    it('returns false when under threshold', () => {
      const tracker = new ContextTracker(100000)
      tracker.addMessage('msg-1', 'short')
      expect(tracker.shouldCompact()).toBe(false)
    })

    it('accepts custom threshold', () => {
      const tracker = new ContextTracker(10)
      // Adding even a short message to a tiny limit will push past threshold
      tracker.addMessage(
        'msg-1',
        'This is a test message with multiple tokens for threshold checking'
      )
      expect(tracker.shouldCompact(1)).toBe(true) // 1% threshold on 10 token limit
    })
  })

  // ==========================================================================
  // wouldFit
  // ==========================================================================

  describe('wouldFit', () => {
    it('returns true when plenty of room', () => {
      const tracker = new ContextTracker(100000)
      expect(tracker.wouldFit('Hello')).toBe(true)
    })

    it('considers safety buffer', () => {
      const tracker = new ContextTracker(100000)
      // With default 1000 token buffer, small content should still fit
      expect(tracker.wouldFit('Hello', 1000)).toBe(true)
    })
  })

  // ==========================================================================
  // getAvailable
  // ==========================================================================

  describe('getAvailable', () => {
    it('returns remaining minus buffer', () => {
      const tracker = new ContextTracker(100000)
      expect(tracker.getAvailable(1000)).toBe(99000)
    })

    it('never returns negative', () => {
      const tracker = new ContextTracker(100)
      expect(tracker.getAvailable(200)).toBe(0)
    })
  })

  // ==========================================================================
  // setLimit
  // ==========================================================================

  describe('setLimit', () => {
    it('updates limit and recalculates', () => {
      const tracker = new ContextTracker(100000)
      tracker.addMessage('msg-1', 'Hello')

      tracker.setLimit(50000)

      const stats = tracker.getStats()
      expect(stats.limit).toBe(50000)
      expect(stats.remaining).toBe(50000 - stats.total)
    })
  })

  // ==========================================================================
  // Factory
  // ==========================================================================

  describe('createContextTracker', () => {
    it('creates tracker with specified limit', () => {
      const tracker = createContextTracker(128000)
      expect(tracker.getStats().limit).toBe(128000)
    })
  })
})
