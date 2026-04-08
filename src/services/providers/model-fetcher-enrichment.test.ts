import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'
import { _resetCatalogCache, syncModelsCatalog } from './curated-model-catalog'
import type { FetchedModel } from './model-fetcher'
import { enrichWithCatalog } from './model-fetcher'

const mockListModels = vi.fn()

vi.mock('../rust-bridge', () => ({
  rustBackend: {
    listModels: () => mockListModels(),
  },
}))

describe('enrichWithCatalog', () => {
  beforeEach(async () => {
    _resetCatalogCache()
    mockListModels.mockResolvedValue([
      {
        id: 'grok-4-1-fast-reasoning',
        provider: 'xai',
        name: 'Grok 4.1 Fast (Reasoning)',
        toolCall: true,
        vision: true,
        reasoning: true,
        capabilities: ['tools', 'vision', 'reasoning'],
        contextWindow: 2000000,
        maxOutput: 32768,
        costInput: 0.2,
        costOutput: 0.5,
      },
      {
        id: 'grok-code-fast-1',
        provider: 'xai',
        name: 'Grok Code Fast',
        toolCall: true,
        vision: false,
        reasoning: true,
        capabilities: ['tools', 'reasoning'],
        contextWindow: 256000,
        maxOutput: 16384,
        costInput: 0.2,
        costOutput: 1.5,
      },
    ])
    await syncModelsCatalog()
  })

  afterEach(() => {
    vi.restoreAllMocks()
  })

  it('fills context window when model has default 4096', () => {
    const fetched: FetchedModel[] = [
      { id: 'grok-4-1-fast-reasoning', name: 'Grok 4.1 Fast', contextWindow: 4096 },
    ]
    const enriched = enrichWithCatalog('xai', fetched)
    expect(enriched[0].contextWindow).toBe(2000000)
  })

  it('does not override provider-supplied context window', () => {
    const fetched: FetchedModel[] = [
      { id: 'grok-4-1-fast-reasoning', name: 'Grok 4.1 Fast', contextWindow: 131072 },
    ]
    const enriched = enrichWithCatalog('xai', fetched)
    expect(enriched[0].contextWindow).toBe(131072)
  })

  it('fills missing pricing from catalog', () => {
    const fetched: FetchedModel[] = [
      { id: 'grok-code-fast-1', name: 'Grok Code Fast', contextWindow: 4096 },
    ]
    const enriched = enrichWithCatalog('xai', fetched)
    expect(enriched[0].pricing).toEqual({ prompt: 0.2, completion: 1.5 })
  })

  it('does not override existing pricing', () => {
    const fetched: FetchedModel[] = [
      {
        id: 'grok-code-fast-1',
        name: 'Grok Code Fast',
        contextWindow: 4096,
        pricing: { prompt: 99, completion: 99 },
      },
    ]
    const enriched = enrichWithCatalog('xai', fetched)
    expect(enriched[0].pricing).toEqual({ prompt: 99, completion: 99 })
  })

  it('fills missing capabilities from catalog', () => {
    const fetched: FetchedModel[] = [
      { id: 'grok-4-1-fast-reasoning', name: 'Grok 4.1 Fast', contextWindow: 4096 },
    ]
    const enriched = enrichWithCatalog('xai', fetched)
    expect(enriched[0].capabilities).toContain('tools')
    expect(enriched[0].capabilities).toContain('reasoning')
    expect(enriched[0].capabilities).toContain('vision')
  })

  it('merges catalog capabilities into existing ones', () => {
    const fetched: FetchedModel[] = [
      {
        id: 'grok-4-1-fast-reasoning',
        name: 'Grok 4.1 Fast',
        contextWindow: 4096,
        capabilities: ['tools'],
      },
    ]
    const enriched = enrichWithCatalog('xai', fetched)
    // Existing 'tools' is preserved, catalog adds 'reasoning' and 'vision'
    expect(enriched[0].capabilities).toContain('tools')
    expect(enriched[0].capabilities).toContain('reasoning')
    expect(enriched[0].capabilities).toContain('vision')
  })

  it('passes through models not found in catalog', () => {
    const fetched: FetchedModel[] = [{ id: 'unknown-model', name: 'Unknown', contextWindow: 4096 }]
    const enriched = enrichWithCatalog('xai', fetched)
    expect(enriched[0]).toEqual(fetched[0])
  })

  it('handles empty catalog gracefully', () => {
    _resetCatalogCache()
    const fetched: FetchedModel[] = [
      { id: 'grok-code-fast-1', name: 'Grok Code Fast', contextWindow: 4096 },
    ]
    const enriched = enrichWithCatalog('xai', fetched)
    expect(enriched[0].contextWindow).toBe(4096) // No enrichment
  })
})
