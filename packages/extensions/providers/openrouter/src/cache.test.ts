import { describe, expect, it } from 'vitest'
import { addCacheControlMarkers } from './cache.js'

describe('addCacheControlMarkers (OpenRouter)', () => {
  it('returns empty array for empty input', () => {
    expect(addCacheControlMarkers([])).toEqual([])
  })

  it('marks string system message with cache_control', () => {
    const messages = [
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

  it('marks array system message — last block only', () => {
    const messages = [
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
    expect(blocks[0]!.cache_control).toBeUndefined()
    expect(blocks[1]!.cache_control).toEqual({ type: 'ephemeral' })
  })

  it('marks last 2 user messages with cache_control', () => {
    const messages = [
      { role: 'user', content: 'First question' },
      { role: 'assistant', content: 'First answer' },
      { role: 'user', content: 'Second question' },
      { role: 'assistant', content: 'Second answer' },
      { role: 'user', content: 'Third question' },
    ]
    const result = addCacheControlMarkers(messages)

    // First user message (index 0) — NOT marked
    expect(typeof result[0]!.content).toBe('string')

    // Second user message (index 2) — marked
    const secondUser = result[2]!.content as Array<Record<string, unknown>>
    expect(Array.isArray(secondUser)).toBe(true)
    expect(secondUser[0]!.cache_control).toEqual({ type: 'ephemeral' })

    // Third user message (index 4) — marked
    const thirdUser = result[4]!.content as Array<Record<string, unknown>>
    expect(Array.isArray(thirdUser)).toBe(true)
    expect(thirdUser[0]!.cache_control).toEqual({ type: 'ephemeral' })

    // Assistant messages unchanged
    expect(typeof result[1]!.content).toBe('string')
    expect(typeof result[3]!.content).toBe('string')
  })

  it('handles single user message', () => {
    const messages = [{ role: 'user', content: 'Hello' }]
    const result = addCacheControlMarkers(messages)

    const blocks = result[0]!.content as Array<Record<string, unknown>>
    expect(blocks).toHaveLength(1)
    expect(blocks[0]!.text).toBe('Hello')
    expect(blocks[0]!.cache_control).toEqual({ type: 'ephemeral' })
  })

  it('handles user message with array content', () => {
    const messages = [
      {
        role: 'user',
        content: [
          { type: 'text', text: 'Look at this' },
          { type: 'text', text: 'And this too' },
        ],
      },
    ]
    const result = addCacheControlMarkers(messages)

    const blocks = result[0]!.content as Array<Record<string, unknown>>
    expect(blocks).toHaveLength(2)
    expect(blocks[0]!.cache_control).toBeUndefined()
    expect(blocks[1]!.cache_control).toEqual({ type: 'ephemeral' })
  })

  it('marks both system and user messages', () => {
    const messages = [
      { role: 'system', content: 'You are AVA.' },
      { role: 'user', content: 'Hello' },
      { role: 'assistant', content: 'Hi there' },
      { role: 'user', content: 'How are you?' },
    ]
    const result = addCacheControlMarkers(messages)

    // System marked
    const sysBlocks = result[0]!.content as Array<Record<string, unknown>>
    expect(sysBlocks[0]!.cache_control).toEqual({ type: 'ephemeral' })

    // Both user messages marked
    const user1 = result[1]!.content as Array<Record<string, unknown>>
    expect(user1[0]!.cache_control).toEqual({ type: 'ephemeral' })

    const user2 = result[3]!.content as Array<Record<string, unknown>>
    expect(user2[0]!.cache_control).toEqual({ type: 'ephemeral' })
  })

  it('does not mutate original messages', () => {
    const messages = [
      { role: 'system', content: 'System prompt' },
      { role: 'user', content: 'Hello' },
    ]

    const originalSystem = messages[0]!.content
    const originalUser = messages[1]!.content

    addCacheControlMarkers(messages)

    expect(messages[0]!.content).toBe(originalSystem)
    expect(messages[1]!.content).toBe(originalUser)
  })

  it('does not mutate original array content blocks', () => {
    const blocks = [
      { type: 'text', text: 'First' },
      { type: 'text', text: 'Second' },
    ]
    const messages = [{ role: 'user', content: blocks }]

    addCacheControlMarkers(messages)

    expect((blocks[0] as Record<string, unknown>).cache_control).toBeUndefined()
    expect((blocks[1] as Record<string, unknown>).cache_control).toBeUndefined()
  })

  it('handles no user messages', () => {
    const messages = [
      { role: 'system', content: 'System prompt' },
      { role: 'assistant', content: 'Hello' },
    ]
    const result = addCacheControlMarkers(messages)

    const sysBlocks = result[0]!.content as Array<Record<string, unknown>>
    expect(sysBlocks[0]!.cache_control).toEqual({ type: 'ephemeral' })

    expect(typeof result[1]!.content).toBe('string')
  })

  it('handles no system message', () => {
    const messages = [
      { role: 'user', content: 'Hello' },
      { role: 'assistant', content: 'Hi' },
      { role: 'user', content: 'Follow-up' },
    ]
    const result = addCacheControlMarkers(messages)

    const user1 = result[0]!.content as Array<Record<string, unknown>>
    expect(user1[0]!.cache_control).toEqual({ type: 'ephemeral' })

    const user2 = result[2]!.content as Array<Record<string, unknown>>
    expect(user2[0]!.cache_control).toEqual({ type: 'ephemeral' })
  })

  it('handles null content gracefully', () => {
    const messages = [
      { role: 'assistant', content: null as unknown as string },
      { role: 'user', content: 'Hello' },
    ]
    const result = addCacheControlMarkers(messages)
    expect(result).toHaveLength(2)

    // User message is marked
    const userBlocks = result[1]!.content as Array<Record<string, unknown>>
    expect(userBlocks[0]!.cache_control).toEqual({ type: 'ephemeral' })
  })

  it('preserves message roles and order', () => {
    const messages = [
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

  it('preserves extra properties on messages', () => {
    const messages = [{ role: 'user', content: 'Hello', tool_call_id: 'tc-1' }]
    const result = addCacheControlMarkers(messages)
    expect((result[0] as Record<string, unknown>).tool_call_id).toBe('tc-1')
  })
})
