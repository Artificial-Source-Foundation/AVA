import { describe, expect, it } from 'vitest'
import type { ProviderConfig } from '../../types/llm.js'
import {
  buildOpenAIRequestBody,
  convertToolsToOpenAIFormat,
  ToolCallBuffer,
} from './openai-compat.js'

describe('convertToolsToOpenAIFormat', () => {
  it('converts AVA tool definitions to OpenAI format', () => {
    const tools: ProviderConfig['tools'] = [
      {
        name: 'read_file',
        description: 'Read a file',
        input_schema: {
          type: 'object',
          properties: { path: { type: 'string' } },
          required: ['path'],
        },
      },
    ]

    const result = convertToolsToOpenAIFormat(tools)
    expect(result).toEqual([
      {
        type: 'function',
        function: {
          name: 'read_file',
          description: 'Read a file',
          parameters: {
            type: 'object',
            properties: { path: { type: 'string' } },
            required: ['path'],
          },
        },
      },
    ])
  })

  it('returns undefined for empty tools', () => {
    expect(convertToolsToOpenAIFormat([])).toBeUndefined()
  })

  it('returns undefined for undefined tools', () => {
    expect(convertToolsToOpenAIFormat(undefined)).toBeUndefined()
  })
})

describe('ToolCallBuffer', () => {
  it('accumulates tool call fragments', () => {
    const buffer = new ToolCallBuffer()

    // First fragment: id and name
    buffer.accumulate([{ index: 0, id: 'call_1', function: { name: 'read_file', arguments: '' } }])

    // Second fragment: arguments
    buffer.accumulate([{ index: 0, function: { arguments: '{"path":' } }])

    // Third fragment: rest of arguments
    buffer.accumulate([{ index: 0, function: { arguments: '"/test.ts"}' } }])

    const results = [...buffer.flush()]
    expect(results).toHaveLength(1)
    expect(results[0].toolUse).toEqual({
      type: 'tool_use',
      id: 'call_1',
      name: 'read_file',
      input: { path: '/test.ts' },
    })
  })

  it('handles multiple concurrent tool calls', () => {
    const buffer = new ToolCallBuffer()

    buffer.accumulate([
      { index: 0, id: 'call_1', function: { name: 'read_file', arguments: '{"path":"a.ts"}' } },
      { index: 1, id: 'call_2', function: { name: 'glob', arguments: '{"pattern":"*.ts"}' } },
    ])

    const results = [...buffer.flush()]
    expect(results).toHaveLength(2)
    expect(results[0].toolUse?.name).toBe('read_file')
    expect(results[1].toolUse?.name).toBe('glob')
  })

  it('skips tool calls without id', () => {
    const buffer = new ToolCallBuffer()
    buffer.accumulate([{ index: 0, function: { name: 'test', arguments: '{}' } }])

    const results = [...buffer.flush()]
    expect(results).toHaveLength(0)
  })

  it('skips tool calls without name', () => {
    const buffer = new ToolCallBuffer()
    buffer.accumulate([{ index: 0, id: 'call_1', function: { arguments: '{}' } }])

    const results = [...buffer.flush()]
    expect(results).toHaveLength(0)
  })

  it('skips tool calls with invalid JSON arguments', () => {
    const buffer = new ToolCallBuffer()
    buffer.accumulate([
      { index: 0, id: 'call_1', function: { name: 'test', arguments: 'not-json' } },
    ])

    const results = [...buffer.flush()]
    expect(results).toHaveLength(0)
  })

  it('defaults to empty object for missing arguments', () => {
    const buffer = new ToolCallBuffer()
    buffer.accumulate([{ index: 0, id: 'call_1', function: { name: 'test', arguments: '' } }])

    const results = [...buffer.flush()]
    expect(results).toHaveLength(1)
    expect(results[0].toolUse?.input).toEqual({})
  })
})

describe('buildOpenAIRequestBody', () => {
  const baseConfig: ProviderConfig = {
    provider: 'deepseek',
    model: 'deepseek-chat',
    authMethod: 'api-key',
  }

  it('builds basic request body', () => {
    const body = buildOpenAIRequestBody([{ role: 'user', content: 'hello' }], baseConfig, {
      model: 'default-model',
    })

    expect(body.model).toBe('deepseek-chat')
    expect(body.stream).toBe(true)
    expect(body.messages).toEqual([{ role: 'user', content: 'hello' }])
  })

  it('uses default model when config model is missing', () => {
    const config = { ...baseConfig, model: undefined as unknown as string }
    const body = buildOpenAIRequestBody([{ role: 'user', content: 'hi' }], config, {
      model: 'fallback-model',
    })
    expect(body.model).toBe('fallback-model')
  })

  it('includes maxTokens when set', () => {
    const config = { ...baseConfig, maxTokens: 4096 }
    const body = buildOpenAIRequestBody([], config, { model: 'm' })
    expect(body.max_tokens).toBe(4096)
  })

  it('includes temperature when set', () => {
    const config = { ...baseConfig, temperature: 0.7 }
    const body = buildOpenAIRequestBody([], config, { model: 'm' })
    expect(body.temperature).toBe(0.7)
  })

  it('includes temperature when set to 0', () => {
    const config = { ...baseConfig, temperature: 0 }
    const body = buildOpenAIRequestBody([], config, { model: 'm' })
    expect(body.temperature).toBe(0)
  })

  it('includes tools when provided', () => {
    const config: ProviderConfig = {
      ...baseConfig,
      tools: [
        {
          name: 'test',
          description: 'A test tool',
          input_schema: { type: 'object', properties: {} },
        },
      ],
    }
    const body = buildOpenAIRequestBody([], config, { model: 'm' })
    expect(body.tools).toBeDefined()
    expect(body.tools as unknown[]).toHaveLength(1)
  })

  it('omits tools when empty', () => {
    const config = { ...baseConfig, tools: [] }
    const body = buildOpenAIRequestBody([], config, { model: 'm' })
    expect(body.tools).toBeUndefined()
  })
})
