import type { ChatMessage, ProviderConfig } from '@ava/core-v2/llm'
import { describe, expect, it } from 'vitest'
import { buildResponsesRequestBody } from './responses-body.js'

describe('buildResponsesRequestBody', () => {
  const baseConfig: ProviderConfig = {
    provider: 'openai',
    model: 'gpt-5',
  }

  it('builds basic request with user message', () => {
    const messages: ChatMessage[] = [{ role: 'user', content: 'Hello' }]

    const body = buildResponsesRequestBody(messages, baseConfig)

    expect(body.model).toBe('gpt-5')
    expect(body.stream).toBe(true)
    expect(body.store).toBe(false)
    expect(body.input).toHaveLength(1)
    expect(body.input[0]).toEqual({
      role: 'user',
      content: [{ type: 'input_text', text: 'Hello' }],
    })
  })

  it('extracts system messages into instructions', () => {
    const messages: ChatMessage[] = [
      { role: 'system', content: 'You are helpful.' },
      { role: 'user', content: 'Hi' },
    ]

    const body = buildResponsesRequestBody(messages, baseConfig)

    expect(body.instructions).toBe('You are helpful.')
    expect(body.input).toHaveLength(1)
    expect(body.input[0]?.role).toBe('user')
  })

  it('combines multiple system messages', () => {
    const messages: ChatMessage[] = [
      { role: 'system', content: 'Rule 1: be helpful.' },
      { role: 'system', content: 'Rule 2: be concise.' },
      { role: 'user', content: 'Hi' },
    ]

    const body = buildResponsesRequestBody(messages, baseConfig)

    expect(body.instructions).toContain('Rule 1')
    expect(body.instructions).toContain('Rule 2')
  })

  it('prepends external instructions to system messages', () => {
    const messages: ChatMessage[] = [
      { role: 'system', content: 'System prompt.' },
      { role: 'user', content: 'Hi' },
    ]

    const body = buildResponsesRequestBody(messages, baseConfig, 'Extra instructions')

    expect(body.instructions).toBe('Extra instructions\n\nSystem prompt.')
  })

  it('uses default instructions when no system messages', () => {
    const messages: ChatMessage[] = [{ role: 'user', content: 'Hi' }]

    const body = buildResponsesRequestBody(messages, baseConfig)

    expect(body.instructions).toBe('You are AVA, a coding assistant.')
  })

  it('maps assistant messages with output_text type', () => {
    const messages: ChatMessage[] = [
      { role: 'user', content: 'Hello' },
      { role: 'assistant', content: 'Hi there!' },
      { role: 'user', content: 'How are you?' },
    ]

    const body = buildResponsesRequestBody(messages, baseConfig)

    expect(body.input).toHaveLength(3)
    expect(body.input[1]).toEqual({
      role: 'assistant',
      content: [{ type: 'output_text', text: 'Hi there!' }],
    })
  })

  it('converts tools to flat Responses API format', () => {
    const config: ProviderConfig = {
      ...baseConfig,
      tools: [
        {
          name: 'read_file',
          description: 'Read a file',
          input_schema: {
            type: 'object',
            properties: { path: { type: 'string' } },
            required: ['path'],
          },
        },
      ],
    }

    const body = buildResponsesRequestBody([{ role: 'user', content: 'Read index.ts' }], config)

    expect(body.tools).toHaveLength(1)
    expect(body.tools![0]).toEqual({
      type: 'function',
      name: 'read_file',
      description: 'Read a file',
      parameters: {
        type: 'object',
        properties: { path: { type: 'string' } },
        required: ['path'],
      },
    })
  })

  it('omits tools when none configured', () => {
    const body = buildResponsesRequestBody([{ role: 'user', content: 'Hi' }], baseConfig)

    expect(body.tools).toBeUndefined()
  })

  it('includes maxTokens as max_output_tokens', () => {
    const config: ProviderConfig = { ...baseConfig, maxTokens: 8192 }

    const body = buildResponsesRequestBody([{ role: 'user', content: 'Hi' }], config)

    expect(body.max_output_tokens).toBe(8192)
  })

  it('includes temperature when set', () => {
    const config: ProviderConfig = { ...baseConfig, temperature: 0.7 }

    const body = buildResponsesRequestBody([{ role: 'user', content: 'Hi' }], config)

    expect(body.temperature).toBe(0.7)
  })

  it('handles content block arrays (extracts text blocks)', () => {
    const messages: ChatMessage[] = [
      {
        role: 'user',
        content: [
          { type: 'text', text: 'First part.' },
          { type: 'text', text: 'Second part.' },
        ],
      },
    ]

    const body = buildResponsesRequestBody(messages, baseConfig)

    expect(body.input).toHaveLength(1)
    expect(body.input[0]?.content[0]?.text).toBe('First part.\nSecond part.')
  })

  it('skips messages with empty text', () => {
    const messages: ChatMessage[] = [
      { role: 'user', content: '' },
      { role: 'user', content: 'Real message' },
    ]

    const body = buildResponsesRequestBody(messages, baseConfig)

    expect(body.input).toHaveLength(1)
    expect(body.input[0]?.content[0]?.text).toBe('Real message')
  })
})
