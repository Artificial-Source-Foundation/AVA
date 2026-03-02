import { describe, expect, it } from 'vitest'
import type { ChatMessage } from '../llm/types.js'
import { exportSessionToJSON, exportSessionToMarkdown } from './export.js'

describe('exportSessionToMarkdown', () => {
  it('returns placeholder for empty messages', () => {
    const result = exportSessionToMarkdown([])
    expect(result).toContain('No messages')
  })

  it('exports simple text messages', () => {
    const messages: ChatMessage[] = [
      { role: 'user', content: 'Hello' },
      { role: 'assistant', content: 'Hi there!' },
    ]
    const result = exportSessionToMarkdown(messages)
    expect(result).toContain('## User')
    expect(result).toContain('Hello')
    expect(result).toContain('## Assistant')
    expect(result).toContain('Hi there!')
  })

  it('exports system messages', () => {
    const messages: ChatMessage[] = [{ role: 'system', content: 'You are a helper.' }]
    const result = exportSessionToMarkdown(messages)
    expect(result).toContain('## System')
    expect(result).toContain('You are a helper.')
  })

  it('exports tool use blocks', () => {
    const messages: ChatMessage[] = [
      {
        role: 'assistant',
        content: [
          { type: 'text', text: 'Let me read that file.' },
          {
            type: 'tool_use',
            id: 'call_1',
            name: 'read_file',
            input: { path: '/tmp/test.txt' },
          },
        ],
      },
    ]
    const result = exportSessionToMarkdown(messages)
    expect(result).toContain('Let me read that file.')
    expect(result).toContain('Tool call: read_file')
    expect(result).toContain('/tmp/test.txt')
  })

  it('exports tool result blocks', () => {
    const messages: ChatMessage[] = [
      {
        role: 'user',
        content: [
          {
            type: 'tool_result',
            tool_use_id: 'call_1',
            content: 'File contents here',
            is_error: false,
          },
        ],
      },
    ]
    const result = exportSessionToMarkdown(messages)
    expect(result).toContain('Tool result')
    expect(result).toContain('success')
    expect(result).toContain('File contents here')
  })

  it('exports error tool results', () => {
    const messages: ChatMessage[] = [
      {
        role: 'user',
        content: [
          {
            type: 'tool_result',
            tool_use_id: 'call_1',
            content: 'File not found',
            is_error: true,
          },
        ],
      },
    ]
    const result = exportSessionToMarkdown(messages)
    expect(result).toContain('error')
  })

  it('exports image blocks', () => {
    const messages: ChatMessage[] = [
      {
        role: 'user',
        content: [
          {
            type: 'image',
            source: { type: 'base64', media_type: 'image/png', data: 'abc123' },
          },
        ],
      },
    ]
    const result = exportSessionToMarkdown(messages)
    expect(result).toContain('[Image: image/png]')
  })

  it('includes separator between messages', () => {
    const messages: ChatMessage[] = [
      { role: 'user', content: 'A' },
      { role: 'assistant', content: 'B' },
    ]
    const result = exportSessionToMarkdown(messages)
    expect(result).toContain('---')
  })
})

describe('exportSessionToJSON', () => {
  it('returns pretty-printed JSON', () => {
    const messages: ChatMessage[] = [
      { role: 'user', content: 'Hello' },
      { role: 'assistant', content: 'Hi' },
    ]
    const result = exportSessionToJSON(messages)
    const parsed = JSON.parse(result) as ChatMessage[]
    expect(parsed).toHaveLength(2)
    expect(parsed[0]!.role).toBe('user')
    expect(parsed[1]!.content).toBe('Hi')
  })

  it('preserves structured content blocks', () => {
    const messages: ChatMessage[] = [
      {
        role: 'assistant',
        content: [{ type: 'text', text: 'test' }],
      },
    ]
    const result = exportSessionToJSON(messages)
    const parsed = JSON.parse(result) as ChatMessage[]
    expect(Array.isArray(parsed[0]!.content)).toBe(true)
  })

  it('handles empty messages', () => {
    const result = exportSessionToJSON([])
    expect(JSON.parse(result)).toEqual([])
  })
})
