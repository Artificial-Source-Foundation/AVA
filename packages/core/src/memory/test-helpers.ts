/**
 * Test Helpers for Memory Module
 *
 * Mock implementations of VectorStore and Embedder for testing.
 */

import type {
  Embedder,
  MemoryEntry,
  MemoryId,
  MemoryQuery,
  MemoryType,
  SimilarityResult,
  VectorStore,
} from './types.js'

// ============================================================================
// Mock Embedder
// ============================================================================

/**
 * Mock embedder that generates deterministic embeddings
 * Uses hash-based approach for reproducibility
 */
export class MockEmbedder implements Embedder {
  private readonly dimensions = 4

  /**
   * Generate deterministic embedding from text
   * Same text always produces same embedding
   */
  async embed(text: string): Promise<Float32Array> {
    return this.generateEmbedding(text)
  }

  /**
   * Generate embeddings for multiple texts
   */
  async embedBatch(texts: string[]): Promise<Float32Array[]> {
    return Promise.all(texts.map((t) => this.embed(t)))
  }

  /**
   * Get embedding dimensions
   */
  getDimensions(): number {
    return this.dimensions
  }

  /**
   * Generate deterministic embedding from text hash
   */
  private generateEmbedding(text: string): Float32Array {
    const hash = this.hashString(text)
    const values = new Float32Array(this.dimensions)

    // Generate 4 values from hash
    for (let i = 0; i < this.dimensions; i++) {
      // Use different parts of hash for each dimension
      const offset = i * 8
      const slice = (hash >>> offset) & 0xff
      values[i] = (slice / 255) * 2 - 1 // Normalize to [-1, 1]
    }

    // Normalize vector
    const magnitude = Math.sqrt(values.reduce((sum, v) => sum + v * v, 0))
    if (magnitude > 0) {
      for (let i = 0; i < this.dimensions; i++) {
        values[i] /= magnitude
      }
    }

    return values
  }

  /**
   * Simple hash function for strings
   */
  private hashString(str: string): number {
    let hash = 0
    for (let i = 0; i < str.length; i++) {
      const char = str.charCodeAt(i)
      hash = (hash << 5) - hash + char
      hash = hash & hash // Convert to 32bit integer
    }
    return hash >>> 0 // Make unsigned
  }
}

// ============================================================================
// In-Memory Vector Store
// ============================================================================

/**
 * In-memory vector store for testing
 * Implements all VectorStore operations without persistence
 */
export class InMemoryVectorStore implements VectorStore {
  private entries = new Map<MemoryId, MemoryEntry>()

  /**
   * Insert a memory entry
   */
  async insert(entry: MemoryEntry): Promise<void> {
    this.entries.set(entry.id, { ...entry })
  }

  /**
   * Get a memory by ID
   */
  async get(id: MemoryId): Promise<MemoryEntry | null> {
    const entry = this.entries.get(id)
    return entry ? { ...entry } : null
  }

  /**
   * Update a memory entry
   */
  async update(id: MemoryId, updates: Partial<MemoryEntry>): Promise<void> {
    const entry = this.entries.get(id)
    if (!entry) return

    // Merge updates
    const updated = { ...entry, ...updates }

    // Handle nested metadata updates
    if (updates.metadata) {
      updated.metadata = { ...entry.metadata, ...updates.metadata }
    }

    this.entries.set(id, updated)
  }

  /**
   * Delete a memory entry
   */
  async delete(id: MemoryId): Promise<void> {
    this.entries.delete(id)
  }

