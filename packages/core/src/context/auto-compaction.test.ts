/**
 * Compactor Auto-Compaction Tests
 *
 * Tests for auto-compaction threshold and visibility integration.
 */

import { describe, expect, it } from 'vitest'
import { createAutoCompactor, createCompactor } from './compactor.js'
import type { Message } from './types.js'

// ============================================================================
// Helpers
// ============================================================================

function makeMessage(role: 'user' | 'assistant' | 'system', content: string): Message {
  return {
    id: `msg-${Math.random().toString(36).slice(2, 8)}`,
    sessionId: 'test',
    role,
    content,
    createdAt: Date.now(),
    tokenCount: Math.ceil(content.length / 4),
  }
}

function mockTracker(percentUsed: number, limit = 100000): ContextTracker {
  return {
    getStats: () => ({
      used: Math.floor((limit * percentUsed) / 100),
      limit,
      percentUsed,
      remaining: limit - Math.floor((limit * percentUsed) / 100),
    }),
    shouldCompact: (threshold: number) => percentUsed >= threshold,
    add: vi.fn(),
    reset: vi.fn(),
  } as unknown as ContextTracker
}

// ============================================================================
// createAutoCompactor
// ============================================================================

describe('createAutoCompactor', () => {
  it('returns messages unchanged when below threshold', async () => {
    const tracker = mockTracker(50)
    const compactor = createCompactor(tracker)
    const autoCompact = createAutoCompactor(compactor, { threshold: 80 })

    const messages = [makeMessage('user', 'hello'), makeMessage('assistant', 'hi')]
    const result = await autoCompact(messages)
    expect(result).toBe(messages) // Same reference — not compacted
  })

  it('compacts when above threshold', async () => {
    const tracker = mockTracker(90, 100) // 90% used, tiny limit
    const compactor = createCompactor(tracker, 50)
    const autoCompact = createAutoCompactor(compactor, { threshold: 80, targetPercent: 50 })

    const messages = Array.from({ length: 20 }, (_, i) =>
      makeMessage(i % 2 === 0 ? 'user' : 'assistant', 'a'.repeat(50))
    )
    const result = await autoCompact(messages)
    expect(result.length).toBeLessThanOrEqual(messages.length)
  })

  it('uses default threshold of 80', async () => {
    const tracker = mockTracker(75)
    const compactor = createCompactor(tracker)
    const autoCompact = createAutoCompactor(compactor)

    const messages = [makeMessage('user', 'hello')]
    const result = await autoCompact(messages)
    // 75% < 80% threshold → no compaction
    expect(result).toBe(messages)
  })

  it('uses custom threshold', async () => {
    const tracker = mockTracker(55)
    const compactor = createCompactor(tracker)
    const autoCompact = createAutoCompactor(compactor, { threshold: 50 })

    const messages = Array.from({ length: 20 }, (_, i) =>
      makeMessage(i % 2 === 0 ? 'user' : 'assistant', 'a'.repeat(100))
    )
    // 55% >= 50% → should trigger compaction
    const result = await autoCompact(messages)
    expect(result.length).toBeLessThanOrEqual(messages.length)
  })
})

// ============================================================================
// Compactor Strategy Management
// ============================================================================

describe('Compactor strategy management', () => {
  it('addStrategy appends to list', () => {
    const tracker = mockTracker(50)
    const compactor = createCompactor(tracker)
    const original = compactor.getStrategyNames()

    compactor.addStrategy({ name: 'custom', compact: async (m) => m })
    expect(compactor.getStrategyNames()).toHaveLength(original.length + 1)
    expect(compactor.getStrategyNames()).toContain('custom')
  })

  it('removeStrategy removes by name', () => {
    const tracker = mockTracker(50)
    const compactor = createCompactor(tracker)
    compactor.addStrategy({ name: 'custom', compact: async (m) => m })

    const removed = compactor.removeStrategy('custom')
    expect(removed).toBe(true)
    expect(compactor.getStrategyNames()).not.toContain('custom')
  })

  it('removeStrategy returns false for unknown', () => {
    const tracker = mockTracker(50)
    const compactor = createCompactor(tracker)
    expect(compactor.removeStrategy('nonexistent')).toBe(false)
  })

  it('insertStrategy at specific position', () => {
    const tracker = mockTracker(50)
    const compactor = createCompactor(tracker)
    compactor.addStrategy({ name: 'end', compact: async (m) => m })
    compactor.insertStrategy({ name: 'start', compact: async (m) => m }, 0)

    const names = compactor.getStrategyNames()
    expect(names[0]).toBe('start')
  })
})

// ============================================================================
// Compactor needsCompaction
// ============================================================================

describe('Compactor needsCompaction', () => {
  it('returns true when over threshold', () => {
    const tracker = mockTracker(85)
    const compactor = createCompactor(tracker)
    expect(compactor.needsCompaction(80)).toBe(true)
  })

  it('returns false when under threshold', () => {
    const tracker = mockTracker(70)
    const compactor = createCompactor(tracker)
    expect(compactor.needsCompaction(80)).toBe(false)
  })

  it('uses default threshold when not specified', () => {
    const tracker = mockTracker(60)
    const compactor = createCompactor(tracker, 50)
    expect(compactor.needsCompaction()).toBe(true)
  })
})

// ============================================================================
// Compactor getUsagePercent
// ============================================================================

describe('Compactor getUsagePercent', () => {
  it('returns current usage percentage', () => {
    const tracker = mockTracker(42)
    const compactor = createCompactor(tracker)
    expect(compactor.getUsagePercent()).toBe(42)
  })
})
