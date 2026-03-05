import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'
import { fetchWellKnownConfig } from './well-known.js'

describe('fetchWellKnownConfig', () => {
  const originalFetch = globalThis.fetch

  beforeEach(() => {
    vi.useFakeTimers({ shouldAdvanceTime: true })
  })

  afterEach(() => {
    globalThis.fetch = originalFetch
    vi.useRealTimers()
  })

  it('fetches and returns valid config', async () => {
    const config = { name: 'Acme Corp', provider: 'openai', model: 'gpt-4o' }
    globalThis.fetch = vi.fn().mockResolvedValue({
      ok: true,
      headers: new Headers({ 'content-type': 'application/json' }),
      json: () => Promise.resolve(config),
    })

    const result = await fetchWellKnownConfig('acme.com')

    expect(result).toEqual(config)
    expect(globalThis.fetch).toHaveBeenCalledWith(
      'https://acme.com/.well-known/ava',
      expect.objectContaining({ method: 'GET' })
    )
  })

  it('returns config with mcpServers', async () => {
    const config = {
      name: 'Dev Corp',
      mcpServers: [{ name: 'internal', url: 'https://mcp.dev.corp/sse' }],
    }
    globalThis.fetch = vi.fn().mockResolvedValue({
      ok: true,
      headers: new Headers({ 'content-type': 'application/json; charset=utf-8' }),
      json: () => Promise.resolve(config),
    })

    const result = await fetchWellKnownConfig('dev.corp')
    expect(result?.mcpServers).toHaveLength(1)
    expect(result?.mcpServers?.[0]?.name).toBe('internal')
  })

  it('returns null on HTTP error', async () => {
    globalThis.fetch = vi.fn().mockResolvedValue({
      ok: false,
      status: 404,
      headers: new Headers(),
    })

    const result = await fetchWellKnownConfig('missing.com')
    expect(result).toBeNull()
  })

  it('returns null on non-JSON content-type', async () => {
    globalThis.fetch = vi.fn().mockResolvedValue({
      ok: true,
      headers: new Headers({ 'content-type': 'text/html' }),
      json: () => Promise.resolve({}),
    })

    const result = await fetchWellKnownConfig('html-only.com')
    expect(result).toBeNull()
  })

  it('returns null on network error', async () => {
    globalThis.fetch = vi.fn().mockRejectedValue(new Error('DNS resolution failed'))

    const result = await fetchWellKnownConfig('no-dns.invalid')
    expect(result).toBeNull()
  })

  it('returns null on invalid JSON body', async () => {
    globalThis.fetch = vi.fn().mockResolvedValue({
      ok: true,
      headers: new Headers({ 'content-type': 'application/json' }),
      json: () => Promise.reject(new SyntaxError('Unexpected token')),
    })

    const result = await fetchWellKnownConfig('bad-json.com')
    expect(result).toBeNull()
  })

  it('returns null when body is not an object', async () => {
    globalThis.fetch = vi.fn().mockResolvedValue({
      ok: true,
      headers: new Headers({ 'content-type': 'application/json' }),
      json: () => Promise.resolve('just a string'),
    })

    const result = await fetchWellKnownConfig('string-body.com')
    expect(result).toBeNull()
  })

  it('returns null when body is null', async () => {
    globalThis.fetch = vi.fn().mockResolvedValue({
      ok: true,
      headers: new Headers({ 'content-type': 'application/json' }),
      json: () => Promise.resolve(null),
    })

    const result = await fetchWellKnownConfig('null-body.com')
    expect(result).toBeNull()
  })

  it('respects caller abort signal', async () => {
    const callerController = new AbortController()
    callerController.abort()

    globalThis.fetch = vi.fn().mockRejectedValue(new DOMException('Aborted', 'AbortError'))

    const result = await fetchWellKnownConfig('slow.com', callerController.signal)
    expect(result).toBeNull()
  })

  it('times out after 5 seconds', async () => {
    globalThis.fetch = vi.fn().mockImplementation(
      (_url: string, init: { signal: AbortSignal }) =>
        new Promise((_resolve, reject) => {
          init.signal.addEventListener('abort', () => {
            reject(new DOMException('Aborted', 'AbortError'))
          })
        })
    )

    const promise = fetchWellKnownConfig('timeout.com')
    await vi.advanceTimersByTimeAsync(5001)
    const result = await promise
    expect(result).toBeNull()
  })
})
