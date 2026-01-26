/**
 * Delta9 Semantic Memory Search
 *
 * Vector similarity search for knowledge retrieval.
 *
 * Features:
 * - Pluggable embedding providers (OpenAI, local, mock)
 * - In-memory vector index with persistence
 * - Cosine similarity search
 * - Automatic chunking for long documents
 */

import type { KnowledgeBlock, KnowledgeScope } from './types.js'

// =============================================================================
// Types
// =============================================================================

/** Embedding vector (array of floats) */
export type EmbeddingVector = number[]

/** Embedding provider interface */
export interface EmbeddingProvider {
  /** Provider name */
  name: string
  /** Embedding dimension */
  dimension: number
  /** Generate embedding for text */
  embed(text: string): Promise<EmbeddingVector>
  /** Generate embeddings for multiple texts (batch) */
  embedBatch?(texts: string[]): Promise<EmbeddingVector[]>
}

/** Indexed document */
export interface IndexedDocument {
  /** Unique ID */
  id: string
  /** Source knowledge block */
  source: {
    scope: KnowledgeScope
    label: string
    category: string
  }
  /** Text content (or chunk) */
  text: string
  /** Embedding vector */
  embedding: EmbeddingVector
  /** Chunk index (if document was split) */
  chunkIndex?: number
  /** Total chunks (if document was split) */
  totalChunks?: number
  /** Timestamp when indexed */
  indexedAt: Date
}

/** Search result */
export interface SemanticSearchResult {
  /** Document */
  document: IndexedDocument
  /** Similarity score (0-1, higher = more similar) */
  score: number
  /** Rank in results */
  rank: number
}

/** Semantic index configuration */
export interface SemanticIndexConfig {
  /** Embedding provider */
  provider: EmbeddingProvider
  /** Maximum chunk size (characters) */
  maxChunkSize: number
  /** Chunk overlap (characters) */
  chunkOverlap: number
  /** Minimum similarity threshold for results */
  minSimilarity: number
  /** Maximum results to return */
  maxResults: number
  /** Logger */
  log?: (level: string, message: string, data?: Record<string, unknown>) => void
}

/** Index statistics */
export interface IndexStats {
  /** Total documents indexed */
  totalDocuments: number
  /** Total chunks (may be > documents if chunking was used) */
  totalChunks: number
  /** Documents by scope */
  byScope: Record<KnowledgeScope, number>
  /** Documents by category */
  byCategory: Record<string, number>
  /** Embedding dimension */
  embeddingDimension: number
  /** Provider name */
  providerName: string
  /** Last indexing time */
  lastIndexedAt?: Date
}

// =============================================================================
// Mock Embedding Provider
// =============================================================================

/**
 * Create a mock embedding provider for testing
 * Uses simple hash-based "embeddings" - NOT for production use
 */
export function createMockEmbeddingProvider(dimension: number = 384): EmbeddingProvider {
  return {
    name: 'mock',
    dimension,

    async embed(text: string): Promise<EmbeddingVector> {
      // Simple deterministic "embedding" based on text content
      // This is for testing only - NOT actual semantic embeddings
      const vector: number[] = []

      for (let i = 0; i < dimension; i++) {
        // Use characters at different positions modulated by index
        let val = 0
        for (let j = 0; j < Math.min(text.length, 50); j++) {
          val += text.charCodeAt(j % text.length) * Math.sin((i + 1) * (j + 1))
        }
        // Normalize to -1 to 1 range
        vector.push(Math.tanh(val / 1000))
      }

      // Normalize to unit length
      const magnitude = Math.sqrt(vector.reduce((sum, v) => sum + v * v, 0))
      return vector.map((v) => v / (magnitude || 1))
    },

    async embedBatch(texts: string[]): Promise<EmbeddingVector[]> {
      return Promise.all(texts.map((t) => this.embed(t)))
    },
  }
}

// =============================================================================
// Vector Operations
// =============================================================================

/**
 * Calculate cosine similarity between two vectors
 */
export function cosineSimilarity(a: EmbeddingVector, b: EmbeddingVector): number {
  if (a.length !== b.length) {
    throw new Error(`Vector dimension mismatch: ${a.length} vs ${b.length}`)
  }

  let dotProduct = 0
  let magnitudeA = 0
  let magnitudeB = 0

  for (let i = 0; i < a.length; i++) {
    dotProduct += a[i] * b[i]
    magnitudeA += a[i] * a[i]
    magnitudeB += b[i] * b[i]
  }

  const magnitude = Math.sqrt(magnitudeA) * Math.sqrt(magnitudeB)
  if (magnitude === 0) return 0

  return dotProduct / magnitude
}

/**
 * Normalize a vector to unit length
 */
export function normalizeVector(v: EmbeddingVector): EmbeddingVector {
  const magnitude = Math.sqrt(v.reduce((sum, val) => sum + val * val, 0))
  if (magnitude === 0) return v
  return v.map((val) => val / magnitude)
}

// =============================================================================
// Text Chunking
// =============================================================================

