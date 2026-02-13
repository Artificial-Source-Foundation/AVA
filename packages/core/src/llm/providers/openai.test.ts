import { beforeEach, describe, expect, it, vi } from 'vitest'
import type { ProviderConfig } from '../../types/llm.js'

const mockGetAuth = vi.fn()
const mockGetAccountId = vi.fn()
let RegisteredClient:
  | (new () => { stream: (...args: unknown[]) => AsyncGenerator<unknown> })
  | null = null

vi.mock('../client.js', () => ({
  getAuth: (...args: unknown[]) => mockGetAuth(...args),
  registerClient: (
    _provider: string,
    clientClass: new () => { stream: (...args: unknown[]) => AsyncGenerator<unknown> }
  ) => {
    RegisteredClient = clientClass
  },
}))

vi.mock('../../auth/manager.js', () => ({
  getAccountId: (...args: unknown[]) => mockGetAccountId(...args),
}))

describe('OpenAIClient OAuth transport behavior', () => {
  beforeEach(async () => {
    vi.clearAllMocks()
    if (!RegisteredClient) {
      await import('./openai.js')
    }
  })

  it('does not fallback to API-key endpoint on OAuth network errors', async () => {
    mockGetAuth.mockResolvedValue({
      type: 'oauth',
      token: 'oauth-token',
      accountId: 'acct-1',
    })
    mockGetAccountId.mockResolvedValue('acct-1')

    const fetchMock = vi.fn().mockRejectedValueOnce(new TypeError('Load failed'))

    vi.stubGlobal('fetch', fetchMock)

    const client = new RegisteredClient!()
    const config: ProviderConfig = {
      provider: 'openai',
      model: 'gpt-5.2',
      authMethod: 'oauth',
    }
    const messages = [
      { role: 'user' as const, content: 'hello' },
      { role: 'assistant' as const, content: 'Hi there' },
    ]

    const deltas: unknown[] = []
    for await (const delta of client.stream(messages, config)) {
      deltas.push(delta)
    }

    expect(fetchMock).toHaveBeenCalledTimes(1)
    expect(fetchMock.mock.calls[0]?.[0]).toBe('https://chatgpt.com/backend-api/codex/responses')
    expect(deltas[0]).toMatchObject({
      done: true,
      error: { type: 'network' },
    })

    vi.unstubAllGlobals()
  })

  it('sends instructions field for OAuth codex requests', async () => {
    mockGetAuth.mockResolvedValue({
      type: 'oauth',
      token: 'oauth-token',
      accountId: 'acct-1',
    })
    mockGetAccountId.mockResolvedValue('acct-1')

    const fetchMock = vi.fn().mockResolvedValueOnce(
      new Response(JSON.stringify({ detail: 'Instructions are required' }), {
        status: 400,
        headers: { 'content-type': 'application/json' },
      })
    )

    vi.stubGlobal('fetch', fetchMock)

    const client = new RegisteredClient!()
    const config: ProviderConfig = {
      provider: 'openai',
      model: 'gpt-5.2',
      authMethod: 'oauth',
    }
    const messages = [
      { role: 'user' as const, content: 'hello' },
      { role: 'assistant' as const, content: 'Hi there' },
    ]

    for await (const _delta of client.stream(messages, config)) {
      // consume
    }

    const requestInit = fetchMock.mock.calls[0]?.[1] as RequestInit | undefined
    const requestBody = JSON.parse(String(requestInit?.body ?? '{}')) as {
      instructions?: string
      store?: boolean
      stream?: boolean
      max_tokens?: number
      max_output_tokens?: number
      input?: Array<{
        role?: string
        content?: Array<{ type?: string; text?: string }>
      }>
    }

    expect(requestBody.instructions).toBeTypeOf('string')
    expect(requestBody.instructions?.length).toBeGreaterThan(0)
    expect(requestBody.store).toBe(false)
    expect(requestBody.stream).toBe(true)
    expect(requestBody.max_tokens).toBeUndefined()
    expect(requestBody.max_output_tokens).toBeUndefined()
    expect(requestBody.input?.[0]?.content?.[0]?.type).toBe('input_text')
    expect(requestBody.input?.[1]?.content?.[0]?.type).toBe('output_text')

    vi.unstubAllGlobals()
  })
})
