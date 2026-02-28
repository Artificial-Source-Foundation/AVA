import { describe, expect, it } from 'vitest'
import { browserTool } from './index.js'

const mockCtx = {
  sessionId: 'test',
  workingDirectory: '/tmp',
  signal: new AbortController().signal,
}

describe('browserTool', () => {
  it('has correct definition', () => {
    expect(browserTool.definition.name).toBe('browser')
  })

  it('validates launch requires url', async () => {
    const result = await browserTool.execute({ action: 'launch' }, mockCtx)
    expect(result.success).toBe(false)
    expect(result.output).toContain('URL is required')
  })

  it('validates click requires coordinate', async () => {
    const result = await browserTool.execute({ action: 'click' }, mockCtx)
    expect(result.success).toBe(false)
    expect(result.output).toContain('Coordinate is required')
  })

  it('validates type requires text', async () => {
    const result = await browserTool.execute({ action: 'type' }, mockCtx)
    expect(result.success).toBe(false)
    expect(result.output).toContain('Text is required')
  })

  it('reports puppeteer not installed for launch', async () => {
    // Puppeteer is not installed in test environment
    const result = await browserTool.execute(
      { action: 'launch', url: 'https://example.com' },
      mockCtx
    )
    expect(result.success).toBe(false)
    expect(result.output).toContain('Puppeteer is not installed')
  })
})
