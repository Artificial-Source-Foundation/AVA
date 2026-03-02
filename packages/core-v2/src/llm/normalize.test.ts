import { describe, expect, it } from 'vitest'
import { normalizeMessages, normalizeSystemPosition, stripThinkingBlocks } from './normalize.js'
import type { ChatMessage, ContentBlock } from './types.js'

describe('normalizeMessages', () => {
  it('passes through messages with short tool IDs unchanged', () => {
    const messages: ChatMessage[] = [
      {
        role: 'assistant',
        content: [{ type: 'tool_use', id: 'toolu_abc123', name: 'read_file', input: {} }],
      },
      {
        role: 'user',
        content: [{ type: 'tool_result', tool_use_id: 'toolu_abc123', content: 'ok' }],
      },
    ]
    const result = normalizeMessages(messages)
    // Should return the same references when no changes needed
    expect(result[0]).toBe(messages[0])
    expect(result[1]).toBe(messages[1])
  })

  it('truncates tool IDs longer than 64 chars', () => {
    const longId = 'x'.repeat(100)
    const messages: ChatMessage[] = [
      {
        role: 'assistant',
        content: [{ type: 'tool_use', id: longId, name: 'bash', input: {} }],
      },
      {
        role: 'user',
        content: [{ type: 'tool_result', tool_use_id: longId, content: 'done' }],
      },
    ]
    const result = normalizeMessages(messages)

    const toolUse = (result[0].content as ContentBlock[])[0]
    expect(toolUse.type).toBe('tool_use')
    expect((toolUse as { id: string }).id).toHaveLength(64)

    const toolResult = (result[1].content as ContentBlock[])[0]
    expect(toolResult.type).toBe('tool_result')
    expect((toolResult as { tool_use_id: string }).tool_use_id).toHaveLength(64)
  })

  it('passes through string content unchanged', () => {
    const messages: ChatMessage[] = [{ role: 'user', content: 'hello' }]
    const result = normalizeMessages(messages)
    expect(result[0]).toBe(messages[0])
  })

  it('passes through text blocks unchanged', () => {
    const messages: ChatMessage[] = [
      { role: 'assistant', content: [{ type: 'text', text: 'hello' }] },
    ]
    const result = normalizeMessages(messages)
    expect(result[0]).toBe(messages[0])
  })
})

describe('stripThinkingBlocks', () => {
  it('removes thinking blocks from content', () => {
    const content: ContentBlock[] = [
      {
        type: 'thinking' as unknown as 'text',
        text: 'internal reasoning',
      } as unknown as ContentBlock,
      { type: 'text', text: 'visible response' },
    ]
    const result = stripThinkingBlocks(content)
    expect(result).toHaveLength(1)
    expect((result as ContentBlock[])[0]).toEqual({ type: 'text', text: 'visible response' })
  })

  it('returns strings unchanged', () => {
    const result = stripThinkingBlocks('hello world')
    expect(result).toBe('hello world')
  })

  it('returns content unchanged when no thinking blocks present', () => {
    const content: ContentBlock[] = [
      { type: 'text', text: 'a' },
      { type: 'text', text: 'b' },
    ]
    const result = stripThinkingBlocks(content)
    // Same reference — no filtering needed
    expect(result).toBe(content)
  })
})

describe('normalizeSystemPosition', () => {
  it('moves system messages to front', () => {
    const messages: ChatMessage[] = [
      { role: 'user', content: 'hi' },
      { role: 'system', content: 'you are helpful' },
      { role: 'assistant', content: 'hello' },
    ]
    const result = normalizeSystemPosition(messages)
    expect(result).toHaveLength(3)
    expect(result[0].role).toBe('system')
    expect(result[0].content).toBe('you are helpful')
    expect(result[1].role).toBe('user')
    expect(result[2].role).toBe('assistant')
  })

  it('merges multiple system messages', () => {
    const messages: ChatMessage[] = [
      { role: 'system', content: 'rule one' },
      { role: 'user', content: 'hi' },
      { role: 'system', content: 'rule two' },
    ]
    const result = normalizeSystemPosition(messages)
    expect(result).toHaveLength(2)
    expect(result[0].role).toBe('system')
    expect(result[0].content).toBe('rule one\n\nrule two')
    expect(result[1].role).toBe('user')
  })

  it('returns messages unchanged when no system messages', () => {
    const messages: ChatMessage[] = [
      { role: 'user', content: 'hi' },
      { role: 'assistant', content: 'hello' },
    ]
    const result = normalizeSystemPosition(messages)
    // Same reference — no system messages to move
    expect(result).toBe(messages)
  })
})
