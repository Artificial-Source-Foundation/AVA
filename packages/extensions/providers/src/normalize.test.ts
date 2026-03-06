import type { ChatMessage } from '@ava/core-v2/llm'
import { describe, expect, it } from 'vitest'
import { normalizeProviderMessages } from './normalize.js'

describe('normalizeProviderMessages', () => {
  it('strips thinking blocks for providers without thinking support', () => {
    const messages: ChatMessage[] = [
      {
        role: 'assistant',
        content: [
          { type: 'thinking' as unknown as 'text', text: 'hidden' } as unknown as never,
          { type: 'text', text: 'visible' },
        ],
      },
    ]

    const result = normalizeProviderMessages(messages, 'openai')
    expect(result[0].content).toEqual([{ type: 'text', text: 'visible' }])
  })

  it('drops orphaned tool_result blocks', () => {
    const messages: ChatMessage[] = [
      {
        role: 'user',
        content: [{ type: 'tool_result', tool_use_id: 'call_1', content: 'orphan' }],
      },
    ]

    const result = normalizeProviderMessages(messages, 'openrouter')
    expect(result[0].content).toEqual([])
  })

  it('normalizes tool call ids to provider format', () => {
    const messages: ChatMessage[] = [
      {
        role: 'assistant',
        content: [{ type: 'tool_use', id: 'toolu_abc', name: 'read_file', input: {} }],
      },
      {
        role: 'user',
        content: [{ type: 'tool_result', tool_use_id: 'toolu_abc', content: 'ok' }],
      },
    ]

    const result = normalizeProviderMessages(messages, 'openrouter')
    const assistant = result[0].content as Array<{ type: string; id: string }>
    const user = result[1].content as Array<{ type: string; tool_use_id: string }>
    expect(assistant[0]?.id.startsWith('call_')).toBe(true)
    expect(user[0]?.tool_use_id).toBe(assistant[0]?.id)
  })

  it('normalizes null content to empty string', () => {
    const messages = [{ role: 'assistant', content: null }] as unknown as ChatMessage[]
    const result = normalizeProviderMessages(messages, 'openai')
    expect(result[0].content).toBe('')
  })
})
