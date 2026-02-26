import { describe, expect, it } from 'vitest'
import {
  buildOpenAIRequestBody,
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
