import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'
import {
  _resetCatalogCache,
  getModelFromCatalog,
  getModelsDevModels,
  syncModelsCatalog,
} from './models-dev-catalog'

// Mock localStorage
const store = new Map<string, string>()
const mockLocalStorage = {
  getItem: (key: string) => store.get(key) ?? null,
  setItem: (key: string, value: string) => store.set(key, value),
  removeItem: (key: string) => store.delete(key),
} as unknown as Storage

// Sample API response
const MOCK_CATALOG = {
  anthropic: {
    id: 'anthropic',
    name: 'Anthropic',
    models: {
      'claude-sonnet-4-6': {
        id: 'claude-sonnet-4-6',
        name: 'Claude Sonnet 4.6',
        family: 'claude-sonnet',
        attachment: true,
        reasoning: true,
        tool_call: true,
        modalities: { input: ['text', 'image', 'pdf'], output: ['text'] },
        cost: { input: 3, output: 15 },
        limit: { context: 200000, output: 64000 },
      },
      'claude-haiku-4-5-20251001': {
        id: 'claude-haiku-4-5-20251001',
        name: 'Claude Haiku 4.5',
        family: 'claude-haiku',
        attachment: true,
        reasoning: false,
        tool_call: true,
        cost: { input: 1, output: 5 },
        limit: { context: 200000, output: 8192 },
      },
    },
  },
  openai: {
    id: 'openai',
    name: 'OpenAI',
    models: {
      'gpt-5.2': {
        id: 'gpt-5.2',
        name: 'GPT-5.2',
        attachment: true,
        reasoning: true,
        tool_call: true,
        cost: { input: 1.75, output: 14 },
        limit: { context: 400000, output: 32768 },
      },
      'text-embedding-3-large': {
        id: 'text-embedding-3-large',
        name: 'Text Embedding 3 Large',
        tool_call: false,
        modalities: { input: ['text'], output: ['text'] },
        limit: { context: 8192 },
      },
      'tts-1': {
        id: 'tts-1',
        name: 'TTS-1',
        tool_call: false,
        modalities: { input: ['text'], output: ['audio'] },
        limit: { context: 4096 },
      },
    },
  },
  togetherai: {
    id: 'togetherai',
    name: 'Together AI',
    models: {
      'meta-llama/Llama-3.3-70B-Instruct-Turbo': {
        id: 'meta-llama/Llama-3.3-70B-Instruct-Turbo',
        name: 'Llama 3.3 70B',
        tool_call: true,
        reasoning: false,
        attachment: false,
        limit: { context: 128000 },
      },
    },
  },
}

describe('models-dev-catalog', () => {
  beforeEach(() => {
    store.clear()
    _resetCatalogCache()
    vi.stubGlobal('localStorage', mockLocalStorage)
  })

  afterEach(() => {
    vi.restoreAllMocks()
  })

  describe('syncModelsCatalog', () => {
    it('fetches and caches catalog on success', async () => {
      vi.stubGlobal(
        'fetch',
        vi.fn().mockResolvedValue({
          ok: true,
          json: () => Promise.resolve(MOCK_CATALOG),
        })
      )

      const result = await syncModelsCatalog()
      expect(result).toBeTruthy()
      expect(result!.anthropic.name).toBe('Anthropic')

      // Verify cached in localStorage
      expect(store.has('ava:models-dev-catalog')).toBe(true)
      expect(store.has('ava:models-dev-catalog-ts')).toBe(true)
    })

    it('returns cached data within TTL without fetching', async () => {
      // Pre-populate cache
      store.set('ava:models-dev-catalog', JSON.stringify(MOCK_CATALOG))
      store.set('ava:models-dev-catalog-ts', String(Date.now()))

      const fetchMock = vi.fn()
      vi.stubGlobal('fetch', fetchMock)

      const result = await syncModelsCatalog()
      expect(result).toBeTruthy()
      expect(result!.anthropic.name).toBe('Anthropic')
      expect(fetchMock).not.toHaveBeenCalled()
    })

    it('falls back to stale cache on network error', async () => {
      // Pre-populate stale cache (expired)
      store.set('ava:models-dev-catalog', JSON.stringify(MOCK_CATALOG))
      store.set('ava:models-dev-catalog-ts', String(Date.now() - 2 * 60 * 60 * 1000))

      vi.stubGlobal(
        'fetch',
        vi.fn().mockRejectedValue(new Error('Network error'))
      )

      const result = await syncModelsCatalog()
      expect(result).toBeTruthy()
      expect(result!.anthropic.name).toBe('Anthropic')
    })

    it('returns null when no cache and network fails', async () => {
      vi.stubGlobal(
        'fetch',
        vi.fn().mockRejectedValue(new Error('Network error'))
      )

      const result = await syncModelsCatalog()
      expect(result).toBeNull()
    })
  })

  describe('getModelsDevModels', () => {
    beforeEach(async () => {
      vi.stubGlobal(
        'fetch',
        vi.fn().mockResolvedValue({
          ok: true,
          json: () => Promise.resolve(MOCK_CATALOG),
        })
      )
      await syncModelsCatalog()
    })

    it('transforms models for a known provider', () => {
      const models = getModelsDevModels('anthropic')
      expect(models.length).toBe(2)

      const sonnet = models.find((m) => m.id === 'claude-sonnet-4-6')!
      expect(sonnet.name).toBe('Claude Sonnet 4.6')
      expect(sonnet.contextWindow).toBe(200000)
      expect(sonnet.pricing).toEqual({ input: 3, output: 15 })
      expect(sonnet.capabilities).toContain('tools')
      expect(sonnet.capabilities).toContain('reasoning')
      expect(sonnet.capabilities).toContain('vision')
    })

    it('maps AVA provider IDs to models.dev keys', () => {
      const models = getModelsDevModels('together')
      expect(models.length).toBe(1)
      expect(models[0].id).toBe('meta-llama/Llama-3.3-70B-Instruct-Turbo')
    })

    it('filters out non-coding models (embeddings, TTS)', () => {
      const models = getModelsDevModels('openai')
      expect(models.length).toBe(1) // Only gpt-5.2 (embed + tts filtered)
      expect(models[0].id).toBe('gpt-5.2')
    })

    it('returns empty array for unknown provider', () => {
      const models = getModelsDevModels('nonexistent' as 'openai')
      expect(models).toEqual([])
    })

    it('returns empty array when catalog not loaded', () => {
      _resetCatalogCache()
      const models = getModelsDevModels('anthropic')
      expect(models).toEqual([])
    })
  })

  describe('getModelFromCatalog', () => {
    beforeEach(async () => {
      vi.stubGlobal(
        'fetch',
        vi.fn().mockResolvedValue({
          ok: true,
          json: () => Promise.resolve(MOCK_CATALOG),
        })
      )
      await syncModelsCatalog()
    })

    it('finds model within a specific provider', () => {
      const model = getModelFromCatalog('claude-sonnet-4-6', 'anthropic')
      expect(model).toBeTruthy()
      expect(model!.name).toBe('Claude Sonnet 4.6')
      expect(model!.reasoning).toBe(true)
    })

    it('searches across all providers when no provider specified', () => {
      const model = getModelFromCatalog('gpt-5.2')
      expect(model).toBeTruthy()
      expect(model!.name).toBe('GPT-5.2')
    })

    it('returns null for unknown model', () => {
      expect(getModelFromCatalog('nonexistent-model')).toBeNull()
    })
  })
})
