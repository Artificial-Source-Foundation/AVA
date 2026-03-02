/**
 * Context compaction strategies.
 */

import type { ChatMessage } from '@ava/core-v2/llm'
import { describe, expect, it } from 'vitest'
import {
  ALL_STRATEGIES,
  estimateTokens,
  PROTECTED_TOOLS,
  PRUNE_TOKEN_BUDGET,
  pruneStrategy,
  summarizeStrategy,
  truncateStrategy,
} from './strategies.js'

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

// ─── Pair-aware truncation ──────────────────────────────────────────────────

describe('Pair-aware truncation', () => {
  it('keeps tool_use/tool_result message pairs together', () => {
    const messages: ChatMessage[] = [
      msg('user', 'Find bugs'),
      // Assistant with tool_use
      {
        role: 'assistant',
        content: [
          { type: 'text', text: 'Let me check' },
          { type: 'tool_use', id: 'tc-1', name: 'grep', input: { pattern: 'bug' } },
        ],
      },
      // User with tool_result
      {
        role: 'user',
        content: [
          { type: 'tool_result', tool_use_id: 'tc-1', content: 'found bugs', is_error: false },
        ],
      },
      msg('assistant', 'Found some bugs.'),
    ]

    const result = truncateStrategy.compact(messages, 10000)

    // Check that the tool_use assistant and tool_result user are both present
    const hasToolUse = result.some(
      (m) => Array.isArray(m.content) && m.content.some((b) => b.type === 'tool_use')
    )
    const hasToolResult = result.some(
      (m) => Array.isArray(m.content) && m.content.some((b) => b.type === 'tool_result')
    )

    // If one is present, the other must be too
    if (hasToolUse || hasToolResult) {
      expect(hasToolUse).toBe(true)
      expect(hasToolResult).toBe(true)
    }
  })

  it('drops tool pairs as a unit when truncating', () => {
    const messages: ChatMessage[] = [
      msg('user', 'A'.repeat(40)),
      // Tool pair 1
      {
        role: 'assistant',
        content: [{ type: 'tool_use', id: 'tc-1', name: 'read_file', input: { path: '/a.ts' } }],
      },
      {
        role: 'user',
        content: [
          { type: 'tool_result', tool_use_id: 'tc-1', content: 'A'.repeat(40), is_error: false },
        ],
      },
      // Final response
      msg('assistant', 'D'.repeat(40)),
    ]

    // Very tight budget — only room for ~2 items
    const result = truncateStrategy.compact(messages, 25)

    // Should not have orphan tool_use without tool_result or vice versa
    const toolUseCount = result.filter(
      (m) => Array.isArray(m.content) && m.content.some((b) => b.type === 'tool_use')
    ).length
    const toolResultCount = result.filter(
      (m) => Array.isArray(m.content) && m.content.some((b) => b.type === 'tool_result')
    ).length

    expect(toolUseCount).toBe(toolResultCount)
  })
})

// ─── pruneStrategy ─────────────────────────────────────────────────────────

