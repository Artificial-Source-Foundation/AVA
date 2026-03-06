import { afterEach, describe, expect, it, vi } from 'vitest'
import {
  deleteOllamaModel,
  fetchOllamaModels,
  formatBytes,
  pullOllamaModel,
} from './ollama-helpers'

// ============================================================================
// formatBytes
// ============================================================================

describe('formatBytes', () => {
  it('formats kilobytes', () => {
    expect(formatBytes(512_000)).toBe('512.0 KB')
  })

  it('formats megabytes', () => {
    expect(formatBytes(150_000_000)).toBe('150.0 MB')
  })

  it('formats gigabytes with two decimals', () => {
    expect(formatBytes(4_200_000_000)).toBe('4.20 GB')
  })

  it('handles zero', () => {
    expect(formatBytes(0)).toBe('0.0 KB')
  })

  it('handles boundary at 1 MB', () => {
    expect(formatBytes(999_999)).toBe('1000.0 KB')
    expect(formatBytes(1_000_000)).toBe('1.0 MB')
  })

  it('handles boundary at 1 GB', () => {
    expect(formatBytes(999_999_999)).toBe('1000.0 MB')
    expect(formatBytes(1_000_000_000)).toBe('1.00 GB')
  })
})

// ============================================================================
// API functions (fetch-based)
// ============================================================================

const BASE_URL = 'http://localhost:11434'

describe('fetchOllamaModels', () => {
  afterEach(() => {
    vi.restoreAllMocks()
  })

  it('parses tag response into OllamaModel[]', async () => {
    const mockResponse = {
      models: [
        {
          name: 'llama3:latest',
          size: 4_000_000_000,
          details: {
            family: 'llama',
            parameter_size: '8B',
            quantization_level: 'Q4_0',
          },
          modified_at: '2024-01-15T10:00:00Z',
        },
      ],
    }
    vi.spyOn(globalThis, 'fetch').mockResolvedValue({
      ok: true,
      json: async () => mockResponse,
    } as Response)

    const models = await fetchOllamaModels(BASE_URL)
    expect(models).toHaveLength(1)
    expect(models[0]).toEqual({
      name: 'llama3:latest',
      size: 4_000_000_000,
      family: 'llama',
      parameterSize: '8B',
      quantizationLevel: 'Q4_0',
      modifiedAt: '2024-01-15T10:00:00Z',
    })
    expect(fetch).toHaveBeenCalledWith(`${BASE_URL}/api/tags`)
  })

  it('handles missing details gracefully', async () => {
    const mockResponse = {
      models: [
        {
          name: 'custom:latest',
          size: 1_000_000,
          details: {} as { family: string; parameter_size: string; quantization_level: string },
          modified_at: '2024-01-01T00:00:00Z',
        },
      ],
    }
    vi.spyOn(globalThis, 'fetch').mockResolvedValue({
      ok: true,
      json: async () => mockResponse,
    } as Response)

    const models = await fetchOllamaModels(BASE_URL)
    expect(models[0]?.family).toBe('unknown')
    expect(models[0]?.parameterSize).toBe('')
    expect(models[0]?.quantizationLevel).toBe('')
  })

  it('throws on HTTP error', async () => {
    vi.spyOn(globalThis, 'fetch').mockResolvedValue({
      ok: false,
      status: 500,
    } as Response)

    await expect(fetchOllamaModels(BASE_URL)).rejects.toThrow('HTTP 500')
  })

  it('returns empty array when models is undefined', async () => {
    vi.spyOn(globalThis, 'fetch').mockResolvedValue({
      ok: true,
      json: async () => ({}),
    } as Response)

    const models = await fetchOllamaModels(BASE_URL)
    expect(models).toEqual([])
  })
})

describe('pullOllamaModel', () => {
  afterEach(() => {
    vi.restoreAllMocks()
  })

  it('sends POST to /api/pull', async () => {
    vi.spyOn(globalThis, 'fetch').mockResolvedValue({ ok: true } as Response)

    await pullOllamaModel(BASE_URL, 'llama3:latest')
    expect(fetch).toHaveBeenCalledWith(`${BASE_URL}/api/pull`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ name: 'llama3:latest', stream: false }),
    })
  })

  it('throws error body on failure', async () => {
    vi.spyOn(globalThis, 'fetch').mockResolvedValue({
      ok: false,
      status: 404,
      text: async () => 'model not found',
    } as Response)

    await expect(pullOllamaModel(BASE_URL, 'bad:model')).rejects.toThrow('model not found')
  })
})

describe('deleteOllamaModel', () => {
  afterEach(() => {
    vi.restoreAllMocks()
  })

  it('sends DELETE to /api/delete', async () => {
    vi.spyOn(globalThis, 'fetch').mockResolvedValue({ ok: true } as Response)

    await deleteOllamaModel(BASE_URL, 'old-model:latest')
    expect(fetch).toHaveBeenCalledWith(`${BASE_URL}/api/delete`, {
      method: 'DELETE',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ name: 'old-model:latest' }),
    })
  })

  it('throws on HTTP error', async () => {
    vi.spyOn(globalThis, 'fetch').mockResolvedValue({
      ok: false,
      status: 500,
    } as Response)

    await expect(deleteOllamaModel(BASE_URL, 'model')).rejects.toThrow('HTTP 500')
  })
})
