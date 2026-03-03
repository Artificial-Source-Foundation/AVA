import { describe, expect, it } from 'vitest'

type ChatMessage = {
  role: 'system' | 'user' | 'assistant'
  content:
    | string
    | Array<
        | { type: 'text'; text: string }
        | { type: 'image'; source: { type: 'base64' | 'url'; media_type: string; data: string } }
        | { type: 'tool_use'; id: string; name: string; input: unknown }
        | { type: 'tool_result'; tool_use_id: string; content: string; is_error?: boolean }
      >
}

import {
  ALL_STRATEGIES,
  amortizedForgettingStrategy,
  backwardFifoStrategy,
  observationMaskingStrategy,
  slidingWindowStrategy,
  summarizeStrategy,
  truncateStrategy,
} from './strategies.js'

function msg(role: ChatMessage['role'], content: ChatMessage['content']): ChatMessage {
  return { role, content }
}

describe('truncateStrategy', () => {
  it('preserves system message and keeps newest messages', () => {
    const messages: ChatMessage[] = [
      msg('system', 'sys'),
      msg('user', 'old'),
      msg('assistant', 'old-reply'),
      msg('user', 'new'),
      msg('assistant', 'new-reply'),
    ]
    const result = truncateStrategy.compact(messages, 4)
    expect(result[0]?.role).toBe('system')
    expect(result.some((m) => m.content === 'new-reply')).toBe(true)
  })
})

describe('summarizeStrategy', () => {
  it('adds summary system message with counts', () => {
    const messages: ChatMessage[] = [msg('system', 'sys')]
    for (let i = 0; i < 12; i++) {
      messages.push(msg(i % 2 === 0 ? 'user' : 'assistant', `m${i}`))
    }
    const result = summarizeStrategy.compact(messages, 9999)
    expect(result[0]?.role).toBe('system')
    expect(String(result[0]?.content)).toContain('Summary of earlier conversation')
    expect(String(result[0]?.content)).toContain('assistant responses')
  })
})

describe('backwardFifoStrategy', () => {
  it('prunes old tool output first', () => {
    const old = 'x'.repeat(160_000)
    const recent = 'y'.repeat(220_000)
    const messages: ChatMessage[] = [
      msg('assistant', [{ type: 'tool_use', id: 't1', name: 'grep', input: {} }]),
      msg('user', [{ type: 'tool_result', tool_use_id: 't1', content: old, is_error: false }]),
      msg('assistant', [{ type: 'tool_use', id: 't2', name: 'read_file', input: {} }]),
      msg('user', [{ type: 'tool_result', tool_use_id: 't2', content: recent, is_error: false }]),
    ]

    const result = backwardFifoStrategy.compact(messages, 10_000)
    const oldResult = result[1]
    if (!oldResult || typeof oldResult.content === 'string') throw new Error('invalid fixture')
    const block = oldResult.content[0]
    if (!block || block.type !== 'tool_result') throw new Error('invalid fixture')
    expect(block.content).toContain('[Tool output pruned - originally')
  })

  it('never prunes protected tool output', () => {
    const big = 'z'.repeat(180_000)
    const messages: ChatMessage[] = [
      msg('assistant', [{ type: 'tool_use', id: 't1', name: 'skill', input: {} }]),
      msg('user', [{ type: 'tool_result', tool_use_id: 't1', content: big, is_error: false }]),
      msg('assistant', [{ type: 'tool_use', id: 't2', name: 'grep', input: {} }]),
      msg('user', [
        { type: 'tool_result', tool_use_id: 't2', content: 'k'.repeat(260_000), is_error: false },
      ]),
    ]

    const result = backwardFifoStrategy.compact(messages, 10_000)
    const protectedMsg = result[1]
    if (!protectedMsg || typeof protectedMsg.content === 'string')
      throw new Error('invalid fixture')
    const block = protectedMsg.content[0]
    if (!block || block.type !== 'tool_result') throw new Error('invalid fixture')
    expect(block.content).toBe(big)
  })
})

describe('slidingWindowStrategy', () => {
  it('keeps recent window and preserves system', () => {
    const messages: ChatMessage[] = [msg('system', 'sys')]
    for (let i = 0; i < 20; i++) {
      messages.push(msg(i % 2 === 0 ? 'user' : 'assistant', `m${i}`))
    }
    const result = slidingWindowStrategy.compact(messages, 10_000)
    expect(result[0]?.role).toBe('system')
    expect(result.length).toBeLessThan(messages.length)
  })
})

describe('observationMaskingStrategy', () => {
  it('masks large old tool results', () => {
    const messages: ChatMessage[] = [
      msg('system', 'sys'),
      msg('assistant', [{ type: 'tool_use', id: 't1', name: 'grep', input: {} }]),
      msg('user', [
        { type: 'tool_result', tool_use_id: 't1', content: 'a'.repeat(20_000), is_error: false },
      ]),
    ]
    for (let i = 0; i < 14; i++) {
      messages.push(msg(i % 2 === 0 ? 'user' : 'assistant', `recent-${i}`))
    }
    const result = observationMaskingStrategy.compact(messages, 10_000)
    const item = result[2]
    if (!item || typeof item.content === 'string') throw new Error('invalid fixture')
    const block = item.content[0]
    if (!block || block.type !== 'tool_result') throw new Error('invalid fixture')
    expect(block.content).toContain('Tool output masked')
  })
})

describe('amortizedForgettingStrategy', () => {
  it('reduces detail in old tool results while keeping recent untouched', () => {
    const messages: ChatMessage[] = [
      msg('system', 'sys'),
      msg('assistant', [{ type: 'tool_use', id: 't1', name: 'grep', input: {} }]),
      msg('user', [
        { type: 'tool_result', tool_use_id: 't1', content: 'b'.repeat(50_000), is_error: false },
      ]),
    ]
    for (let i = 0; i < 14; i++) {
      messages.push(msg(i % 2 === 0 ? 'assistant' : 'user', `recent-${i}`))
    }
    const result = amortizedForgettingStrategy.compact(messages, 500)
    const old = result[2]
    if (!old || typeof old.content === 'string') throw new Error('invalid fixture')
    const block = old.content[0]
    if (!block || block.type !== 'tool_result') throw new Error('invalid fixture')
    expect(block.content).toContain('Older details omitted by amortized forgetting')
    expect(result.at(-1)?.content).toBe('recent-13')
  })
})

describe('ALL_STRATEGIES', () => {
  it('contains expanded strategy list', () => {
    const names = ALL_STRATEGIES.map((s) => s.name)
    expect(names).toContain('prune')
    expect(names).toContain('backward-fifo')
    expect(names).toContain('sliding-window')
    expect(names).toContain('observation-masking')
    expect(names).toContain('amortized-forgetting')
    expect(names.length).toBeGreaterThanOrEqual(7)
  })
})
