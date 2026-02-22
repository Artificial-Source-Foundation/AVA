/**
 * Tests for OpenRouter provider
 * Verifies tool calling, auth, error handling, and custom headers
 */

import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'

// Stable mock credentials object
const mockCredentials = { get: vi.fn().mockResolvedValue(null) }

// Mock the platform for getApiKey (used by getAuth fallback)
vi.mock('../../platform.js', () => ({
  getPlatform: () => ({
    credentials: mockCredentials,
  }),
}))

// Mock auth module — getAuth checks OAuth first, then falls back to getApiKey
vi.mock('../../auth/index.js', () => ({
  getStoredAuth: vi.fn().mockResolvedValue(null),
  getValidAccessToken: vi.fn().mockResolvedValue(null),
  getAccountId: vi.fn().mockResolvedValue(null),
}))

import type { ProviderConfig, StreamDelta } from '../../types/llm.js'
import { createClient } from '../client.js'

// Helper to collect all deltas from a stream
async function collectStream(config: Partial<ProviderConfig> = {}): Promise<StreamDelta[]> {
  const client = await createClient('openrouter')
  const deltas: StreamDelta[] = []
  const fullConfig: ProviderConfig = {
    provider: 'openrouter',
    model: 'google/gemini-3-flash-preview',
    authMethod: 'api-key',
    ...config,
  }
  for await (const delta of client.stream([{ role: 'user', content: 'hi' }], fullConfig)) {
    deltas.push(delta)
  }
  return deltas
}

// Helper to create SSE response body
function sseBody(events: string[]): ReadableStream<Uint8Array> {
  const encoder = new TextEncoder()
  const text = `${events.map((e) => `data: ${e}\n\n`).join('')}data: [DONE]\n\n`
  return new ReadableStream({
    start(controller) {
      controller.enqueue(encoder.encode(text))
      controller.close()
    },
  })
}

