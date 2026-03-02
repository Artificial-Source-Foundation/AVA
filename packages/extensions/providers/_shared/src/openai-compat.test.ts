import type { ChatMessage } from '@ava/core-v2/llm'
import { describe, expect, it } from 'vitest'
import {
  buildOpenAIRequestBody,
  convertMessagesToOpenAI,
  convertToolsToOpenAIFormat,
  createOpenAICompatClient,
  ToolCallBuffer,
} from './openai-compat.js'

describe('convertToolsToOpenAIFormat', () => {
  it('converts AVA tools to OpenAI format', () => {
    const tools = [
      {
        name: 'read_file',
        description: 'Read a file',
        input_schema: {
          type: 'object' as const,
          properties: { path: { type: 'string' } },
        },
      },
    ]
    const result = convertToolsToOpenAIFormat(tools)
    expect(result).toHaveLength(1)
    expect(result![0]!.type).toBe('function')
    expect(result![0]!.function.name).toBe('read_file')
  })

  it('returns undefined for empty tools', () => {
    expect(convertToolsToOpenAIFormat([])).toBeUndefined()
    expect(convertToolsToOpenAIFormat(undefined)).toBeUndefined()
  })
})

describe('buildOpenAIRequestBody', () => {
  it('builds basic request body', () => {
    const messages = [{ role: 'user' as const, content: 'Hello' }]
    const config = { provider: 'openai' as const, model: 'gpt-4o' }
    const body = buildOpenAIRequestBody(messages, config, { model: 'gpt-4o' })

    expect(body.model).toBe('gpt-4o')
    expect(body.stream).toBe(true)
    expect(body.messages).toEqual([{ role: 'user', content: 'Hello' }])
  })

  it('includes optional parameters', () => {
    const messages = [{ role: 'user' as const, content: 'Hello' }]
    const config = {
      provider: 'openai' as const,
      model: 'gpt-4o',
      maxTokens: 2048,
      temperature: 0.5,
    }
    const body = buildOpenAIRequestBody(messages, config, { model: 'gpt-4o' })

    expect(body.max_tokens).toBe(2048)
    expect(body.temperature).toBe(0.5)
  })

  it('uses config model over default', () => {
    const messages = [{ role: 'user' as const, content: 'Hello' }]
    const config = { provider: 'openai' as const, model: 'gpt-4o-mini' }
    const body = buildOpenAIRequestBody(messages, config, { model: 'gpt-4o' })

    expect(body.model).toBe('gpt-4o-mini')
  })
})

describe('ToolCallBuffer', () => {
  it('accumulates tool call fragments', () => {
    const buf = new ToolCallBuffer()

    buf.accumulate([{ index: 0, id: 'call_1', function: { name: 'read_file' } }])
    buf.accumulate([{ index: 0, function: { arguments: '{"path":' } }])
    buf.accumulate([{ index: 0, function: { arguments: '"/test.ts"}' } }])

    const results = [...buf.flush()]
    expect(results).toHaveLength(1)
    expect(results[0]!.toolUse?.name).toBe('read_file')
    expect(results[0]!.toolUse?.input).toEqual({ path: '/test.ts' })
  })

  it('handles multiple concurrent tool calls', () => {
    const buf = new ToolCallBuffer()

    buf.accumulate([
      { index: 0, id: 'call_1', function: { name: 'read_file', arguments: '{"path":"a.ts"}' } },
      { index: 1, id: 'call_2', function: { name: 'glob', arguments: '{"pattern":"*.ts"}' } },
    ])

    const results = [...buf.flush()]
    expect(results).toHaveLength(2)
    expect(results[0]!.toolUse?.name).toBe('read_file')
    expect(results[1]!.toolUse?.name).toBe('glob')
  })

  it('skips incomplete tool calls', () => {
    const buf = new ToolCallBuffer()
    buf.accumulate([{ index: 0, function: { arguments: '{invalid' } }])
    const results = [...buf.flush()]
    expect(results).toHaveLength(0) // No id or name
  })

  it('handles invalid JSON arguments', () => {
    const buf = new ToolCallBuffer()
    buf.accumulate([{ index: 0, id: 'call_1', function: { name: 'test', arguments: '{bad json' } }])
    const results = [...buf.flush()]
    expect(results).toHaveLength(0) // JSON parse fails
  })
})

