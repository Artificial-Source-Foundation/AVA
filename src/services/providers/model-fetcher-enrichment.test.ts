import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'
import type { FetchedModel } from './model-fetcher'
import { enrichWithCatalog } from './model-fetcher'
import { _resetCatalogCache, syncModelsCatalog } from './models-dev-catalog'

const MOCK_CATALOG = {
  xai: {
    id: 'xai',
    name: 'xAI',
    models: {
      'grok-4-1-fast-reasoning': {
        id: 'grok-4-1-fast-reasoning',
        name: 'Grok 4.1 Fast (Reasoning)',
        attachment: true,
        reasoning: true,
        tool_call: true,
        cost: { input: 0.2, output: 0.5 },
        limit: { context: 2000000, output: 32768 },
      },
      'grok-code-fast-1': {
        id: 'grok-code-fast-1',
        name: 'Grok Code Fast',
        attachment: false,
        reasoning: true,
        tool_call: true,
        cost: { input: 0.2, output: 1.5 },
        limit: { context: 256000, output: 16384 },
      },
    },
  },
}

// Mock localStorage
const store = new Map<string, string>()
const mockLocalStorage = {
  getItem: (key: string) => store.get(key) ?? null,
  setItem: (key: string, value: string) => store.set(key, value),
  removeItem: (key: string) => store.delete(key),
} as unknown as Storage

describe('enrichWithCatalog', () => {
  beforeEach(async () => {
    store.clear()
    _resetCatalogCache()
    vi.stubGlobal('localStorage', mockLocalStorage)
    vi.stubGlobal(
      'fetch',
      vi.fn().mockResolvedValue({
        ok: true,
        json: () => Promise.resolve(MOCK_CATALOG),
      })
    )
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
