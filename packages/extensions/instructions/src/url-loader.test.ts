import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'
import { fetchUrlInstruction } from './url-loader.js'

describe('fetchUrlInstruction', () => {
  const originalFetch = globalThis.fetch

  beforeEach(() => {
    vi.restoreAllMocks()
  })

  afterEach(() => {
    globalThis.fetch = originalFetch
  })

  it('returns instruction file on successful fetch', async () => {
    globalThis.fetch = vi.fn().mockResolvedValue({
      ok: true,
      status: 200,
      text: vi.fn().mockResolvedValue('# Remote instructions'),
    }) as unknown as typeof fetch

    const result = await fetchUrlInstruction('https://example.com/instructions.md')

    expect(result).not.toBeNull()
    expect(result!.path).toBe('https://example.com/instructions.md')
    expect(result!.content).toBe('# Remote instructions')
    expect(result!.scope).toBe('remote')
    expect(result!.priority).toBe(0)
  })

  it('returns null on HTTP error response', async () => {
    const log = { debug: vi.fn(), info: vi.fn(), warn: vi.fn(), error: vi.fn() }

    globalThis.fetch = vi.fn().mockResolvedValue({
      ok: false,
      status: 404,
      text: vi.fn().mockResolvedValue('Not Found'),
    }) as unknown as typeof fetch

    const result = await fetchUrlInstruction('https://example.com/missing.md', log)

    expect(result).toBeNull()
    expect(log.warn).toHaveBeenCalledWith(expect.stringContaining('HTTP 404'))
  })

  it('returns null on network error', async () => {
    const log = { debug: vi.fn(), info: vi.fn(), warn: vi.fn(), error: vi.fn() }

    globalThis.fetch = vi
      .fn()
      .mockRejectedValue(new Error('Network unreachable')) as unknown as typeof fetch

    const result = await fetchUrlInstruction('https://unreachable.example.com/rules.md', log)

    expect(result).toBeNull()
    expect(log.warn).toHaveBeenCalledWith(expect.stringContaining('Network unreachable'))
  })

  it('passes abort signal to fetch', async () => {
    const controller = new AbortController()
    const fetchMock = vi.fn().mockResolvedValue({
      ok: true,
      status: 200,
      text: vi.fn().mockResolvedValue('content'),
    }) as unknown as typeof fetch
    globalThis.fetch = fetchMock

    await fetchUrlInstruction('https://example.com/rules.md', undefined, controller.signal)

    expect(fetchMock).toHaveBeenCalledWith(
      'https://example.com/rules.md',
      expect.objectContaining({ signal: expect.anything() })
    )
  })

  it('returns null without throwing on abort', async () => {
    const controller = new AbortController()
    controller.abort()

    globalThis.fetch = vi
      .fn()
      .mockRejectedValue(
        new DOMException('The operation was aborted', 'AbortError')
      ) as unknown as typeof fetch

    const result = await fetchUrlInstruction(
      'https://example.com/rules.md',
      undefined,
      controller.signal
    )

    expect(result).toBeNull()
  })

  it('calls fetch with timeout signal even without external signal', async () => {
    const fetchMock = vi.fn().mockResolvedValue({
      ok: true,
      status: 200,
      text: vi.fn().mockResolvedValue('content'),
    }) as unknown as typeof fetch
    globalThis.fetch = fetchMock

    await fetchUrlInstruction('https://example.com/rules.md')

    expect(fetchMock).toHaveBeenCalledWith(
      'https://example.com/rules.md',
      expect.objectContaining({ signal: expect.anything() })
    )
  })
})