describe('OpenRouter provider', () => {
  const originalFetch = globalThis.fetch

  beforeEach(() => {
    vi.clearAllMocks()
    mockCredentials.get.mockResolvedValue(null)
  })

  afterEach(() => {
    globalThis.fetch = originalFetch
  })

  it('yields auth error when no API key', async () => {
    const deltas = await collectStream()
    expect(deltas).toHaveLength(1)
    expect(deltas[0].error?.type).toBe('auth')
    expect(deltas[0].error?.message).toContain('AVA_OPENROUTER_API_KEY')
  })

  it('yields rate_limit error on 429', async () => {
    mockCredentials.get.mockResolvedValue('test-key')

    globalThis.fetch = vi.fn().mockResolvedValue(
      new Response('{"error":{"message":"rate limited"}}', {
        status: 429,
        headers: { 'retry-after': '30' },
      })
    )

    const deltas = await collectStream()
    expect(deltas).toHaveLength(1)
    expect(deltas[0].error?.type).toBe('rate_limit')
    expect(deltas[0].error?.retryAfter).toBe(30)
  })

  it('yields auth error on 401', async () => {
    mockCredentials.get.mockResolvedValue('bad-key')

    globalThis.fetch = vi
      .fn()
      .mockResolvedValue(new Response('{"error":{"message":"Invalid API key"}}', { status: 401 }))

    const deltas = await collectStream()
    expect(deltas).toHaveLength(1)
    expect(deltas[0].error?.type).toBe('auth')
  })

  it('yields server error on 500', async () => {
    mockCredentials.get.mockResolvedValue('test-key')

    globalThis.fetch = vi
      .fn()
      .mockResolvedValue(new Response('Internal Server Error', { status: 500 }))

    const deltas = await collectStream()
    expect(deltas).toHaveLength(1)
    expect(deltas[0].error?.type).toBe('server')
  })

  it('streams content successfully', async () => {
    mockCredentials.get.mockResolvedValue('test-key')

    const event = JSON.stringify({
      id: 'test',
      object: 'chat.completion.chunk',
      created: Date.now(),
      model: 'google/gemini-3-flash-preview',
      choices: [{ index: 0, delta: { content: 'Hello world' }, finish_reason: null }],
    })

    globalThis.fetch = vi.fn().mockResolvedValue(new Response(sseBody([event]), { status: 200 }))

    const deltas = await collectStream()
    const textDeltas = deltas.filter((d) => d.content)
    expect(textDeltas).toHaveLength(1)
    expect(textDeltas[0].content).toBe('Hello world')
    expect(deltas[deltas.length - 1].done).toBe(true)
  })

  it('handles streaming tool calls', async () => {
    mockCredentials.get.mockResolvedValue('test-key')

    const events = [
      JSON.stringify({
        id: 'test',
        object: 'chat.completion.chunk',
        created: Date.now(),
        model: 'google/gemini-3-flash-preview',
        choices: [
          {
            index: 0,
            delta: {
              tool_calls: [
                {
                  index: 0,
                  id: 'call_1',
                  type: 'function',
                  function: { name: 'read_file', arguments: '{"path":"/test.ts"}' },
                },
              ],
            },
            finish_reason: null,
          },
        ],
      }),
    ]

    globalThis.fetch = vi.fn().mockResolvedValue(new Response(sseBody(events), { status: 200 }))

    const deltas = await collectStream()
    const toolDeltas = deltas.filter((d) => d.toolUse)
    expect(toolDeltas).toHaveLength(1)
    expect(toolDeltas[0].toolUse?.name).toBe('read_file')
    expect(toolDeltas[0].toolUse?.input).toEqual({ path: '/test.ts' })
  })

  it('accumulates multi-chunk tool call arguments', async () => {
    mockCredentials.get.mockResolvedValue('test-key')

    const events = [
      // First chunk: tool call start with partial arguments
      JSON.stringify({
        id: 'test',
        object: 'chat.completion.chunk',
        created: Date.now(),
        model: 'test-model',
        choices: [
          {
            index: 0,
            delta: {
              tool_calls: [
                {
                  index: 0,
                  id: 'call_2',
                  type: 'function',
                  function: { name: 'edit', arguments: '{"file":' },
                },
              ],
            },
            finish_reason: null,
          },
        ],
      }),
      // Second chunk: rest of arguments
      JSON.stringify({
        id: 'test',
        object: 'chat.completion.chunk',
        created: Date.now(),
        model: 'test-model',
        choices: [
          {
            index: 0,
            delta: {
              tool_calls: [
                {
                  index: 0,
                  function: { arguments: '"/app.ts"}' },
                },
              ],
            },
            finish_reason: null,
          },
        ],
      }),
    ]

    globalThis.fetch = vi.fn().mockResolvedValue(new Response(sseBody(events), { status: 200 }))

    const deltas = await collectStream()
    const toolDeltas = deltas.filter((d) => d.toolUse)
    expect(toolDeltas).toHaveLength(1)
    expect(toolDeltas[0].toolUse?.name).toBe('edit')
    expect(toolDeltas[0].toolUse?.input).toEqual({ file: '/app.ts' })
  })

  it('includes tools in request body when provided', async () => {
    mockCredentials.get.mockResolvedValue('test-key')

    const event = JSON.stringify({
      id: 'test',
      object: 'chat.completion.chunk',
      created: Date.now(),
      model: 'test-model',
      choices: [{ index: 0, delta: { content: 'ok' }, finish_reason: 'stop' }],
    })

    globalThis.fetch = vi.fn().mockResolvedValue(new Response(sseBody([event]), { status: 200 }))

    const tools = [
      {
        name: 'read_file',
        description: 'Read a file',
        input_schema: { type: 'object', properties: { path: { type: 'string' } } },
      },
    ]

    await collectStream({ tools })

    const fetchCall = (globalThis.fetch as ReturnType<typeof vi.fn>).mock.calls[0]
    const body = JSON.parse(fetchCall[1].body)
    expect(body.tools).toBeDefined()
    expect(body.tools).toHaveLength(1)
    expect(body.tools[0].type).toBe('function')
    expect(body.tools[0].function.name).toBe('read_file')
  })

  it('sends OpenRouter-specific headers', async () => {
    mockCredentials.get.mockResolvedValue('test-key')

    const event = JSON.stringify({
      id: 'test',
      object: 'chat.completion.chunk',
      created: Date.now(),
      model: 'test-model',
      choices: [{ index: 0, delta: { content: 'ok' }, finish_reason: 'stop' }],
    })

    globalThis.fetch = vi.fn().mockResolvedValue(new Response(sseBody([event]), { status: 200 }))

    await collectStream()

    const fetchCall = (globalThis.fetch as ReturnType<typeof vi.fn>).mock.calls[0]
    const headers = fetchCall[1].headers
    expect(headers['HTTP-Referer']).toBe('https://ava.app')
    expect(headers['X-Title']).toBe('AVA')
    expect(headers['Authorization']).toBe('Bearer test-key')
  })

  it('handles network error', async () => {
    mockCredentials.get.mockResolvedValue('test-key')

    globalThis.fetch = vi.fn().mockRejectedValue(new Error('ECONNREFUSED'))

    const deltas = await collectStream()
    expect(deltas).toHaveLength(1)
    expect(deltas[0].error?.type).toBe('network')
    expect(deltas[0].error?.message).toContain('ECONNREFUSED')
  })

  it('extracts usage from final event', async () => {
    mockCredentials.get.mockResolvedValue('test-key')

    const events = [
      JSON.stringify({
        id: 'test',
        object: 'chat.completion.chunk',
        created: Date.now(),
        model: 'test-model',
        choices: [{ index: 0, delta: { content: 'Hi' }, finish_reason: null }],
      }),
      JSON.stringify({
        id: 'test',
        object: 'chat.completion.chunk',
        created: Date.now(),
        model: 'test-model',
        choices: [{ index: 0, delta: {}, finish_reason: 'stop' }],
        usage: { prompt_tokens: 10, completion_tokens: 5, total_tokens: 15 },
      }),
    ]

    globalThis.fetch = vi.fn().mockResolvedValue(new Response(sseBody(events), { status: 200 }))

    const deltas = await collectStream()
    const usageDeltas = deltas.filter((d) => d.usage)
    expect(usageDeltas).toHaveLength(1)
    expect(usageDeltas[0].usage?.inputTokens).toBe(10)
    expect(usageDeltas[0].usage?.outputTokens).toBe(5)
  })
})
