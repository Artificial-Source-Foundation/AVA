/**
 * Context Compaction Strategies Tests
 */

import { describe, expect, it } from 'vitest'
import type { Message } from '../types.js'
import { buildSummaryTree, selectLevel } from './hierarchical.js'
import { createSlidingWindow, slidingWindow } from './sliding-window.js'
import { createSummarize, extractSummary, getSummarizationPrompt } from './summarize.js'

// ============================================================================
// Helpers
// ============================================================================

function makeMessage(role: 'user' | 'assistant' | 'system', content: string, id?: string): Message {
  return {
    id: id ?? `msg-${Math.random().toString(36).slice(2, 8)}`,
    sessionId: 'test-session',
    role,
    content,
    createdAt: Date.now(),
    tokenCount: Math.ceil(content.length / 4),
  }
}

// ============================================================================
// Sliding Window
// ============================================================================

describe('slidingWindow', () => {
  it('returns empty for empty messages', async () => {
    const result = await slidingWindow.compact([], 1000)
    expect(result).toHaveLength(0)
  })

  it('keeps all messages when under budget', async () => {
    const messages = [makeMessage('user', 'hello'), makeMessage('assistant', 'hi')]
    const result = await slidingWindow.compact(messages, 100000)
    expect(result).toHaveLength(2)
  })

  it('preserves system messages', async () => {
    const messages = [
      makeMessage('system', 'You are helpful'),
      makeMessage('user', 'hello'),
      makeMessage('assistant', 'hi'),
    ]
    const result = await slidingWindow.compact(messages, 100000)
    expect(result[0].role).toBe('system')
  })

  it('drops oldest messages when over budget', async () => {
    const messages = [
      makeMessage('user', 'a'.repeat(100)),
      makeMessage('assistant', 'b'.repeat(100)),
      makeMessage('user', 'recent message'),
    ]
    // Very tight budget — only room for last message or two
    const result = await slidingWindow.compact(messages, 10)
    expect(result.length).toBeLessThan(messages.length)
  })

  it('returns only system messages when budget is very low', async () => {
    const sys = makeMessage('system', 'x'.repeat(1000))
    const messages = [sys, makeMessage('user', 'hello')]
    const result = await slidingWindow.compact(messages, 5) // tiny budget
    expect(result).toHaveLength(1)
    expect(result[0].role).toBe('system')
  })

  it('has name property', () => {
    expect(slidingWindow.name).toBe('sliding-window')
  })
})

describe('createSlidingWindow', () => {
  it('creates strategy with custom options', () => {
    const strategy = createSlidingWindow({ minMessages: 4 })
    expect(strategy.name).toBe('sliding-window')
  })

  it('ensures valid turns when enabled', async () => {
    const messages = [
      makeMessage('assistant', 'I responded first'),
      makeMessage('user', 'then I asked'),
      makeMessage('assistant', 'then I replied'),
    ]
    const strategy = createSlidingWindow({ ensureValidTurns: true })
    const result = await strategy.compact(messages, 100000)
    // First non-system message should be user
    const nonSystem = result.filter((m) => m.role !== 'system')
    if (nonSystem.length > 0) {
      expect(nonSystem[0].role).toBe('user')
    }
  })

  it('keeps minimum messages even when over budget', async () => {
    const messages = [
      makeMessage('user', 'a'.repeat(1000)),
      makeMessage('assistant', 'b'.repeat(1000)),
    ]
    const strategy = createSlidingWindow({ minMessages: 2 })
    const result = await strategy.compact(messages, 10)
    // Should keep at least 2 messages (minMessages)
    expect(result.length).toBeGreaterThanOrEqual(2)
  })

  it('returns empty for empty messages', async () => {
    const strategy = createSlidingWindow()
    const result = await strategy.compact([], 1000)
    expect(result).toHaveLength(0)
  })
})

// ============================================================================
// Hierarchical
// ============================================================================

describe('buildSummaryTree', () => {
  const mockSummarize = async (messages: Message[]) => `Summary of ${messages.length} messages`

  it('builds tree from messages', async () => {
    const messages = Array.from({ length: 8 }, (_, i) =>
      makeMessage(i % 2 === 0 ? 'user' : 'assistant', `Message ${i}`)
    )
    const tree = await buildSummaryTree(messages, {
      messagesPerLeaf: 2,
      maxDepth: 3,
      summarizeFn: mockSummarize,
    })
    expect(tree.nodes.size).toBeGreaterThan(0)
    expect(tree.rootId).toBeTruthy()
  })

  it('creates leaf nodes for each group', async () => {
    const messages = Array.from({ length: 4 }, (_, i) => makeMessage('user', `msg-${i}`))
    const tree = await buildSummaryTree(messages, {
      messagesPerLeaf: 2,
      maxDepth: 2,
      summarizeFn: mockSummarize,
    })
    const leaves = Array.from(tree.nodes.values()).filter((n) => n.level === 0)
    expect(leaves).toHaveLength(2)
  })

  it('handles single message', async () => {
    const messages = [makeMessage('user', 'single')]
    const tree = await buildSummaryTree(messages, {
      messagesPerLeaf: 4,
      maxDepth: 2,
      summarizeFn: mockSummarize,
    })
    expect(tree.nodes.size).toBeGreaterThanOrEqual(1)
  })
})

