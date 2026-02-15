/**
 * Visibility-Aware Compaction Strategy Tests
 */

import { describe, expect, it } from 'vitest'
import type { Message, MessageVisibility } from '../types.js'
import {
  createVisibilityCompaction,
  filterAgentVisible,
  filterUserVisible,
  isAgentVisible,
  isUserVisible,
  tagVisibility,
  visibilityCompaction,
} from './visibility.js'

// ============================================================================
// Helpers
// ============================================================================

function makeMessage(
  role: 'user' | 'assistant' | 'system',
  content: string,
  visibility?: MessageVisibility
): Message {
  return {
    id: `msg-${Math.random().toString(36).slice(2, 8)}`,
    sessionId: 'test',
    role,
    content,
    createdAt: Date.now(),
    visibility,
  }
}

// ============================================================================
// Visibility Helpers
// ============================================================================

describe('isUserVisible', () => {
  it('returns true for "all" visibility', () => {
    expect(isUserVisible(makeMessage('user', 'hi', 'all'))).toBe(true)
  })

  it('returns true for "user_visible"', () => {
    expect(isUserVisible(makeMessage('user', 'hi', 'user_visible'))).toBe(true)
  })

  it('returns false for "agent_visible"', () => {
    expect(isUserVisible(makeMessage('user', 'hi', 'agent_visible'))).toBe(false)
  })

  it('defaults to true when undefined', () => {
    expect(isUserVisible(makeMessage('user', 'hi'))).toBe(true)
  })
})

describe('isAgentVisible', () => {
  it('returns true for "all" visibility', () => {
    expect(isAgentVisible(makeMessage('user', 'hi', 'all'))).toBe(true)
  })

  it('returns true for "agent_visible"', () => {
    expect(isAgentVisible(makeMessage('user', 'hi', 'agent_visible'))).toBe(true)
  })

  it('returns false for "user_visible"', () => {
    expect(isAgentVisible(makeMessage('user', 'hi', 'user_visible'))).toBe(false)
  })

  it('defaults to true when undefined', () => {
    expect(isAgentVisible(makeMessage('user', 'hi'))).toBe(true)
  })
})

describe('filterUserVisible', () => {
  it('filters out agent_visible messages', () => {
    const messages = [
      makeMessage('user', 'visible', 'all'),
      makeMessage('assistant', 'hidden', 'agent_visible'),
      makeMessage('user', 'also visible'),
    ]
    const filtered = filterUserVisible(messages)
    expect(filtered).toHaveLength(2)
  })

  it('keeps user_visible messages', () => {
    const messages = [makeMessage('user', 'ui only', 'user_visible')]
    expect(filterUserVisible(messages)).toHaveLength(1)
  })
})

describe('filterAgentVisible', () => {
  it('filters out user_visible messages', () => {
    const messages = [
      makeMessage('user', 'sent to LLM', 'all'),
      makeMessage('assistant', 'ui only', 'user_visible'),
      makeMessage('user', 'also sent', 'agent_visible'),
    ]
    const filtered = filterAgentVisible(messages)
    expect(filtered).toHaveLength(2)
  })
})

describe('tagVisibility', () => {
  it('creates new message with visibility set', () => {
    const msg = makeMessage('user', 'hello')
    const tagged = tagVisibility(msg, 'agent_visible')
    expect(tagged.visibility).toBe('agent_visible')
    expect(tagged.content).toBe('hello')
  })

  it('does not mutate original', () => {
    const msg = makeMessage('user', 'hello')
    tagVisibility(msg, 'agent_visible')
    expect(msg.visibility).toBeUndefined()
  })
})

// ============================================================================
// Strategy
// ============================================================================

describe('createVisibilityCompaction', () => {
  it('returns empty for empty messages', async () => {
    const strategy = createVisibilityCompaction()
    const result = await strategy.compact([], 1000)
    expect(result).toHaveLength(0)
  })

  it('preserves all when under preserveRecent', async () => {
    const messages = [makeMessage('user', 'hello'), makeMessage('assistant', 'hi')]
    const strategy = createVisibilityCompaction({ preserveRecent: 6 })
    const result = await strategy.compact(messages, 1000)
    expect(result).toHaveLength(2)
    expect(result.every((m) => m.visibility === undefined)).toBe(true)
  })

  it('tags older messages as agent_visible', async () => {
    const messages = Array.from({ length: 10 }, (_, i) =>
      makeMessage(i % 2 === 0 ? 'user' : 'assistant', `msg-${i}`)
    )
    const strategy = createVisibilityCompaction({ preserveRecent: 4 })
    const result = await strategy.compact(messages, 1000)
    expect(result).toHaveLength(10)

    // First 6 conversation messages should be agent_visible
    const conversation = result.filter((m) => m.role !== 'system')
    const agentOnly = conversation.filter((m) => m.visibility === 'agent_visible')
    expect(agentOnly).toHaveLength(6)

    // Last 4 should have no visibility tag (fully visible)
    const recent = conversation.slice(-4)
    expect(recent.every((m) => m.visibility === undefined)).toBe(true)
  })

  it('preserves system messages', async () => {
    const messages = [
      makeMessage('system', 'You are helpful'),
      ...Array.from({ length: 8 }, (_, i) =>
        makeMessage(i % 2 === 0 ? 'user' : 'assistant', `msg-${i}`)
      ),
    ]
    const strategy = createVisibilityCompaction({ preserveRecent: 2 })
    const result = await strategy.compact(messages, 1000)
    expect(result[0].role).toBe('system')
    expect(result[0].visibility).toBeUndefined()
  })

  it('has name property', () => {
    expect(visibilityCompaction.name).toBe('visibility')
  })
})
