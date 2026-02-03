/**
 * Semantic Memory
 *
 * Fact-based memories that capture learned knowledge and concepts.
 * Includes duplicate detection, confidence tracking, and fact reinforcement.
 */

import type {
  CreateSemanticMemoryInput,
  Embedder,
  MemoryId,
  SemanticMemory,
  SimilarityResult,
  VectorStore,
} from './types.js'
import { DEFAULT_IMPORTANCE, DUPLICATE_SIMILARITY_THRESHOLD } from './types.js'

// ============================================================================
// Semantic Memory Manager
// ============================================================================

/**
 * Manages semantic (fact/knowledge) memories
 */
export class SemanticMemoryManager {
  constructor(
    private store: VectorStore,
    private embedder: Embedder
  ) {}

  // ==========================================================================
  // Core Operations
  // ==========================================================================

  /**
   * Learn a new fact
   * Checks for duplicates and reinforces if similar fact exists
   */
  async learn(input: CreateSemanticMemoryInput): Promise<MemoryId> {
    // Generate embedding for the fact
    const embedding = await this.embedder.embed(input.fact)

    // Check for duplicates
    const similar = await this.store.findSimilar(embedding, 1, 'semantic')

    if (similar.length > 0 && similar[0].similarity >= DUPLICATE_SIMILARITY_THRESHOLD) {
      // Reinforce existing memory instead of creating duplicate
      const existingId = similar[0].memory.id
      await this.reinforce(existingId)

      // Update confidence if new input has higher confidence
      const existing = similar[0].memory as SemanticMemory
      if (input.confidence && input.confidence > existing.confidence) {
        await this.store.update(existingId, {
          metadata: {
            ...existing.metadata,
            confidence: input.confidence,
          },
        })
      }

      return existingId
    }

    // Create new memory
    return this.createMemory(input, embedding)
  }

  /**
   * Query for relevant facts
   */
  async query(question: string, limit = 5): Promise<SimilarityResult[]> {
    const embedding = await this.embedder.embed(question)
    const results = await this.store.findSimilar(embedding, limit, 'semantic')

    // Record access for retrieved memories
    for (const result of results) {
      await this.recordAccess(result.memory.id)
    }

    return results
  }

  /**
   * Get semantic memory by ID
   */
  async get(id: MemoryId): Promise<SemanticMemory | null> {
    const entry = await this.store.get(id)
    if (!entry || entry.type !== 'semantic') return null
    return entry as SemanticMemory
  }

  /**
   * Reinforce a memory (boost importance on access)
   */
  async reinforce(id: MemoryId): Promise<void> {
    const memory = await this.get(id)
    if (!memory) return

    // Boost importance by 10%, max 1.0
    const newImportance = Math.min(1.0, memory.metadata.importance + 0.1)

    await this.store.update(id, {
      metadata: {
        ...memory.metadata,
        importance: newImportance,
        accessCount: memory.metadata.accessCount + 1,
        lastAccessed: Date.now(),
      },
    })
  }

  /**
   * Update confidence for a fact
   */
  async updateConfidence(id: MemoryId, confidence: number): Promise<void> {
    const memory = await this.get(id)
    if (!memory) return

    await this.store.update(id, {
      metadata: {
        ...memory.metadata,
        confidence: Math.max(0, Math.min(1, confidence)),
      },
    })
  }

  /**
   * Add related memory IDs
   */
  async addRelated(id: MemoryId, relatedIds: MemoryId[]): Promise<void> {
    const memory = await this.get(id)
    if (!memory) return

    const existing = memory.relatedIds ?? []
    const combined = [...new Set([...existing, ...relatedIds])]

    // Type assertion needed for the update
    const memoryUpdate = { ...memory, relatedIds: combined } as SemanticMemory
    await this.store.update(id, memoryUpdate)
  }

  /**
   * Count total semantic memories
   */
  async count(): Promise<number> {
    return this.store.count('semantic')
  }

  // ==========================================================================
  // Querying
  // ==========================================================================

  /**
   * Get facts by source
   */
  async getBySource(source: string, limit = 10): Promise<SemanticMemory[]> {
    const entries = await this.store.query({
      type: 'semantic',
      limit: limit * 2, // Over-fetch then filter
      orderBy: 'timestamp',
      order: 'desc',
    })

    return (entries as SemanticMemory[]).filter((e) => e.source === source).slice(0, limit)
  }

  /**
   * Get facts by tag
   */
  async getByTag(tag: string, limit = 10): Promise<SemanticMemory[]> {
    return (await this.store.query({
      type: 'semantic',
      tags: [tag],
      limit,
      orderBy: 'importance',
      order: 'desc',
    })) as SemanticMemory[]
  }

