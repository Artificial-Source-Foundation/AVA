/**
 * Embedding Utilities
 *
 * Text embedding generation using LLM providers.
 * Default implementation uses OpenAI text-embedding-3-small.
 */

import type { Embedder } from './types.js'
import { EMBEDDING_DIMENSIONS } from './types.js'

// ============================================================================
// OpenAI Embedder
// ============================================================================

/** OpenAI embedding API response */
interface EmbeddingResponse {
  data: { embedding: number[]; index: number }[]
  model: string
  usage: { prompt_tokens: number; total_tokens: number }
}

/**
 * OpenAI-compatible embedder using text-embedding-3-small
 */
export class OpenAIEmbedder implements Embedder {
  private apiKey: string | null = null
  private baseUrl: string
  private model: string

  constructor(options?: { apiKey?: string; baseUrl?: string; model?: string }) {
    this.apiKey = options?.apiKey ?? null
    this.baseUrl = options?.baseUrl ?? 'https://api.openai.com/v1'
    this.model = options?.model ?? 'text-embedding-3-small'
  }

  /**
   * Set API key (can be called after construction)
   */
  setApiKey(apiKey: string): void {
    this.apiKey = apiKey
  }

  /**
   * Generate embedding for text
   */
  async embed(text: string): Promise<Float32Array> {
    const results = await this.embedBatch([text])
    return results[0]
  }

  /**
   * Generate embeddings for multiple texts (batch)
   */
  async embedBatch(texts: string[]): Promise<Float32Array[]> {
    const key = this.getApiKey()

    const response = await fetch(`${this.baseUrl}/embeddings`, {
      method: 'POST',
      headers: {
        'Content-Type': 'application/json',
        Authorization: `Bearer ${key}`,
      },
      body: JSON.stringify({
        model: this.model,
        input: texts,
      }),
    })

    if (!response.ok) {
      const error = await response.text()
      throw new EmbeddingError(`Embedding API error: ${response.status} ${error}`)
    }

    const data = (await response.json()) as EmbeddingResponse

    // Sort by index to maintain order
    const sorted = data.data.sort((a, b) => a.index - b.index)

    return sorted.map((d) => new Float32Array(d.embedding))
  }

  /**
   * Get embedding dimensions
   */
  getDimensions(): number {
    return EMBEDDING_DIMENSIONS
  }

  /**
   * Get API key from environment or stored value
   */
  private getApiKey(): string {
    if (this.apiKey) return this.apiKey

    const envKey = process.env.OPENAI_API_KEY
    if (envKey) return envKey

    throw new EmbeddingError('OpenAI API key not configured')
  }
}

// ============================================================================
// Mock Embedder (for testing)
// ============================================================================

/**
 * Mock embedder that generates deterministic embeddings based on text hash
 * Useful for testing without API calls
 */
export class MockEmbedder implements Embedder {
  private dimensions: number

  constructor(dimensions = EMBEDDING_DIMENSIONS) {
    this.dimensions = dimensions
  }

  async embed(text: string): Promise<Float32Array> {
    return this.generateDeterministicEmbedding(text)
  }

  async embedBatch(texts: string[]): Promise<Float32Array[]> {
    return texts.map((t) => this.generateDeterministicEmbedding(t))
  }

  getDimensions(): number {
    return this.dimensions
  }

  /**
   * Generate a deterministic embedding based on text content
   * Similar texts will have similar embeddings
   */
  private generateDeterministicEmbedding(text: string): Float32Array {
    const embedding = new Float32Array(this.dimensions)

    // Simple hash-based generation
    const hash = this.hashString(text)

    // Use hash to seed a simple random number generator
    let seed = hash
    for (let i = 0; i < this.dimensions; i++) {
      seed = (seed * 1103515245 + 12345) & 0x7fffffff
      // Generate value between -1 and 1
      embedding[i] = (seed / 0x7fffffff) * 2 - 1
    }

    // Normalize to unit vector
    const norm = Math.sqrt(embedding.reduce((sum, v) => sum + v * v, 0))
    if (norm > 0) {
      for (let i = 0; i < this.dimensions; i++) {
        embedding[i] /= norm
      }
    }

    return embedding
  }

  /**
   * Simple string hash function
   */
  private hashString(str: string): number {
    let hash = 0
    for (let i = 0; i < str.length; i++) {
      const char = str.charCodeAt(i)
      hash = ((hash << 5) - hash + char) | 0
    }
    return Math.abs(hash)
  }
}

// ============================================================================
// Caching Embedder
// ============================================================================

/**
 * Caching wrapper for any embedder
 * Stores embeddings in memory to avoid duplicate API calls
 */
export class CachingEmbedder implements Embedder {
  private cache = new Map<string, Float32Array>()
  private maxCacheSize: number

  constructor(
    private inner: Embedder,
    options?: { maxCacheSize?: number }
  ) {
    this.maxCacheSize = options?.maxCacheSize ?? 1000
  }

  async embed(text: string): Promise<Float32Array> {
    const cached = this.cache.get(text)
    if (cached) {
      return cached
    }

    const embedding = await this.inner.embed(text)
    this.addToCache(text, embedding)
    return embedding
  }

  async embedBatch(texts: string[]): Promise<Float32Array[]> {
    const results: Float32Array[] = []
    const uncached: { index: number; text: string }[] = []

    // Check cache first
    for (let i = 0; i < texts.length; i++) {
      const cached = this.cache.get(texts[i])
      if (cached) {
        results[i] = cached
      } else {
        uncached.push({ index: i, text: texts[i] })
      }
    }

    // Fetch uncached
    if (uncached.length > 0) {
      const embeddings = await this.inner.embedBatch(uncached.map((u) => u.text))

      for (let i = 0; i < uncached.length; i++) {
        const { index, text } = uncached[i]
        const embedding = embeddings[i]
        results[index] = embedding
        this.addToCache(text, embedding)
      }
    }

    return results
  }

  getDimensions(): number {
    return this.inner.getDimensions()
  }

  /**
   * Clear the cache
   */
  clearCache(): void {
    this.cache.clear()
  }

  /**
   * Get cache statistics
   */
  getCacheStats(): { size: number; maxSize: number } {
    return {
      size: this.cache.size,
      maxSize: this.maxCacheSize,
    }
  }

  private addToCache(text: string, embedding: Float32Array): void {
    // Evict oldest if at capacity
    if (this.cache.size >= this.maxCacheSize) {
      const firstKey = this.cache.keys().next().value
      if (firstKey) {
        this.cache.delete(firstKey)
      }
    }

    this.cache.set(text, embedding)
  }
}

// ============================================================================
// Errors
// ============================================================================

/**
 * Error during embedding generation
 */
export class EmbeddingError extends Error {
  constructor(message: string) {
    super(message)
    this.name = 'EmbeddingError'
  }
}

// ============================================================================
// Factories
// ============================================================================

/**
 * Create an OpenAI embedder
 */
export function createOpenAIEmbedder(options?: {
  apiKey?: string
  baseUrl?: string
  model?: string
}): OpenAIEmbedder {
  return new OpenAIEmbedder(options)
}

/**
 * Create a mock embedder for testing
 */
export function createMockEmbedder(dimensions?: number): MockEmbedder {
  return new MockEmbedder(dimensions)
}

/**
 * Create a caching embedder wrapper
 */
export function createCachingEmbedder(
  inner: Embedder,
  options?: { maxCacheSize?: number }
): CachingEmbedder {
  return new CachingEmbedder(inner, options)
}