describe('pruneStrategy', () => {
  it('has correct name', () => {
    expect(pruneStrategy.name).toBe('prune')
  })

  it('returns empty array for empty messages', () => {
    const result = pruneStrategy.compact([], 10000)
    expect(result).toEqual([])
  })

  it('preserves all messages when tool results are within token budget', () => {
    // Small tool results that fit within 40K tokens
    const messages: ChatMessage[] = [
      msg('user', 'Read this file'),
      {
        role: 'assistant',
        content: [{ type: 'tool_use', id: 'tc-1', name: 'read_file', input: { path: '/a.ts' } }],
      },
      {
        role: 'user',
        content: [
          { type: 'tool_result', tool_use_id: 'tc-1', content: 'const x = 1;', is_error: false },
        ],
      },
      msg('assistant', 'I see the file.'),
    ]

    const result = pruneStrategy.compact(messages, 10000)
    // All messages preserved, no content cleared
    expect(result).toHaveLength(4)
    const resultBlock = (result[2]!.content as Array<{ type: string; content: string }>)[0]!
    expect(resultBlock.content).toBe('const x = 1;')
  })

  it('replaces older tool result content that exceeds token budget', () => {
    // Create large tool results that exceed 40K tokens (160K chars)
    const bigContent = 'x'.repeat(200_000) // 50K tokens
    const smallContent = 'y'.repeat(40) // 10 tokens

    const messages: ChatMessage[] = [
      msg('user', 'First'),
      {
        role: 'assistant',
        content: [
          { type: 'tool_use', id: 'tc-old', name: 'read_file', input: { path: '/big.ts' } },
        ],
      },
      {
        role: 'user',
        content: [
          { type: 'tool_result', tool_use_id: 'tc-old', content: bigContent, is_error: false },
        ],
      },
      msg('user', 'Now read a small file'),
      {
        role: 'assistant',
        content: [
          { type: 'tool_use', id: 'tc-new', name: 'read_file', input: { path: '/small.ts' } },
        ],
      },
      {
        role: 'user',
        content: [
          { type: 'tool_result', tool_use_id: 'tc-new', content: smallContent, is_error: false },
        ],
      },
    ]

    const result = pruneStrategy.compact(messages, 10000)
    expect(result).toHaveLength(6) // all messages still present

    // The newer (small) tool result should be preserved
    const newResultMsg = result[5]!.content as Array<{ type: string; content: string }>
    expect(newResultMsg[0]!.content).toBe(smallContent)

    // The older (big) tool result should be cleared
    const oldResultMsg = result[2]!.content as Array<{ type: string; content: string }>
    expect(oldResultMsg[0]!.content).toBe('[Old tool result content cleared]')
  })

  it('preserves recent tool results within budget', () => {
    // Create two results that together fit in budget (40K tokens = 160K chars)
    const content1 = 'a'.repeat(80_000) // 20K tokens
    const content2 = 'b'.repeat(80_000) // 20K tokens

    const messages: ChatMessage[] = [
      {
        role: 'assistant',
        content: [{ type: 'tool_use', id: 'tc-1', name: 'grep', input: { pattern: 'foo' } }],
      },
      {
        role: 'user',
        content: [{ type: 'tool_result', tool_use_id: 'tc-1', content: content1, is_error: false }],
      },
      {
        role: 'assistant',
        content: [{ type: 'tool_use', id: 'tc-2', name: 'grep', input: { pattern: 'bar' } }],
      },
      {
        role: 'user',
        content: [{ type: 'tool_result', tool_use_id: 'tc-2', content: content2, is_error: false }],
      },
    ]

    const result = pruneStrategy.compact(messages, 10000)
    // Both fit within 40K tokens, so neither should be cleared
    const r1 = (result[1]!.content as Array<{ type: string; content: string }>)[0]!
    const r2 = (result[3]!.content as Array<{ type: string; content: string }>)[0]!
    expect(r1.content).toBe(content1)
    expect(r2.content).toBe(content2)
  })

  it('never clears protected tool results (skill)', () => {
    const bigSkillContent = 'x'.repeat(200_000) // 50K tokens — over budget

    const messages: ChatMessage[] = [
      {
        role: 'assistant',
        content: [{ type: 'tool_use', id: 'tc-skill', name: 'skill', input: { name: 'react' } }],
      },
      {
        role: 'user',
        content: [
          {
            type: 'tool_result',
            tool_use_id: 'tc-skill',
            content: bigSkillContent,
            is_error: false,
          },
        ],
      },
      {
        role: 'assistant',
        content: [{ type: 'tool_use', id: 'tc-read', name: 'read_file', input: { path: '/a.ts' } }],
      },
      {
        role: 'user',
        content: [
          {
            type: 'tool_result',
            tool_use_id: 'tc-read',
            content: 'z'.repeat(200_000), // also 50K tokens
            is_error: false,
          },
        ],
      },
    ]

    const result = pruneStrategy.compact(messages, 10000)

    // Skill result must be preserved (protected)
    const skillResult = (result[1]!.content as Array<{ type: string; content: string }>)[0]!
    expect(skillResult.content).toBe(bigSkillContent)

    // read_file result is NOT protected and exceeds remaining budget
    // The newer read_file (tc-read) consumes 50K tokens, budget is 40K, so it gets cleared
    const readResult = (result[3]!.content as Array<{ type: string; content: string }>)[0]!
    // The read_file is the most recent, so it gets first shot at the budget.
    // It's 50K tokens > 40K budget, so the budget runs out partway.
    // Actually: walking backwards, tc-read is first (50K > 40K), so tc-read is cleared.
    expect(readResult.content).toBe('[Old tool result content cleared]')
  })

  it('never clears protected tool results (memory_read)', () => {
    const bigMemContent = 'y'.repeat(200_000) // 50K tokens

    const messages: ChatMessage[] = [
      {
        role: 'assistant',
        content: [
          { type: 'tool_use', id: 'tc-mem', name: 'memory_read', input: { key: 'patterns' } },
        ],
      },
      {
        role: 'user',
        content: [
          { type: 'tool_result', tool_use_id: 'tc-mem', content: bigMemContent, is_error: false },
        ],
      },
    ]

    const result = pruneStrategy.compact(messages, 10000)
    const memResult = (result[1]!.content as Array<{ type: string; content: string }>)[0]!
    expect(memResult.content).toBe(bigMemContent) // never cleared
  })

  it('never clears protected tool results (load_skill)', () => {
    const bigContent = 'z'.repeat(200_000) // 50K tokens

    const messages: ChatMessage[] = [
      {
        role: 'assistant',
        content: [{ type: 'tool_use', id: 'tc-ls', name: 'load_skill', input: { name: 'ts' } }],
      },
      {
        role: 'user',
        content: [
          { type: 'tool_result', tool_use_id: 'tc-ls', content: bigContent, is_error: false },
        ],
      },
    ]

    const result = pruneStrategy.compact(messages, 10000)
    const lsResult = (result[1]!.content as Array<{ type: string; content: string }>)[0]!
    expect(lsResult.content).toBe(bigContent) // never cleared
  })

  it('preserves non-tool messages unchanged', () => {
    const messages: ChatMessage[] = [
      msg('system', 'You are AVA.'),
      msg('user', 'Hello world'),
      msg('assistant', 'Hi there!'),
      msg('user', 'How are you?'),
      msg('assistant', 'I am fine.'),
    ]

    const result = pruneStrategy.compact(messages, 10000)
    expect(result).toEqual(messages) // no tool results, nothing to prune
  })

  it('handles mixed tool and non-tool messages', () => {
    const bigContent = 'x'.repeat(200_000) // 50K tokens

    const messages: ChatMessage[] = [
      msg('user', 'Start'),
      {
        role: 'assistant',
        content: [
          { type: 'text', text: 'Let me check' },
          { type: 'tool_use', id: 'tc-1', name: 'bash', input: { command: 'ls' } },
        ],
      },
      {
        role: 'user',
        content: [
          { type: 'tool_result', tool_use_id: 'tc-1', content: bigContent, is_error: false },
        ],
      },
      msg('assistant', 'I see the files.'),
      msg('user', 'Thanks'),
      {
        role: 'assistant',
        content: [{ type: 'tool_use', id: 'tc-2', name: 'read_file', input: { path: '/b.ts' } }],
      },
      {
        role: 'user',
        content: [{ type: 'tool_result', tool_use_id: 'tc-2', content: 'small', is_error: false }],
      },
    ]

    const result = pruneStrategy.compact(messages, 10000)
    expect(result).toHaveLength(7) // all messages still present

    // Text messages are preserved exactly
    expect(result[0]).toEqual(msg('user', 'Start'))
    expect(result[3]).toEqual(msg('assistant', 'I see the files.'))
    expect(result[4]).toEqual(msg('user', 'Thanks'))

    // Newer small result is preserved
    const newResult = (result[6]!.content as Array<{ type: string; content: string }>)[0]!
    expect(newResult.content).toBe('small')

    // Older big result is cleared
    const oldResult = (result[2]!.content as Array<{ type: string; content: string }>)[0]!
    expect(oldResult.content).toBe('[Old tool result content cleared]')
  })

  it('token counting uses Math.ceil(length / 4)', () => {
    expect(estimateTokens('abcd')).toBe(1) // 4 chars = 1 token
    expect(estimateTokens('abcde')).toBe(2) // 5 chars = 2 tokens (ceil)
    expect(estimateTokens('')).toBe(0)
    expect(estimateTokens('a')).toBe(1)
    expect(estimateTokens('ab')).toBe(1)
    expect(estimateTokens('abc')).toBe(1)
  })

  it('preserves tool_use_id and is_error in cleared results', () => {
    const bigContent = 'x'.repeat(200_000)

    const messages: ChatMessage[] = [
      {
        role: 'assistant',
        content: [{ type: 'tool_use', id: 'tc-err', name: 'bash', input: { command: 'fail' } }],
      },
      {
        role: 'user',
        content: [
          { type: 'tool_result', tool_use_id: 'tc-err', content: bigContent, is_error: true },
        ],
      },
      {
        role: 'assistant',
        content: [{ type: 'tool_use', id: 'tc-ok', name: 'bash', input: { command: 'ls' } }],
      },
      {
        role: 'user',
        content: [
          { type: 'tool_result', tool_use_id: 'tc-ok', content: 'small output', is_error: false },
        ],
      },
    ]

    const result = pruneStrategy.compact(messages, 10000)
    const clearedBlock = (
      result[1]!.content as Array<{
        type: string
        tool_use_id: string
        content: string
        is_error?: boolean
      }>
    )[0]!

    expect(clearedBlock.tool_use_id).toBe('tc-err')
    expect(clearedBlock.is_error).toBe(true)
    expect(clearedBlock.content).toBe('[Old tool result content cleared]')
  })

  it('exports expected constants', () => {
    expect(PRUNE_TOKEN_BUDGET).toBe(40_000)
    expect(PROTECTED_TOOLS).toContain('skill')
    expect(PROTECTED_TOOLS).toContain('memory_read')
    expect(PROTECTED_TOOLS).toContain('load_skill')
    expect(PROTECTED_TOOLS.size).toBe(3)
  })

  it('handles multiple tool results in a single message', () => {
    const bigContent = 'x'.repeat(200_000) // 50K tokens

    const messages: ChatMessage[] = [
      {
        role: 'assistant',
        content: [
          { type: 'tool_use', id: 'tc-a', name: 'bash', input: { command: 'a' } },
          { type: 'tool_use', id: 'tc-b', name: 'bash', input: { command: 'b' } },
        ],
      },
      {
        role: 'user',
        content: [
          { type: 'tool_result', tool_use_id: 'tc-a', content: bigContent, is_error: false },
          { type: 'tool_result', tool_use_id: 'tc-b', content: 'small', is_error: false },
        ],
      },
    ]

    const result = pruneStrategy.compact(messages, 10000)
    const blocks = result[1]!.content as Array<{ type: string; content: string }>

    // tc-b is walked first (backwards), fits in budget (small)
    expect(blocks[1]!.content).toBe('small')
    // tc-a is 50K tokens, exceeds remaining budget → cleared
    expect(blocks[0]!.content).toBe('[Old tool result content cleared]')
  })
})

// ─── ALL_STRATEGIES ─────────────────────────────────────────────────────────

describe('ALL_STRATEGIES', () => {
  it('contains all three strategies', () => {
    expect(ALL_STRATEGIES).toHaveLength(3)
    expect(ALL_STRATEGIES).toContain(pruneStrategy)
    expect(ALL_STRATEGIES).toContain(truncateStrategy)
    expect(ALL_STRATEGIES).toContain(summarizeStrategy)
  })

  it('has prune strategy first (runs before truncate/summarize)', () => {
    expect(ALL_STRATEGIES[0]).toBe(pruneStrategy)
  })
})