describe('convertMessagesToOpenAI', () => {
  it('passes plain string messages through', () => {
    const messages: ChatMessage[] = [
      { role: 'system', content: 'You are AVA.' },
      { role: 'user', content: 'Hello' },
      { role: 'assistant', content: 'Hi there!' },
    ]
    const result = convertMessagesToOpenAI(messages)
    expect(result).toEqual([
      { role: 'system', content: 'You are AVA.' },
      { role: 'user', content: 'Hello' },
      { role: 'assistant', content: 'Hi there!' },
    ])
  })

  it('converts assistant tool_use blocks to OpenAI tool_calls format', () => {
    const messages: ChatMessage[] = [
      {
        role: 'assistant',
        content: [
          { type: 'text', text: 'Let me read that file' },
          {
            type: 'tool_use',
            id: 'tc-1',
            name: 'read_file',
            input: { path: '/test.txt' },
          },
        ],
      },
    ]
    const result = convertMessagesToOpenAI(messages)
    expect(result).toHaveLength(1)
    expect(result[0]!.role).toBe('assistant')
    expect(result[0]!.content).toBe('Let me read that file')
    expect(result[0]!.tool_calls).toEqual([
      {
        id: 'tc-1',
        type: 'function',
        function: { name: 'read_file', arguments: '{"path":"/test.txt"}' },
      },
    ])
  })

  it('converts user tool_result blocks to role:tool messages', () => {
    const messages: ChatMessage[] = [
      {
        role: 'user',
        content: [
          {
            type: 'tool_result',
            tool_use_id: 'tc-1',
            content: 'file contents here',
            is_error: false,
          },
        ],
      },
    ]
    const result = convertMessagesToOpenAI(messages)
    expect(result).toHaveLength(1)
    expect(result[0]).toEqual({
      role: 'tool',
      content: 'file contents here',
      tool_call_id: 'tc-1',
    })
  })

  it('handles assistant with only tool_use blocks (no text)', () => {
    const messages: ChatMessage[] = [
      {
        role: 'assistant',
        content: [{ type: 'tool_use', id: 'tc-1', name: 'glob', input: { pattern: '*.ts' } }],
      },
    ]
    const result = convertMessagesToOpenAI(messages)
    expect(result[0]!.content).toBeNull()
    expect(result[0]!.tool_calls).toHaveLength(1)
  })

  it('handles multiple tool results in one user message', () => {
    const messages: ChatMessage[] = [
      {
        role: 'user',
        content: [
          { type: 'tool_result', tool_use_id: 'tc-1', content: 'result1' },
          { type: 'tool_result', tool_use_id: 'tc-2', content: 'result2' },
        ],
      },
    ]
    const result = convertMessagesToOpenAI(messages)
    expect(result).toHaveLength(2)
    expect(result[0]!.tool_call_id).toBe('tc-1')
    expect(result[1]!.tool_call_id).toBe('tc-2')
  })

  it('converts base64 image blocks to OpenAI image_url format', () => {
    const messages: ChatMessage[] = [
      {
        role: 'user',
        content: [
          { type: 'text', text: 'What is in this image?' },
          {
            type: 'image',
            source: {
              type: 'base64',
              media_type: 'image/png',
              data: 'iVBORw0KGgoAAAANSUhEUg==',
            },
          },
        ],
      },
    ]
    const result = convertMessagesToOpenAI(messages)
    expect(result).toHaveLength(1)
    expect(result[0]!.role).toBe('user')

    const content = result[0]!.content as Array<Record<string, unknown>>
    expect(content).toHaveLength(2)
    expect(content[0]).toEqual({ type: 'text', text: 'What is in this image?' })
    expect(content[1]).toEqual({
      type: 'image_url',
      image_url: { url: 'data:image/png;base64,iVBORw0KGgoAAAANSUhEUg==' },
    })
  })

  it('converts url image blocks to OpenAI image_url format', () => {
    const messages: ChatMessage[] = [
      {
        role: 'user',
        content: [
          {
            type: 'image',
            source: {
              type: 'url',
              media_type: 'image/jpeg',
              data: 'https://example.com/photo.jpg',
            },
          },
          { type: 'text', text: 'Describe this' },
        ],
      },
    ]
    const result = convertMessagesToOpenAI(messages)
    expect(result).toHaveLength(1)

    const content = result[0]!.content as Array<Record<string, unknown>>
    expect(content).toHaveLength(2)
    expect(content[0]).toEqual({ type: 'text', text: 'Describe this' })
    expect(content[1]).toEqual({
      type: 'image_url',
      image_url: { url: 'https://example.com/photo.jpg' },
    })
  })

  it('keeps plain string format when user message has only text blocks', () => {
    const messages: ChatMessage[] = [
      {
        role: 'user',
        content: [
          { type: 'text', text: 'Hello' },
          { type: 'text', text: 'World' },
        ],
      },
    ]
    const result = convertMessagesToOpenAI(messages)
    expect(result).toHaveLength(1)
    expect(result[0]!.content).toBe('Hello\nWorld')
  })

  it('handles user message with tool results and image blocks', () => {
    const messages: ChatMessage[] = [
      {
        role: 'user',
        content: [
          { type: 'tool_result', tool_use_id: 'tc-1', content: 'done' },
          { type: 'text', text: 'Now analyze this screenshot' },
          {
            type: 'image',
            source: {
              type: 'base64',
              media_type: 'image/webp',
              data: 'UklGR...',
            },
          },
        ],
      },
    ]
    const result = convertMessagesToOpenAI(messages)
    // tool result becomes separate message, then user message with text+image
    expect(result).toHaveLength(2)
    expect(result[0]!.role).toBe('tool')
    expect(result[0]!.tool_call_id).toBe('tc-1')
    expect(result[1]!.role).toBe('user')

    const content = result[1]!.content as Array<Record<string, unknown>>
    expect(content).toHaveLength(2)
    expect(content[0]).toEqual({ type: 'text', text: 'Now analyze this screenshot' })
    expect(content[1]).toEqual({
      type: 'image_url',
      image_url: { url: 'data:image/webp;base64,UklGR...' },
    })
  })
})

describe('createOpenAICompatClient', () => {
  it('creates a client class', () => {
    const Client = createOpenAICompatClient({
      provider: 'deepseek',
      displayName: 'DeepSeek',
      baseUrl: 'https://api.deepseek.com/v1',
      defaultModel: 'deepseek-chat',
      apiKeyHint: 'AVA_DEEPSEEK_API_KEY',
    })

    expect(Client).toBeDefined()
    const instance = new Client()
    expect(typeof instance.stream).toBe('function')
  })
})
