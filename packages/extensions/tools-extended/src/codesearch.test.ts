import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'
import { codesearchTool } from './codesearch.js'

const mockCtx = {
  sessionId: 'test',
  workingDirectory: '/tmp',
  signal: new AbortController().signal,
}

describe('codesearchTool', () => {
  beforeEach(() => {
    vi.stubGlobal('fetch', vi.fn())
  })

  afterEach(() => {
    vi.restoreAllMocks()
    delete process.env.EXA_API_KEY
  })

  it('has correct definition', () => {
    expect(codesearchTool.definition.name).toBe('codesearch')
  })

  it('returns error when EXA_API_KEY not set', async () => {
    const result = await codesearchTool.execute({ query: 'test' }, mockCtx)
    expect(result.success).toBe(false)
    expect(result.output).toContain('EXA_API_KEY')
  })

  it('searches with general type', async () => {
    process.env.EXA_API_KEY = 'test-key'
    const mockFetch = vi.fn().mockResolvedValue({
      ok: true,
      json: () =>
        Promise.resolve({
          results: [
            { title: 'React Docs', url: 'https://react.dev', text: 'content', score: 0.95 },
          ],
        }),
    })
    vi.stubGlobal('fetch', mockFetch)

    const result = await codesearchTool.execute({ query: 'react hooks' }, mockCtx)
    expect(result.success).toBe(true)
    expect(result.output).toContain('React Docs')
    expect(result.output).toContain('95.0%')
  })

  it('handles API errors', async () => {
    process.env.EXA_API_KEY = 'test-key'
    vi.stubGlobal(
      'fetch',
      vi.fn().mockResolvedValue({
        ok: false,
        status: 401,
        text: () => Promise.resolve('Unauthorized'),
      })
    )

    const result = await codesearchTool.execute({ query: 'test' }, mockCtx)
    expect(result.success).toBe(false)
    expect(result.output).toContain('Invalid Exa API key')
  })

  it('handles cancelled signal', async () => {
    const controller = new AbortController()
    controller.abort()
    const result = await codesearchTool.execute(
      { query: 'test' },
      { ...mockCtx, signal: controller.signal }
    )
    expect(result.success).toBe(false)
    expect(result.output).toContain('cancelled')
  })

  it('handles empty results', async () => {
    process.env.EXA_API_KEY = 'test-key'
    vi.stubGlobal(
      'fetch',
      vi.fn().mockResolvedValue({
        ok: true,
        json: () => Promise.resolve({ results: [] }),
      })
    )

    const result = await codesearchTool.execute({ query: 'obscure query' }, mockCtx)
    expect(result.success).toBe(true)
    expect(result.output).toContain('No results found')
  })
})
