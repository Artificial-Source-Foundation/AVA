/**
 * Smoke tests for OpenAI-compatible providers
 * Tests: DeepSeek, xAI, Together, Groq, Mistral, Cohere
 */

import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'

// Stable mock credentials object
const mockCredentials = { get: vi.fn().mockResolvedValue(null) }

// Mock the platform for getApiKey
vi.mock('../../platform.js', () => ({
  getPlatform: () => ({
    credentials: mockCredentials,
  }),
}))

// Mock auth module to avoid OAuth lookups
vi.mock('../../auth/index.js', () => ({
  getStoredAuth: vi.fn().mockResolvedValue(null),
  getValidAccessToken: vi.fn().mockResolvedValue(null),
  getAccountId: vi.fn().mockResolvedValue(null),
}))

import type { ProviderConfig, StreamDelta } from '../../types/llm.js'
import { createClient } from '../client.js'

// Helper to collect all deltas from a stream
async function collectStream(
  provider: string,
  config: Partial<ProviderConfig> = {}
): Promise<StreamDelta[]> {
  const client = await createClient(provider as ProviderConfig['provider'])
  const deltas: StreamDelta[] = []
  const fullConfig: ProviderConfig = {
    provider: provider as ProviderConfig['provider'],
    model: 'test-model',
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

describe.each([
  { provider: 'deepseek', display: 'DeepSeek', envHint: 'AVA_DEEPSEEK_API_KEY' },
  { provider: 'xai', display: 'xAI', envHint: 'AVA_XAI_API_KEY' },
  { provider: 'together', display: 'Together AI', envHint: 'AVA_TOGETHER_API_KEY' },
  { provider: 'groq', display: 'Groq', envHint: 'AVA_GROQ_API_KEY' },
  { provider: 'mistral', display: 'Mistral', envHint: 'AVA_MISTRAL_API_KEY' },
])('$display provider', ({ provider, envHint }) => {
  const originalFetch = globalThis.fetch

  beforeEach(() => {
    vi.clearAllMocks()
    mockCredentials.get.mockResolvedValue(null)
  })

  afterEach(() => {
    globalThis.fetch = originalFetch
  })

  it('yields auth error when no API key', async () => {
    const deltas = await collectStream(provider)
    expect(deltas).toHaveLength(1)
    expect(deltas[0].error?.type).toBe('auth')
    expect(deltas[0].error?.message).toContain(envHint)
  })

  it('yields rate_limit error on 429', async () => {
    mockCredentials.get.mockResolvedValue('test-key')

    globalThis.fetch = vi.fn().mockResolvedValue(
      new Response('{"error":{"message":"rate limited"}}', {
        status: 429,
        headers: { 'retry-after': '30' },
      })
    )

    const deltas = await collectStream(provider)
    expect(deltas).toHaveLength(1)
    expect(deltas[0].error?.type).toBe('rate_limit')
    expect(deltas[0].error?.retryAfter).toBe(30)
  })

  it('streams content successfully', async () => {
    mockCredentials.get.mockResolvedValue('test-key')

    const event = JSON.stringify({
      id: 'test',
      object: 'chat.completion.chunk',
      created: Date.now(),
      model: 'test-model',
      choices: [{ index: 0, delta: { content: 'Hello world' }, finish_reason: null }],
    })

    globalThis.fetch = vi.fn().mockResolvedValue(new Response(sseBody([event]), { status: 200 }))

    const deltas = await collectStream(provider)
    const textDeltas = deltas.filter((d) => d.content)
    expect(textDeltas).toHaveLength(1)
    expect(textDeltas[0].content).toBe('Hello world')
    expect(deltas[deltas.length - 1].done).toBe(true)
  })

  it('handles network error', async () => {
    mockCredentials.get.mockResolvedValue('test-key')

    globalThis.fetch = vi.fn().mockRejectedValue(new Error('ECONNREFUSED'))

    const deltas = await collectStream(provider)
    expect(deltas).toHaveLength(1)
    expect(deltas[0].error?.type).toBe('network')
    expect(deltas[0].error?.message).toContain('ECONNREFUSED')
  })

  it('handles streaming tool calls', async () => {
    mockCredentials.get.mockResolvedValue('test-key')

    const events = [
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

    const deltas = await collectStream(provider)
    const toolDeltas = deltas.filter((d) => d.toolUse)
    expect(toolDeltas).toHaveLength(1)
    expect(toolDeltas[0].toolUse?.name).toBe('read_file')
    expect(toolDeltas[0].toolUse?.input).toEqual({ path: '/test.ts' })
  })
})

describe('Cohere provider', () => {
  const originalFetch = globalThis.fetch

  beforeEach(() => {
    vi.clearAllMocks()
    mockCredentials.get.mockResolvedValue(null)
  })

  afterEach(() => {
    globalThis.fetch = originalFetch
  })

  it('yields auth error when no API key', async () => {
    const deltas = await collectStream('cohere')
    expect(deltas).toHaveLength(1)
    expect(deltas[0].error?.type).toBe('auth')
    expect(deltas[0].error?.message).toContain('AVA_COHERE_API_KEY')
  })

  it('yields rate_limit error on 429', async () => {
    mockCredentials.get.mockResolvedValue('test-key')

    globalThis.fetch = vi.fn().mockResolvedValue(new Response('rate limited', { status: 429 }))

    const deltas = await collectStream('cohere')
    expect(deltas).toHaveLength(1)
    expect(deltas[0].error?.type).toBe('rate_limit')
  })

  it('streams content-delta events', async () => {
    mockCredentials.get.mockResolvedValue('test-key')

    const event = JSON.stringify({
      type: 'content-delta',
      delta: { message: { content: { text: 'Hello from Cohere' } } },
    })

    globalThis.fetch = vi.fn().mockResolvedValue(new Response(sseBody([event]), { status: 200 }))

    const deltas = await collectStream('cohere')
    const textDeltas = deltas.filter((d) => d.content)
    expect(textDeltas).toHaveLength(1)
    expect(textDeltas[0].content).toBe('Hello from Cohere')
  })

  it('handles network error', async () => {
    mockCredentials.get.mockResolvedValue('test-key')

    globalThis.fetch = vi.fn().mockRejectedValue(new Error('Connection failed'))

    const deltas = await collectStream('cohere')
    expect(deltas).toHaveLength(1)
    expect(deltas[0].error?.type).toBe('network')
  })
})