/**
 * Split text into overlapping chunks
 */
export function chunkText(text: string, maxChunkSize: number, overlap: number): string[] {
  if (text.length <= maxChunkSize) {
    return [text]
  }

  const chunks: string[] = []
  let start = 0

  while (start < text.length) {
    let end = start + maxChunkSize

    // Try to break at sentence boundary
    if (end < text.length) {
      const lastPeriod = text.lastIndexOf('.', end)
      const lastNewline = text.lastIndexOf('\n', end)
      const breakPoint = Math.max(lastPeriod, lastNewline)

      if (breakPoint > start + maxChunkSize / 2) {
        end = breakPoint + 1
      }
    }

    chunks.push(text.slice(start, end).trim())

    // Ensure we always make forward progress (minimum step of 1 character)
    const nextStart = end - overlap
    start = Math.max(nextStart, start + 1)
  }

  return chunks.filter((c) => c.length > 0)
}

// =============================================================================
// Semantic Index
// =============================================================================

export class SemanticIndex {
  private config: SemanticIndexConfig
  private documents: Map<string, IndexedDocument> = new Map()
  private lastIndexedAt?: Date

  constructor(config: Partial<SemanticIndexConfig> & { provider: EmbeddingProvider }) {
    this.config = {
      provider: config.provider,
      maxChunkSize: config.maxChunkSize ?? 1000,
      chunkOverlap: config.chunkOverlap ?? 100,
      minSimilarity: config.minSimilarity ?? 0.3,
      maxResults: config.maxResults ?? 10,
      log: config.log,
    }
  }

  // ===========================================================================
  // Indexing
  // ===========================================================================

  /**
   * Index a knowledge block
   */
  async indexBlock(block: KnowledgeBlock): Promise<number> {
    // Remove existing entries for this block
    this.removeBlock(block.scope, block.label)

    // Skip empty content
    if (!block.value.trim()) {
      return 0
    }

    // Chunk the content
    const chunks = chunkText(block.value, this.config.maxChunkSize, this.config.chunkOverlap)

    // Generate embeddings
    const embeddings = this.config.provider.embedBatch
      ? await this.config.provider.embedBatch(chunks)
      : await Promise.all(chunks.map((c) => this.config.provider.embed(c)))

    // Create indexed documents
    for (let i = 0; i < chunks.length; i++) {
      const id = `${block.scope}:${block.label}:${i}`

      const doc: IndexedDocument = {
        id,
        source: {
          scope: block.scope,
          label: block.label,
          category: block.category,
        },
        text: chunks[i],
        embedding: embeddings[i],
        chunkIndex: chunks.length > 1 ? i : undefined,
        totalChunks: chunks.length > 1 ? chunks.length : undefined,
        indexedAt: new Date(),
      }

      this.documents.set(id, doc)
    }

    this.lastIndexedAt = new Date()

    if (this.config.log) {
      this.config.log(
        'debug',
        `Indexed ${chunks.length} chunks from ${block.scope}:${block.label}`,
        {
          label: block.label,
          scope: block.scope,
          chunks: chunks.length,
        }
      )
    }

    return chunks.length
  }

  /**
   * Index multiple blocks
   */
  async indexBlocks(blocks: KnowledgeBlock[]): Promise<number> {
    let totalChunks = 0

    for (const block of blocks) {
      totalChunks += await this.indexBlock(block)
    }

    return totalChunks
  }

  /**
   * Remove a block from the index
   */
  removeBlock(scope: KnowledgeScope, label: string): number {
    const prefix = `${scope}:${label}:`
    let removed = 0

    for (const id of this.documents.keys()) {
      if (id.startsWith(prefix)) {
        this.documents.delete(id)
        removed++
      }
    }

    return removed
  }

  /**
   * Clear the entire index
   */
  clear(): void {
    this.documents.clear()
    this.lastIndexedAt = undefined
  }

  // ===========================================================================
  // Search
  // ===========================================================================

  /**
   * Search for similar documents
   */
  async search(
    query: string,
    options?: {
      scope?: KnowledgeScope
      category?: string
      minSimilarity?: number
      maxResults?: number
    }
  ): Promise<SemanticSearchResult[]> {
    // Generate query embedding
    const queryEmbedding = await this.config.provider.embed(query)

    // Calculate similarities
    const results: SemanticSearchResult[] = []

    for (const doc of this.documents.values()) {
      // Apply filters
      if (options?.scope && doc.source.scope !== options.scope) {
        continue
      }
      if (options?.category && doc.source.category !== options.category) {
        continue
      }

      const score = cosineSimilarity(queryEmbedding, doc.embedding)
      const minSim = options?.minSimilarity ?? this.config.minSimilarity

      if (score >= minSim) {
        results.push({ document: doc, score, rank: 0 })
      }
    }

    // Sort by score descending
    results.sort((a, b) => b.score - a.score)

    // Apply max results limit
    const maxRes = options?.maxResults ?? this.config.maxResults
    const limited = results.slice(0, maxRes)

    // Assign ranks
    limited.forEach((r, i) => {
      r.rank = i + 1
    })

    return limited
  }