describe('selectLevel', () => {
  it('returns root node when budget is tight', () => {
    // Manually construct a tree to avoid buildSummaryTree maxLevel off-by-one
    const nodes = new Map([
      ['root', { id: 'root', level: 1, summary: 'Root summary', tokenCount: 5 }],
      [
        'leaf-0',
        { id: 'leaf-0', level: 0, summary: 'Detailed leaf 0', tokenCount: 10, messageIds: ['m0'] },
      ],
      [
        'leaf-1',
        { id: 'leaf-1', level: 0, summary: 'Detailed leaf 1', tokenCount: 10, messageIds: ['m1'] },
      ],
    ])
    const tree = { rootId: 'root', nodes, maxLevel: 1 }
    // Budget fits root (5 tokens) but not both leaves (20 tokens)
    const selected = selectLevel(tree, 8)
    expect(selected).toHaveLength(1)
    expect(selected[0].id).toBe('root')
  })

  it('returns highest level that fits budget (most compressed first)', () => {
    // selectLevel iterates from maxLevel down, returns first that fits
    // So with sufficient budget, it returns the root level since root fits first
    const nodes = new Map([
      ['root', { id: 'root', level: 1, summary: 'Root summary', tokenCount: 5 }],
      [
        'leaf-0',
        { id: 'leaf-0', level: 0, summary: 'Leaf 0 detail', tokenCount: 10, messageIds: ['m0'] },
      ],
      [
        'leaf-1',
        { id: 'leaf-1', level: 0, summary: 'Leaf 1 detail', tokenCount: 10, messageIds: ['m1'] },
      ],
    ])
    const tree = { rootId: 'root', nodes, maxLevel: 1 }
    const selected = selectLevel(tree, 100)
    // Returns root (level 1) since it's checked first and fits
    expect(selected).toHaveLength(1)
    expect(selected[0].level).toBe(1)
  })

  it('returns empty for missing root', () => {
    const tree = { rootId: 'missing', nodes: new Map(), maxLevel: 0 }
    expect(selectLevel(tree, 100)).toHaveLength(0)
  })
})

// ============================================================================
// Summarize Strategy
// ============================================================================

describe('createSummarize', () => {
  it('creates strategy with name', () => {
    const strategy = createSummarize()
    expect(strategy.name).toBe('summarize')
  })

  it('returns all messages when count <= preserveRecent', async () => {
    const messages = [makeMessage('user', 'hello'), makeMessage('assistant', 'hi')]
    const strategy = createSummarize({
      preserveRecent: 6,
      summarizeFn: async () => 'summary',
    })
    const result = await strategy.compact(messages, 100000)
    expect(result).toHaveLength(2)
  })

  it('summarizes older messages', async () => {
    const messages = [
      makeMessage('user', 'old msg 1'),
      makeMessage('assistant', 'old reply 1'),
      makeMessage('user', 'old msg 2'),
      makeMessage('assistant', 'old reply 2'),
      makeMessage('user', 'recent msg'),
      makeMessage('assistant', 'recent reply'),
    ]
    const strategy = createSummarize({
      preserveRecent: 2,
      summarizeFn: async () => 'Summary of older messages',
    })
    const result = await strategy.compact(messages, 100000)
    // Should have summary + 2 recent
    expect(result.length).toBeLessThan(messages.length)
    const summaryMsg = result.find((m) => m.content.includes('summary'))
    expect(summaryMsg).toBeDefined()
  })

  it('preserves system message', async () => {
    const messages = [
      makeMessage('system', 'You are helpful'),
      makeMessage('user', 'msg 1'),
      makeMessage('assistant', 'reply 1'),
      makeMessage('user', 'msg 2'),
      makeMessage('assistant', 'reply 2'),
      makeMessage('user', 'msg 3'),
      makeMessage('assistant', 'reply 3'),
      makeMessage('user', 'msg 4'),
      makeMessage('assistant', 'reply 4'),
    ]
    const strategy = createSummarize({
      preserveRecent: 2,
      summarizeFn: async () => 'summary',
    })
    const result = await strategy.compact(messages, 100000)
    expect(result[0].role).toBe('system')
  })

  it('throws when no summarizeFn provided', async () => {
    const strategy = createSummarize()
    const messages = Array.from({ length: 10 }, (_, i) =>
      makeMessage(i % 2 === 0 ? 'user' : 'assistant', `msg-${i}`)
    )
    await expect(strategy.compact(messages, 100000)).rejects.toThrow('No summarizeFn')
  })

  it('returns empty for empty messages', async () => {
    const strategy = createSummarize({ summarizeFn: async () => 'summary' })
    const result = await strategy.compact([], 1000)
    expect(result).toHaveLength(0)
  })
})

// ============================================================================
// Summarization Utilities
// ============================================================================

describe('getSummarizationPrompt', () => {
  it('returns system and user prompts', () => {
    const messages = [makeMessage('user', 'Hello'), makeMessage('assistant', 'Hi there')]
    const { system, user } = getSummarizationPrompt(messages)
    expect(system).toContain('summarizer')
    expect(user).toContain('Hello')
    expect(user).toContain('Hi there')
  })

  it('formats roles correctly', () => {
    const messages = [makeMessage('user', 'test')]
    const { user } = getSummarizationPrompt(messages)
    expect(user).toContain('[User]')
  })
})

describe('extractSummary', () => {
  it('removes common prefixes', () => {
    expect(extractSummary('Here is a summary: The project uses React')).toBe(
      'The project uses React'
    )
    expect(extractSummary("Here's a summary: Some content")).toBe('Some content')
    expect(extractSummary('Summary: Brief overview')).toBe('Brief overview')
  })

  it('returns raw text when no prefix', () => {
    expect(extractSummary('The project is a web app')).toBe('The project is a web app')
  })

  it('trims whitespace', () => {
    expect(extractSummary('  spaced content  ')).toBe('spaced content')
  })
})