  /**
   * Get high-confidence facts
   */
  async getHighConfidence(minConfidence = 0.8, limit = 10): Promise<SemanticMemory[]> {
    const entries = await this.store.query({
      type: 'semantic',
      limit: limit * 2,
      orderBy: 'importance',
      order: 'desc',
    })

    return (entries as SemanticMemory[])
      .filter((e) => e.confidence >= minConfidence)
      .slice(0, limit)
  }

  /**
   * Get related facts
   */
  async getRelated(id: MemoryId): Promise<SemanticMemory[]> {
    const memory = await this.get(id)
    if (!memory || !memory.relatedIds?.length) return []

    const related: SemanticMemory[] = []
    for (const relatedId of memory.relatedIds) {
      const relatedMemory = await this.get(relatedId)
      if (relatedMemory) {
        related.push(relatedMemory)
      }
    }

    return related
  }

  // ==========================================================================
  // Analysis
  // ==========================================================================

  /**
   * Find contradicting facts
   * Returns pairs of facts that might contradict each other
   */
  async findPotentialContradictions(): Promise<
    { a: SemanticMemory; b: SemanticMemory; similarity: number }[]
  > {
    const entries = await this.store.query({ type: 'semantic' })
    const memories = entries as SemanticMemory[]
    const contradictions: { a: SemanticMemory; b: SemanticMemory; similarity: number }[] = []

    // Compare facts with similar topics but different content
    for (let i = 0; i < memories.length; i++) {
      for (let j = i + 1; j < memories.length; j++) {
        const a = memories[i]
        const b = memories[j]

        // Skip if same source
        if (a.source === b.source) continue

        // Check if embeddings exist
        if (!a.embedding || !b.embedding) continue

        // Calculate similarity
        const similarity = this.cosineSimilarity(a.embedding, b.embedding)

        // High similarity in topic (0.7-0.9) but not exact duplicate
        // could indicate contradicting information
        if (similarity >= 0.7 && similarity < DUPLICATE_SIMILARITY_THRESHOLD) {
          contradictions.push({ a, b, similarity })
        }
      }
    }

    return contradictions.sort((x, y) => y.similarity - x.similarity)
  }

  /**
   * Get fact statistics
   */
  async getStats(): Promise<{
    total: number
    bySource: Map<string, number>
    avgConfidence: number
    avgImportance: number
  }> {
    const entries = await this.store.query({ type: 'semantic' })
    const memories = entries as SemanticMemory[]

    const bySource = new Map<string, number>()
    let totalConfidence = 0
    let totalImportance = 0

    for (const memory of memories) {
      bySource.set(memory.source, (bySource.get(memory.source) ?? 0) + 1)
      totalConfidence += memory.confidence
      totalImportance += memory.metadata.importance
    }

    return {
      total: memories.length,
      bySource,
      avgConfidence: memories.length > 0 ? totalConfidence / memories.length : 0,
      avgImportance: memories.length > 0 ? totalImportance / memories.length : 0,
    }
  }

  // ==========================================================================
  // Helpers
  // ==========================================================================

  /**
   * Create a new semantic memory
   */
  private async createMemory(
    input: CreateSemanticMemoryInput,
    embedding: Float32Array
  ): Promise<MemoryId> {
    const id = this.generateId()
    const now = Date.now()

    // Calculate importance based on confidence
    const confidence = input.confidence ?? DEFAULT_IMPORTANCE
    const importance = DEFAULT_IMPORTANCE + confidence * 0.3

    const memory: SemanticMemory = {
      id,
      type: 'semantic',
      content: input.fact,
      embedding,
      metadata: {
        timestamp: now,
        importance,
        accessCount: 0,
        lastAccessed: now,
        tags: input.tags ?? [],
        source: input.source,
        confidence,
      },
      fact: input.fact,
      source: input.source,
      confidence,
      relatedIds: input.relatedIds,
    }

    await this.store.insert(memory)
    return id
  }

  /**
   * Record memory access
   */
  private async recordAccess(id: MemoryId): Promise<void> {
    const memory = await this.get(id)
    if (!memory) return

    await this.store.update(id, {
      metadata: {
        ...memory.metadata,
        accessCount: memory.metadata.accessCount + 1,
        lastAccessed: Date.now(),
      },
    })
  }

  /**
   * Generate unique ID
   */
  private generateId(): MemoryId {
    return `semantic-${Date.now()}-${Math.random().toString(36).slice(2, 8)}`
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
// Factory
// ============================================================================

/**
 * Create a semantic memory manager
 */
export function createSemanticMemory(
  store: VectorStore,
  embedder: Embedder
): SemanticMemoryManager {
  return new SemanticMemoryManager(store, embedder)
}
