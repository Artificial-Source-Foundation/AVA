import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'
import { websearchTool } from './websearch.js'

const mockCtx = {
  sessionId: 'test',
  workingDirectory: '/tmp',
  signal: new AbortController().signal,
}

describe('websearchTool', () => {
  beforeEach(() => {
    vi.stubGlobal('fetch', vi.fn())
  })

  afterEach(() => {
    vi.restoreAllMocks()
    delete process.env.TAVILY_API_KEY
    delete process.env.EXA_API_KEY
  })

  it('has correct definition', () => {
    expect(websearchTool.definition.name).toBe('websearch')
  })

  it('returns error when no provider configured', async () => {
    const result = await websearchTool.execute({ query: 'test' }, mockCtx)
    expect(result.success).toBe(false)
    expect(result.output).toContain('No search provider configured')
  })

  it('auto-detects tavily when key is set', async () => {
    process.env.TAVILY_API_KEY = 'test-key'
    const mockFetch = vi.fn().mockResolvedValue({
      ok: true,
      json: () =>
        Promise.resolve({
          results: [{ title: 'Test', url: 'https://example.com', content: 'snippet', score: 0.9 }],
        }),
    })
    vi.stubGlobal('fetch', mockFetch)

    const result = await websearchTool.execute({ query: 'test query' }, mockCtx)
    expect(result.success).toBe(true)
    expect(result.output).toContain('tavily')
    expect(mockFetch).toHaveBeenCalledWith('https://api.tavily.com/search', expect.anything())
  })

  it('auto-detects exa when key is set', async () => {
    process.env.EXA_API_KEY = 'test-key'
    const mockFetch = vi.fn().mockResolvedValue({
      ok: true,
      json: () =>
        Promise.resolve({
          results: [{ title: 'Test', url: 'https://example.com', text: 'snippet', score: 0.8 }],
        }),
    })
    vi.stubGlobal('fetch', mockFetch)

    const result = await websearchTool.execute({ query: 'test query' }, mockCtx)
    expect(result.success).toBe(true)
    expect(result.output).toContain('exa')
    expect(mockFetch).toHaveBeenCalledWith('https://api.exa.ai/search', expect.anything())
  })

  it('handles API errors', async () => {
    process.env.TAVILY_API_KEY = 'test-key'
    vi.stubGlobal(
      'fetch',
      vi.fn().mockResolvedValue({
        ok: false,
        status: 500,
        text: () => Promise.resolve('Server error'),
      })
    )

    const result = await websearchTool.execute({ query: 'test' }, mockCtx)
    expect(result.success).toBe(false)
    expect(result.output).toContain('Search failed')
  })

  it('handles cancelled signal', async () => {
    const controller = new AbortController()
    controller.abort()
    const result = await websearchTool.execute(
      { query: 'test' },
      { ...mockCtx, signal: controller.signal }
    )
    expect(result.success).toBe(false)
    expect(result.output).toContain('cancelled')
  })

  it('formats results with truncated snippets', async () => {
    process.env.TAVILY_API_KEY = 'test-key'
    const longSnippet = 'a'.repeat(400)
    vi.stubGlobal(
      'fetch',
      vi.fn().mockResolvedValue({
        ok: true,
        json: () =>
          Promise.resolve({
            results: [
              { title: 'Long', url: 'https://example.com', content: longSnippet, score: 0.9 },
            ],
          }),
      })
    )

    const result = await websearchTool.execute({ query: 'test' }, mockCtx)
    expect(result.success).toBe(true)
    expect(result.output).toContain('...')
    expect(result.output.length).toBeLessThan(longSnippet.length + 200)
  })
})
