import type { ChatMessage } from '@ava/core-v2/llm'
import { describe, expect, it } from 'vitest'
import { addCacheControlMarkers } from './cache.js'

describe('addCacheControlMarkers (Anthropic)', () => {
  it('returns empty array for empty input', () => {
    expect(addCacheControlMarkers([])).toEqual([])
  })

  it('marks system message with cache_control', () => {
    const messages: ChatMessage[] = [
      { role: 'system', content: 'You are AVA.' },
      { role: 'user', content: 'Hello' },
    ]
    const result = addCacheControlMarkers(messages)

    // System message should be converted to block format with cache_control
    expect(Array.isArray(result[0]!.content)).toBe(true)
    const blocks = result[0]!.content as Array<Record<string, unknown>>
    expect(blocks).toHaveLength(1)
    expect(blocks[0]!.type).toBe('text')
    expect(blocks[0]!.text).toBe('You are AVA.')
    expect(blocks[0]!.cache_control).toEqual({ type: 'ephemeral' })
  })

  it('marks system message with block content', () => {
    const messages: ChatMessage[] = [
      {
        role: 'system',
        content: [
          { type: 'text', text: 'You are AVA.' },
          { type: 'text', text: 'Be helpful.' },
        ],
      },
      { role: 'user', content: 'Hello' },
    ]
    const result = addCacheControlMarkers(messages)

    const blocks = result[0]!.content as Array<Record<string, unknown>>
    expect(blocks).toHaveLength(2)
    // Only the last block should have cache_control
    expect(blocks[0]!.cache_control).toBeUndefined()
    expect(blocks[1]!.cache_control).toEqual({ type: 'ephemeral' })
  })

  it('marks last 2 user messages with cache_control', () => {
    const messages: ChatMessage[] = [
      { role: 'user', content: 'First question' },
      { role: 'assistant', content: 'First answer' },
      { role: 'user', content: 'Second question' },
      { role: 'assistant', content: 'Second answer' },
      { role: 'user', content: 'Third question' },
    ]
    const result = addCacheControlMarkers(messages)

    // First user message (index 0) — NOT marked
    expect(typeof result[0]!.content).toBe('string')

    // Second user message (index 2) — marked (second-to-last user)
    const secondUser = result[2]!.content as Array<Record<string, unknown>>
    expect(Array.isArray(secondUser)).toBe(true)
    expect(secondUser[0]!.cache_control).toEqual({ type: 'ephemeral' })

    // Third user message (index 4) — marked (last user)
    const thirdUser = result[4]!.content as Array<Record<string, unknown>>
    expect(Array.isArray(thirdUser)).toBe(true)
    expect(thirdUser[0]!.cache_control).toEqual({ type: 'ephemeral' })

    // Assistant messages unchanged
    expect(typeof result[1]!.content).toBe('string')
    expect(typeof result[3]!.content).toBe('string')
  })

  it('handles single user message', () => {
    const messages: ChatMessage[] = [{ role: 'user', content: 'Hello' }]
    const result = addCacheControlMarkers(messages)

    const blocks = result[0]!.content as Array<Record<string, unknown>>
    expect(blocks).toHaveLength(1)
    expect(blocks[0]!.cache_control).toEqual({ type: 'ephemeral' })
  })

  it('handles user message with block content', () => {
    const messages: ChatMessage[] = [
      {
        role: 'user',
        content: [
          { type: 'text', text: 'Look at this' },
          {
            type: 'tool_result',
            tool_use_id: 'tc-1',
            content: 'file contents',
          },
        ],
      },
    ]
    const result = addCacheControlMarkers(messages)

    const blocks = result[0]!.content as Array<Record<string, unknown>>
    expect(blocks).toHaveLength(2)
    // Only last block marked
    expect(blocks[0]!.cache_control).toBeUndefined()
    expect(blocks[1]!.cache_control).toEqual({ type: 'ephemeral' })
  })

  it('marks both system and user messages', () => {
    const messages: ChatMessage[] = [
      { role: 'system', content: 'You are AVA.' },
      { role: 'user', content: 'Hello' },
      { role: 'assistant', content: 'Hi there' },
      { role: 'user', content: 'How are you?' },
    ]
    const result = addCacheControlMarkers(messages)

    // System marked
    const sysBlocks = result[0]!.content as Array<Record<string, unknown>>
    expect(sysBlocks[0]!.cache_control).toEqual({ type: 'ephemeral' })

    // Both user messages marked (only 2 user messages total)
    const user1 = result[1]!.content as Array<Record<string, unknown>>
    expect(user1[0]!.cache_control).toEqual({ type: 'ephemeral' })

    const user2 = result[3]!.content as Array<Record<string, unknown>>
    expect(user2[0]!.cache_control).toEqual({ type: 'ephemeral' })

    // Assistant unchanged
    expect(typeof result[2]!.content).toBe('string')
  })

  it('does not mutate original messages', () => {
    const messages: ChatMessage[] = [
      { role: 'system', content: 'System prompt' },
      { role: 'user', content: 'Hello' },
    ]

    const originalSystem = messages[0]!.content
    const originalUser = messages[1]!.content

    addCacheControlMarkers(messages)

    // Original should be unchanged
    expect(messages[0]!.content).toBe(originalSystem)
    expect(messages[1]!.content).toBe(originalUser)
    expect(typeof messages[0]!.content).toBe('string')
    expect(typeof messages[1]!.content).toBe('string')
  })

  it('does not mutate original block content', () => {
    const blocks = [
      { type: 'text' as const, text: 'First' },
      { type: 'text' as const, text: 'Second' },
    ]
    const messages: ChatMessage[] = [{ role: 'user', content: blocks }]

    addCacheControlMarkers(messages)

    // Original blocks should not have cache_control
    expect((blocks[0] as Record<string, unknown>).cache_control).toBeUndefined()
    expect((blocks[1] as Record<string, unknown>).cache_control).toBeUndefined()
  })

  it('handles no user messages', () => {
    const messages: ChatMessage[] = [
      { role: 'system', content: 'System prompt' },
      { role: 'assistant', content: 'Hello' },
    ]
    const result = addCacheControlMarkers(messages)

    // System still marked
    const sysBlocks = result[0]!.content as Array<Record<string, unknown>>
    expect(sysBlocks[0]!.cache_control).toEqual({ type: 'ephemeral' })

    // Assistant unchanged
    expect(typeof result[1]!.content).toBe('string')
  })

  it('handles no system message', () => {
    const messages: ChatMessage[] = [
      { role: 'user', content: 'Hello' },
      { role: 'assistant', content: 'Hi' },
      { role: 'user', content: 'Follow-up' },
    ]
    const result = addCacheControlMarkers(messages)

    // Both user messages marked
    const user1 = result[0]!.content as Array<Record<string, unknown>>
    expect(user1[0]!.cache_control).toEqual({ type: 'ephemeral' })

    const user2 = result[2]!.content as Array<Record<string, unknown>>
    expect(user2[0]!.cache_control).toEqual({ type: 'ephemeral' })
  })

  it('preserves message roles and order', () => {
    const messages: ChatMessage[] = [
      { role: 'system', content: 'System' },
      { role: 'user', content: 'User 1' },
      { role: 'assistant', content: 'Asst 1' },
      { role: 'user', content: 'User 2' },
    ]
    const result = addCacheControlMarkers(messages)

    expect(result).toHaveLength(4)
    expect(result[0]!.role).toBe('system')
    expect(result[1]!.role).toBe('user')
    expect(result[2]!.role).toBe('assistant')
    expect(result[3]!.role).toBe('user')
  })

  it('handles empty block content array', () => {
    const messages: ChatMessage[] = [{ role: 'user', content: [] }]
    const result = addCacheControlMarkers(messages)

    // Empty content stays empty (markContent returns [])
    const content = result[0]!.content as Array<Record<string, unknown>>
    expect(content).toEqual([])
  })
})