  /**
   * Query memories with filters
   */
  async query(query: MemoryQuery): Promise<MemoryEntry[]> {
    let results = Array.from(this.entries.values())

    // Filter by type
    if (query.type) {
      const types = Array.isArray(query.type) ? query.type : [query.type]
      results = results.filter((e) => types.includes(e.type))
    }

    // Filter by tags
    if (query.tags && query.tags.length > 0) {
      results = results.filter((e) => query.tags!.some((tag) => e.metadata.tags.includes(tag)))
    }

    // Filter by importance
    if (query.minImportance !== undefined) {
      results = results.filter((e) => e.metadata.importance >= query.minImportance!)
    }

    // Filter by age
    if (query.maxAge !== undefined) {
      const cutoff = Date.now() - query.maxAge
      results = results.filter((e) => e.metadata.timestamp >= cutoff)
    }

    // Sort
    if (query.orderBy) {
      results.sort((a, b) => {
        let aVal: number
        let bVal: number

        switch (query.orderBy) {
          case 'timestamp':
            aVal = a.metadata.timestamp
            bVal = b.metadata.timestamp
            break
          case 'importance':
            aVal = a.metadata.importance
            bVal = b.metadata.importance
            break
          case 'lastAccessed':
            aVal = a.metadata.lastAccessed
            bVal = b.metadata.lastAccessed
            break
          case 'accessCount':
            aVal = a.metadata.accessCount
            bVal = b.metadata.accessCount
            break
          default:
            return 0
        }

        return query.order === 'desc' ? bVal - aVal : aVal - bVal
      })
    }

    // Offset
    if (query.offset) {
      results = results.slice(query.offset)
    }

    // Limit
    if (query.limit) {
      results = results.slice(0, query.limit)
    }

    return results.map((e) => ({ ...e }))
  }

  /**
   * Find similar memories by embedding
   */
  async findSimilar(
    embedding: Float32Array,
    limit: number,
    type?: MemoryType
  ): Promise<SimilarityResult[]> {
    let candidates = Array.from(this.entries.values())

    // Filter by type
    if (type) {
      candidates = candidates.filter((e) => e.type === type)
    }

    // Filter entries with embeddings
    candidates = candidates.filter((e) => e.embedding !== undefined)

    // Calculate similarities
    const similarities: SimilarityResult[] = candidates.map((memory) => ({
      memory,
      similarity: this.cosineSimilarity(embedding, memory.embedding!),
    }))

    // Sort by similarity (descending) and limit
    return similarities.sort((a, b) => b.similarity - a.similarity).slice(0, limit)
  }

  /**
   * Count memories by type
   */
  async count(type?: MemoryType): Promise<number> {
    if (!type) {
      return this.entries.size
    }

    let count = 0
    for (const entry of this.entries.values()) {
      if (entry.type === type) count++
    }
    return count
  }

  /**
   * Get all memory IDs
   */
  async getAllIds(type?: MemoryType): Promise<MemoryId[]> {
    const ids: MemoryId[] = []

    for (const entry of this.entries.values()) {
      if (!type || entry.type === type) {
        ids.push(entry.id)
      }
    }

    return ids
  }

  /**
   * Clear all entries (for testing)
   */
  clear(): void {
    this.entries.clear()
  }

  /**
   * Get entry count (for testing)
   */
  size(): number {
    return this.entries.size
  }

  /**
   * Calculate cosine similarity between two vectors
   */
  private cosineSimilarity(a: Float32Array, b: Float32Array): number {
    if (a.length !== b.length) return 0

    let dotProduct = 0
    let normA = 0
    let normB = 0

    for (let i = 0; i < a.length; i++) {
      dotProduct += a[i] * b[i]
      normA += a[i] * a[i]
      normB += b[i] * b[i]
    }

    normA = Math.sqrt(normA)
    normB = Math.sqrt(normB)

    if (normA === 0 || normB === 0) return 0
    return dotProduct / (normA * normB)
  }
}

// ============================================================================
// Test Utilities
// ============================================================================

/**
 * Create a mock store and embedder for tests
 */
export function createTestDependencies(): {
  store: InMemoryVectorStore
  embedder: MockEmbedder
} {
  return {
    store: new InMemoryVectorStore(),
    embedder: new MockEmbedder(),
  }
}

/**
 * Wait for a specified time (for async tests)
 */
export function sleep(ms: number): Promise<void> {
  return new Promise((resolve) => setTimeout(resolve, ms))
}
