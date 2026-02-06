/**
 * Split Point Detection Tests
 */

import { describe, expect, it } from 'vitest'
import type { Message } from '../types.js'
import {
  DEFAULT_PRESERVE_FRACTION,
  findAllSplitPoints,
  findSafeSplitPoint,
  findSizeSplitPoint,
  getContentSizeUpTo,
  MIN_PRESERVE_MESSAGES,
} from './split-point.js'

// ============================================================================
// Helpers
// ============================================================================

function msg(role: 'user' | 'assistant' | 'system', content?: string): Message {
  return {
    id: `msg-${Math.random().toString(36).slice(2, 8)}`,
    sessionId: 'test',
    role,
    content: content ?? `${role} message`,
    createdAt: Date.now(),
  }
}

/** Create a typical conversation pattern: user → assistant → user → assistant ... */
function createConversation(turns: number): Message[] {
  const messages: Message[] = []
  for (let i = 0; i < turns; i++) {
    messages.push(msg('user', `User turn ${i}`))
    messages.push(msg('assistant', `Assistant turn ${i}`))
  }
  return messages
}

// ============================================================================
// Tests
// ============================================================================

describe('split-point', () => {
  describe('findSafeSplitPoint', () => {
    it('should return 0 for small conversations', () => {
      const messages = createConversation(2) // 4 messages
      expect(findSafeSplitPoint(messages)).toBe(0)
    })

    it('should find split point at user message boundary', () => {
      const messages = createConversation(10) // 20 messages
      const splitIdx = findSafeSplitPoint(messages, 0.3)

      // Should be at a user message boundary
      expect(messages[splitIdx]!.role).toBe('user')
    })

    it('should preserve approximately the right fraction', () => {
      const messages = createConversation(20) // 40 messages
      const splitIdx = findSafeSplitPoint(messages, 0.3)

      // 30% of 40 = 12, so split should be around index 28
      const preserved = messages.length - splitIdx
      expect(preserved).toBeGreaterThanOrEqual(10)
      expect(preserved).toBeLessThanOrEqual(16)
    })

    it('should handle all-user messages', () => {
      const messages = Array.from({ length: 20 }, (_, i) => msg('user', `Message ${i}`))

      const splitIdx = findSafeSplitPoint(messages, 0.3)
      expect(splitIdx).toBeGreaterThan(0)
      expect(messages[splitIdx]!.role).toBe('user')
    })

    it('should never split below MIN_PRESERVE_MESSAGES', () => {
      const messages = createConversation(3) // 6 messages
      const splitIdx = findSafeSplitPoint(messages, 0.1) // Try to keep only 10%

      const preserved = messages.length - splitIdx
      expect(preserved).toBeGreaterThanOrEqual(MIN_PRESERVE_MESSAGES)
    })

    it('should handle preserveFraction = 0.5', () => {
      const messages = createConversation(10) // 20 messages
      const splitIdx = findSafeSplitPoint(messages, 0.5)

      const preserved = messages.length - splitIdx
      // Should preserve roughly 50%
      expect(preserved).toBeGreaterThanOrEqual(8)
      expect(preserved).toBeLessThanOrEqual(12)
    })

    it('should handle preserveFraction = 1.0 (keep all)', () => {
      const messages = createConversation(10)
      const splitIdx = findSafeSplitPoint(messages, 1.0)

      // Target index is 0, should return 0
      expect(splitIdx).toBe(0)
    })

    it('should not split on assistant messages', () => {
      const messages = createConversation(10)
      const splitIdx = findSafeSplitPoint(messages, 0.3)

      if (splitIdx > 0) {
        expect(messages[splitIdx]!.role).toBe('user')
      }
    })

    it('should use default preserve fraction', () => {
      expect(DEFAULT_PRESERVE_FRACTION).toBe(0.3)
    })
  })

  describe('findAllSplitPoints', () => {
    it('should find all user message boundaries', () => {
      const messages = createConversation(10) // 20 messages
      const points = findAllSplitPoints(messages)

      // All split points should be at user messages
      for (const idx of points) {
        expect(messages[idx]!.role).toBe('user')
      }
    })

    it('should exclude last MIN_PRESERVE_MESSAGES', () => {
      const messages = createConversation(10)
      const points = findAllSplitPoints(messages)

      // No split point should be too close to the end
      for (const idx of points) {
        expect(messages.length - idx).toBeGreaterThanOrEqual(MIN_PRESERVE_MESSAGES)
      }
    })

    it('should return empty for tiny conversations', () => {
      const messages = createConversation(2)
      const points = findAllSplitPoints(messages)
      expect(points).toHaveLength(0)
    })
  })

  describe('getContentSizeUpTo', () => {
    it('should calculate cumulative content size', () => {
      const messages = [
        msg('user', 'Hello'), // 5 chars
        msg('assistant', 'Hi!'), // 3 chars
        msg('user', 'More text'), // 9 chars
      ]

      expect(getContentSizeUpTo(messages, 0)).toBe(0)
      expect(getContentSizeUpTo(messages, 1)).toBe(5)
      expect(getContentSizeUpTo(messages, 2)).toBe(8)
      expect(getContentSizeUpTo(messages, 3)).toBe(17)
    })

    it('should handle empty messages', () => {
      expect(getContentSizeUpTo([], 0)).toBe(0)
    })
  })

  describe('findSizeSplitPoint', () => {
    it('should split based on content size', () => {
      // Create messages with varying sizes
      const messages: Message[] = [
        msg('user', 'a'.repeat(1000)), // Large early message
        msg('assistant', 'b'.repeat(1000)),
        msg('user', 'c'.repeat(100)), // Small later
        msg('assistant', 'd'.repeat(100)),
        msg('user', 'e'.repeat(100)),
        msg('assistant', 'f'.repeat(100)),
        msg('user', 'g'.repeat(100)),
        msg('assistant', 'h'.repeat(100)),
      ]

      const splitIdx = findSizeSplitPoint(messages, 0.3)

      // Should split after the large messages (which take most of the size)
      expect(splitIdx).toBeGreaterThan(0)
      expect(splitIdx).toBeLessThan(messages.length)
    })

    it('should return 0 for small conversations', () => {
      const messages = createConversation(2)
      expect(findSizeSplitPoint(messages, 0.3)).toBe(0)
    })

    it('should snap to user message boundaries', () => {
      const messages = createConversation(10)
      const splitIdx = findSizeSplitPoint(messages, 0.3)

      if (splitIdx > 0 && splitIdx < messages.length) {
        expect(messages[splitIdx]!.role).toBe('user')
      }
    })
  })
})
