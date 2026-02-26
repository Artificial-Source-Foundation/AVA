/**
 * Context compaction strategies.
 */

import type { ChatMessage } from '@ava/core-v2/llm'
import { describe, expect, it } from 'vitest'
import { ALL_STRATEGIES, summarizeStrategy, truncateStrategy } from './strategies.js'

// ─── Helpers ────────────────────────────────────────────────────────────────

function msg(role: ChatMessage['role'], content: string): ChatMessage {
  return { role, content }
}

// ─── truncateStrategy ───────────────────────────────────────────────────────

describe('truncateStrategy', () => {
  it('has correct name', () => {
    expect(truncateStrategy.name).toBe('truncate')
  })

  it('keeps system message', () => {
    const messages = [msg('system', 'You are AVA.'), msg('user', 'Hello'), msg('assistant', 'Hi!')]
    // Large token limit so everything fits
    const result = truncateStrategy.compact(messages, 10000)
    expect(result[0]).toEqual(msg('system', 'You are AVA.'))
  })

  it('keeps newest messages and drops oldest when over limit', () => {
    const messages = [
      msg('user', 'A'.repeat(40)),
      msg('assistant', 'B'.repeat(40)),
      msg('user', 'C'.repeat(40)),
      msg('assistant', 'D'.repeat(40)),
    ]
    // Allow only ~80 chars = 20 tokens, so only 2 newest messages should fit
    const result = truncateStrategy.compact(messages, 20)
    expect(result.length).toBeLessThan(messages.length)
    // Most recent messages should be kept
    const contents = result.map((m) => m.content[0])
    expect(contents[contents.length - 1]).toBe('D')
  })

  it('respects token limit', () => {
    const messages = [
      msg('system', 'S'),
      msg('user', 'A'.repeat(100)),
      msg('assistant', 'B'.repeat(100)),
      msg('user', 'C'.repeat(100)),
      msg('assistant', 'D'.repeat(100)),
    ]
    // Only allow ~50 chars (12 tokens * 4 chars)
    const result = truncateStrategy.compact(messages, 12)
    // Should have system + at most 1 message
    expect(result.length).toBeLessThanOrEqual(3)
    expect(result[0].role).toBe('system')
  })

  it('handles empty messages', () => {
    const result = truncateStrategy.compact([], 1000)
    expect(result).toEqual([])
  })

  it('handles single message', () => {
    const messages = [msg('user', 'Hello')]
    const result = truncateStrategy.compact(messages, 1000)
    expect(result).toEqual([msg('user', 'Hello')])
  })

  it('handles messages without system message', () => {
    const messages = [msg('user', 'Hello'), msg('assistant', 'Hi')]
    const result = truncateStrategy.compact(messages, 10000)
    expect(result).toHaveLength(2)
  })

  it('drops oldest non-system messages when over limit', () => {
    const messages = [
      msg('system', 'sys'),
      msg('user', 'first'), // 5 chars
      msg('assistant', 'reply1'), // 6 chars
      msg('user', 'second'), // 6 chars
      msg('assistant', 'reply2'), // 6 chars
    ]
    // Allow system (3) + one message (~5) = 8 chars = 2 tokens
    const result = truncateStrategy.compact(messages, 3)
    expect(result[0].role).toBe('system')
    // Most recent messages should be kept
    expect(result.length).toBeLessThan(messages.length)
  })
})

// ─── summarizeStrategy ──────────────────────────────────────────────────────

describe('summarizeStrategy', () => {
  it('has correct name', () => {
    expect(summarizeStrategy.name).toBe('summarize')
  })

  it('returns all messages when no old messages to summarize', () => {
    const messages = [msg('user', 'Hello'), msg('assistant', 'Hi')]
    // With 2 messages, keepCount = max(4, floor(2/3)) = 4, so all are "recent"
    const result = summarizeStrategy.compact(messages, 10000)
    expect(result).toEqual(messages)
  })

  it('summarizes old messages and keeps recent', () => {
    const messages: ChatMessage[] = []
    // Create enough messages so that some become "old"
    for (let i = 0; i < 15; i++) {
      messages.push(msg(i % 2 === 0 ? 'user' : 'assistant', `Message ${i}`))
    }
    // keepCount = max(4, floor(15/3)) = 5
    const result = summarizeStrategy.compact(messages, 10000)
    // First message should be a summary
    expect(result[0].role).toBe('system')
    expect(result[0].content).toContain('Context compacted')
    expect(result[0].content).toContain('earlier messages summarized')
    // Should have summary + recent messages
    expect(result.length).toBeLessThan(messages.length)
  })

  it('includes count of summarized messages', () => {
    const messages: ChatMessage[] = []
    for (let i = 0; i < 12; i++) {
      messages.push(msg(i % 2 === 0 ? 'user' : 'assistant', `Msg ${i}`))
    }
    // keepCount = max(4, floor(12/3)) = 4
    // old = 12 - 4 = 8 messages
    const result = summarizeStrategy.compact(messages, 10000)
    expect(result[0].content).toContain('8 earlier messages')
  })

  it('falls back to truncation when still too long', () => {
    const messages: ChatMessage[] = []
    for (let i = 0; i < 15; i++) {
      messages.push(msg(i % 2 === 0 ? 'user' : 'assistant', 'A'.repeat(50)))
    }
    // Very small token limit
    const result = summarizeStrategy.compact(messages, 5)
    // Should have been truncated further
    expect(result.length).toBeLessThan(6)
  })

  it('counts assistant responses in summary', () => {
    const messages: ChatMessage[] = [
      msg('user', 'Q1'),
      msg('assistant', 'A1'),
      msg('user', 'Q2'),
      msg('assistant', 'A2'),
      msg('user', 'Q3'),
      msg('assistant', 'A3'),
      msg('user', 'Q4'),
      msg('assistant', 'A4'),
      msg('user', 'Q5'),
      msg('assistant', 'A5'),
      msg('user', 'Q6'),
      msg('assistant', 'A6'),
    ]
    // keepCount = max(4, floor(12/3)) = 4
    // old = 12 - 4 = 8 messages, 4 of which are assistant
    const result = summarizeStrategy.compact(messages, 10000)
    expect(result[0].content).toContain('4 assistant responses')
  })
})

// ─── ALL_STRATEGIES ─────────────────────────────────────────────────────────

describe('ALL_STRATEGIES', () => {
  it('contains both strategies', () => {
    expect(ALL_STRATEGIES).toHaveLength(2)
    expect(ALL_STRATEGIES).toContain(truncateStrategy)
    expect(ALL_STRATEGIES).toContain(summarizeStrategy)
  })
})
