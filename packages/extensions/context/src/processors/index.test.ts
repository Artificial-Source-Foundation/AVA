import { describe, expect, it } from 'vitest'

type ChatMessage = {
  role: 'system' | 'user' | 'assistant'
  _variant?: 'ephemeral' | 'persistent'
  content:
    | string
    | Array<
        | { type: 'text'; text: string }
        | { type: 'image'; source: { type: 'base64' | 'url'; media_type: string; data: string } }
        | { type: 'tool_use'; id: string; name: string; input: unknown }
        | { type: 'tool_result'; tool_use_id: string; content: string; is_error?: boolean }
      >
}

import { createCacheControlProcessor } from './cache-control.js'
import { createHistoryProcessorByName, runHistoryProcessors } from './index.js'
import { createLastNObservationsProcessor } from './last-n-observations.js'
import { createTagToolCallsProcessor } from './tag-tool-calls.js'

function msg(role: ChatMessage['role'], content: ChatMessage['content']): ChatMessage {
  return { role, content }
}

describe('createLastNObservationsProcessor', () => {
  it('keeps last N tool observations unchanged', () => {
    const processor = createLastNObservationsProcessor({ keepRecent: 1 })
    const messages: ChatMessage[] = [
      msg('user', [{ type: 'tool_result', tool_use_id: 'a', content: 'old', is_error: false }]),
      msg('user', [{ type: 'tool_result', tool_use_id: 'b', content: 'new', is_error: false }]),
    ]
    const result = processor(messages)
    const second = result[1]
    if (!second || typeof second.content === 'string') throw new Error('invalid fixture')
    const block = second.content[0]
    if (!block || block.type !== 'tool_result') throw new Error('invalid fixture')
    expect(block.content).toBe('new')
  })

  it('replaces old observations with omitted line count', () => {
    const processor = createLastNObservationsProcessor({ keepRecent: 0 })
    const messages: ChatMessage[] = [
      msg('user', [
        {
          type: 'tool_result',
          tool_use_id: 'a',
          content: 'line1\nline2\nline3',
          is_error: false,
        },
      ]),
    ]
    const result = processor(messages)
    const first = result[0]
    if (!first || typeof first.content === 'string') throw new Error('invalid fixture')
    const block = first.content[0]
    if (!block || block.type !== 'tool_result') throw new Error('invalid fixture')
    expect(block.content).toBe('(3 lines omitted)')
  })

  it('marks clipped response when observation exceeds max chars', () => {
    const processor = createLastNObservationsProcessor({ keepRecent: 0, maxObservationChars: 10 })
    const messages: ChatMessage[] = [
      msg('user', [
        {
          type: 'tool_result',
          tool_use_id: 'a',
          content: '123456789012345',
          is_error: false,
        },
      ]),
    ]
    const result = processor(messages)
    const first = result[0]
    if (!first || typeof first.content === 'string') throw new Error('invalid fixture')
    const block = first.content[0]
    if (!block || block.type !== 'tool_result') throw new Error('invalid fixture')
    expect(block.content).toContain('response clipped')
  })
})

describe('createCacheControlProcessor', () => {
  it('is no-op for non-anthropic providers', () => {
    const processor = createCacheControlProcessor('openai', { markLastMessages: 2 })
    const messages: ChatMessage[] = [msg('user', 'u1'), msg('assistant', 'a1')]
    const result = processor(messages)
    expect(result).toEqual(messages)
  })

  it('marks last user messages for anthropic provider', () => {
    const processor = createCacheControlProcessor('anthropic', { markLastMessages: 2 })
    const messages: ChatMessage[] = [msg('user', 'u1'), msg('assistant', 'a1'), msg('user', 'u2')]
    const result = processor(messages)
    const first = result[0]
    const last = result[2]
    expect(first?._variant).toBeUndefined()
    expect(last?._variant).toBe('ephemeral')
  })
})

describe('createTagToolCallsProcessor', () => {
  it('wraps tool result content with tool name tags', () => {
    const processor = createTagToolCallsProcessor()
    const messages: ChatMessage[] = [
      msg('assistant', [{ type: 'tool_use', id: 'call-1', name: 'grep', input: {} }]),
      msg('user', [
        { type: 'tool_result', tool_use_id: 'call-1', content: 'result', is_error: false },
      ]),
    ]
    const result = processor(messages)
    const second = result[1]
    if (!second || typeof second.content === 'string') throw new Error('invalid fixture')
    const block = second.content[0]
    if (!block || block.type !== 'tool_result') throw new Error('invalid fixture')
    expect(block.content).toContain('<tool_output name="grep">')
  })
})

describe('history processor pipeline', () => {
  it('creates processors by known names and runs in order', () => {
    const messages: ChatMessage[] = [
      msg('assistant', [{ type: 'tool_use', id: 'call-1', name: 'grep', input: {} }]),
      msg('user', [
        { type: 'tool_result', tool_use_id: 'call-1', content: 'line1\nline2', is_error: false },
      ]),
      msg('user', 'recent'),
    ]

    const pByName = createHistoryProcessorByName('tag-tool-calls', { provider: 'anthropic' })
    expect(pByName).not.toBeNull()

    const result = runHistoryProcessors(messages, [
      createLastNObservationsProcessor({ keepRecent: 0 }),
      pByName!,
    ])
    const second = result[1]
    if (!second || typeof second.content === 'string') throw new Error('invalid fixture')
    const block = second.content[0]
    if (!block || block.type !== 'tool_result') throw new Error('invalid fixture')
    expect(block.content).toContain('<tool_output')
    expect(block.content).toContain('lines omitted')
  })
})