  /**
   * Find similar blocks (deduped by source)
   */
  async findSimilarBlocks(
    query: string,
    options?: {
      scope?: KnowledgeScope
      category?: string
      maxResults?: number
    }
  ): Promise<
    Array<{
      scope: KnowledgeScope
      label: string
      category: string
      bestScore: number
      matchedChunks: number
    }>
  > {
    // Search with higher limit to account for chunks
    const searchResults = await this.search(query, {
      ...options,
      maxResults: (options?.maxResults ?? this.config.maxResults) * 3,
    })

    // Group by source
    const grouped = new Map<
      string,
      {
        scope: KnowledgeScope
        label: string
        category: string
        bestScore: number
        matchedChunks: number
      }
    >()

    for (const result of searchResults) {
      const key = `${result.document.source.scope}:${result.document.source.label}`
      const existing = grouped.get(key)

      if (existing) {
        existing.bestScore = Math.max(existing.bestScore, result.score)
        existing.matchedChunks++
      } else {
        grouped.set(key, {
          scope: result.document.source.scope,
          label: result.document.source.label,
          category: result.document.source.category,
          bestScore: result.score,
          matchedChunks: 1,
        })
      }
    }

    // Sort by best score descending
    const results = Array.from(grouped.values())
    results.sort((a, b) => b.bestScore - a.bestScore)

    // Apply max results
    return results.slice(0, options?.maxResults ?? this.config.maxResults)
  }

  // ===========================================================================
  // Statistics
  // ===========================================================================

  /**
   * Get index statistics
   */
  getStats(): IndexStats {
    const byScope: Record<KnowledgeScope, number> = {
      project: 0,
      global: 0,
    }
    const byCategory: Record<string, number> = {}
    const uniqueSources = new Set<string>()

    for (const doc of this.documents.values()) {
      byScope[doc.source.scope]++
      byCategory[doc.source.category] = (byCategory[doc.source.category] ?? 0) + 1
      uniqueSources.add(`${doc.source.scope}:${doc.source.label}`)
    }

    return {
      totalDocuments: uniqueSources.size,
      totalChunks: this.documents.size,
      byScope,
      byCategory,
      embeddingDimension: this.config.provider.dimension,
      providerName: this.config.provider.name,
      lastIndexedAt: this.lastIndexedAt,
    }
  }

  /**
   * Check if a block is indexed
   */
  isBlockIndexed(scope: KnowledgeScope, label: string): boolean {
    const prefix = `${scope}:${label}:`
    for (const id of this.documents.keys()) {
      if (id.startsWith(prefix)) {
        return true
      }
    }
    return false
  }

  /**
   * Get all indexed block references
   */
  getIndexedBlocks(): Array<{ scope: KnowledgeScope; label: string }> {
    const seen = new Set<string>()
    const blocks: Array<{ scope: KnowledgeScope; label: string }> = []

    for (const doc of this.documents.values()) {
      const key = `${doc.source.scope}:${doc.source.label}`
      if (!seen.has(key)) {
        seen.add(key)
        blocks.push({
          scope: doc.source.scope,
          label: doc.source.label,
        })
      }
    }

    return blocks
  }
}

// =============================================================================
// Singleton & Factory
// =============================================================================

let defaultIndex: SemanticIndex | null = null

/**
 * Get the default semantic index
 */
export function getSemanticIndex(
  config?: Partial<SemanticIndexConfig> & { provider: EmbeddingProvider }
): SemanticIndex {
  if (!defaultIndex) {
    // Use mock provider if none specified (for development/testing)
    const provider = config?.provider ?? createMockEmbeddingProvider()
    defaultIndex = new SemanticIndex({ ...config, provider })
  }
  return defaultIndex
}

/**
 * Reset the default semantic index (for testing)
 */
export function resetSemanticIndex(): void {
  defaultIndex = null
}

/**
 * Create a new semantic index with the specified provider
 */
export function createSemanticIndex(
  config: Partial<SemanticIndexConfig> & { provider: EmbeddingProvider }
): SemanticIndex {
  return new SemanticIndex(config)
}

// =============================================================================
// Utility Functions
// =============================================================================

/**
 * Describe search results in human-readable format
 */
export function describeSearchResults(results: SemanticSearchResult[]): string {
  if (results.length === 0) {
    return 'No matching results found.'
  }

  const lines: string[] = [`Found ${results.length} matching result(s):\n`]

  for (const result of results) {
    const { document, score, rank } = result
    const chunkInfo = document.totalChunks
      ? ` (chunk ${(document.chunkIndex ?? 0) + 1}/${document.totalChunks})`
      : ''

    lines.push(`${rank}. [${document.source.scope}:${document.source.label}]${chunkInfo}`)
    lines.push(`   Score: ${(score * 100).toFixed(1)}%`)
    lines.push(`   Preview: ${document.text.slice(0, 100).replace(/\n/g, ' ')}...`)
    lines.push('')
  }

  return lines.join('\n')
}
