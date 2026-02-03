/**
 * Procedural Memory
 *
 * Pattern-based memories that capture learned behaviors and tool usage.
 * Tracks success rates and suggests actions based on past experiences.
 */

import type {
  CreateProceduralMemoryInput,
  Embedder,
  MemoryId,
  ProceduralMemory,
  SimilarityResult,
  VectorStore,
} from './types.js'
import { DEFAULT_IMPORTANCE, SUCCESS_RATE_THRESHOLD } from './types.js'

// ============================================================================
// Procedural Memory Manager
// ============================================================================

/**
 * Manages procedural (pattern/behavior) memories
 */
export class ProceduralMemoryManager {
  constructor(
    private store: VectorStore,
    private embedder: Embedder
  ) {}

  // ==========================================================================
  // Core Operations
  // ==========================================================================

  /**
   * Record a pattern (context → action with outcome)
   * Updates existing pattern if similar context exists
   */
  async recordPattern(input: CreateProceduralMemoryInput): Promise<MemoryId> {
    // Generate embedding for the context
    const embedding = await this.embedder.embed(input.context)

    // Check for existing similar pattern
    const similar = await this.store.findSimilar(embedding, 1, 'procedural')

    if (similar.length > 0 && similar[0].similarity >= 0.9) {
      // Update existing pattern
      const existing = similar[0].memory as ProceduralMemory

      // Only update if action is the same
      if (this.normalizeAction(existing.action) === this.normalizeAction(input.action)) {
        return this.updatePatternStats(existing.id, input.success)
      }
    }

    // Create new pattern
    return this.createPattern(input, embedding)
  }

  /**
   * Suggest actions for a given context
   * Returns patterns with success rate above threshold
   */
  async suggestAction(context: string, limit = 3): Promise<ProceduralMemory[]> {
    const embedding = await this.embedder.embed(context)
    const similar = await this.store.findSimilar(embedding, limit * 2, 'procedural')

    // Filter by success rate threshold
    const suggestions = (
      similar
        .filter((r) => {
          const memory = r.memory as ProceduralMemory
          return memory.successRate >= SUCCESS_RATE_THRESHOLD
        })
        .map((r) => r.memory) as ProceduralMemory[]
    ).slice(0, limit)

    // Record access for returned suggestions
    for (const suggestion of suggestions) {
      await this.recordAccess(suggestion.id)
    }

    return suggestions
  }

  /**
   * Get procedural memory by ID
   */
  async get(id: MemoryId): Promise<ProceduralMemory | null> {
    const entry = await this.store.get(id)
    if (!entry || entry.type !== 'procedural') return null
    return entry as ProceduralMemory
  }

  /**
   * Record success/failure for an existing pattern
   */
  async recordOutcome(id: MemoryId, success: boolean): Promise<void> {
    await this.updatePatternStats(id, success)
  }

  /**
   * Count total procedural memories
   */
  async count(): Promise<number> {
    return this.store.count('procedural')
  }

  // ==========================================================================
  // Querying
  // ==========================================================================

  /**
   * Get patterns by tool
   */
  async getByTool(tool: string, limit = 10): Promise<ProceduralMemory[]> {
    const entries = await this.store.query({
      type: 'procedural',
      limit: limit * 2,
      orderBy: 'importance',
      order: 'desc',
    })

    return (entries as ProceduralMemory[]).filter((e) => e.tools.includes(tool)).slice(0, limit)
  }

  /**
   * Get most successful patterns
   */
  async getMostSuccessful(minUses = 3, limit = 10): Promise<ProceduralMemory[]> {
    const entries = await this.store.query({
      type: 'procedural',
      limit: limit * 2,
    })

    return (entries as ProceduralMemory[])
      .filter((e) => e.useCount >= minUses)
      .sort((a, b) => b.successRate - a.successRate)
      .slice(0, limit)
  }

  /**
   * Get frequently used patterns
   */
  async getMostUsed(limit = 10): Promise<ProceduralMemory[]> {
    const entries = await this.store.query({
      type: 'procedural',
      limit: limit * 2,
    })

    return (entries as ProceduralMemory[]).sort((a, b) => b.useCount - a.useCount).slice(0, limit)
  }

  /**
   * Find similar contexts
   */
  async findSimilarContexts(context: string, limit = 5): Promise<SimilarityResult[]> {
    const embedding = await this.embedder.embed(context)
    return this.store.findSimilar(embedding, limit, 'procedural')
  }

  /**
   * Get patterns by tag
   */
  async getByTag(tag: string, limit = 10): Promise<ProceduralMemory[]> {
    return (await this.store.query({
      type: 'procedural',
      tags: [tag],
      limit,
      orderBy: 'importance',
      order: 'desc',
    })) as ProceduralMemory[]
  }

  // ==========================================================================
  // Analysis
  // ==========================================================================

  /**
   * Get tool effectiveness statistics
   */
  async getToolEffectiveness(): Promise<
    Map<string, { totalUses: number; avgSuccessRate: number; patterns: number }>
  > {
    const entries = await this.store.query({ type: 'procedural' })
    const memories = entries as ProceduralMemory[]

    const stats = new Map<string, { uses: number; successSum: number; patterns: number }>()

    for (const memory of memories) {
      for (const tool of memory.tools) {
        const current = stats.get(tool) ?? { uses: 0, successSum: 0, patterns: 0 }
        current.uses += memory.useCount
        current.successSum += memory.successRate
        current.patterns++
        stats.set(tool, current)
      }
    }

    // Convert to final format
    const result = new Map<
      string,
      { totalUses: number; avgSuccessRate: number; patterns: number }
    >()
    for (const [tool, { uses, successSum, patterns }] of stats) {
      result.set(tool, {
        totalUses: uses,
        avgSuccessRate: patterns > 0 ? successSum / patterns : 0,
        patterns,
      })
    }

    return result
  }

  /**
   * Get underperforming patterns (low success rate)
   */
  async getUnderperforming(maxSuccessRate = 0.5, minUses = 3): Promise<ProceduralMemory[]> {
    const entries = await this.store.query({ type: 'procedural' })

    return (entries as ProceduralMemory[])
      .filter((e) => e.useCount >= minUses && e.successRate <= maxSuccessRate)
      .sort((a, b) => a.successRate - b.successRate)
  }

  /**
   * Get pattern statistics
   */
  async getStats(): Promise<{
    total: number
    avgSuccessRate: number
    avgUseCount: number
    aboveThreshold: number
    belowThreshold: number
  }> {
    const entries = await this.store.query({ type: 'procedural' })
    const memories = entries as ProceduralMemory[]

    if (memories.length === 0) {
      return {
        total: 0,
        avgSuccessRate: 0,
        avgUseCount: 0,
        aboveThreshold: 0,
        belowThreshold: 0,
      }
    }

    let totalSuccessRate = 0
    let totalUseCount = 0
    let aboveThreshold = 0
    let belowThreshold = 0

    for (const memory of memories) {
      totalSuccessRate += memory.successRate
      totalUseCount += memory.useCount

      if (memory.successRate >= SUCCESS_RATE_THRESHOLD) {
        aboveThreshold++
      } else {
        belowThreshold++
      }
    }

    return {
      total: memories.length,
      avgSuccessRate: totalSuccessRate / memories.length,
      avgUseCount: totalUseCount / memories.length,
      aboveThreshold,
      belowThreshold,
    }
  }

  // ==========================================================================
  // Helpers
  // ==========================================================================

  /**
   * Create a new procedural memory
   */
  private async createPattern(
    input: CreateProceduralMemoryInput,
    embedding: Float32Array
  ): Promise<MemoryId> {
    const id = this.generateId()
    const now = Date.now()

    const memory: ProceduralMemory = {
      id,
      type: 'procedural',
      content: `${input.context} → ${input.action}`,
      embedding,
      metadata: {
        timestamp: now,
        importance: DEFAULT_IMPORTANCE,
        accessCount: 0,
        lastAccessed: now,
        tags: this.generateTags(input),
        successRate: input.success ? 1.0 : 0.0,
      },
      context: input.context,
      action: input.action,
      tools: input.tools,
      useCount: 1,
      successCount: input.success ? 1 : 0,
      successRate: input.success ? 1.0 : 0.0,
    }

    await this.store.insert(memory)
    return id
  }

  /**
   * Update pattern statistics after use
   */
  private async updatePatternStats(id: MemoryId, success: boolean): Promise<MemoryId> {
    const memory = await this.get(id)
    if (!memory) {
      throw new Error(`Procedural memory not found: ${id}`)
    }

    const newUseCount = memory.useCount + 1
    const newSuccessCount = memory.successCount + (success ? 1 : 0)
    const newSuccessRate = newSuccessCount / newUseCount

    // Adjust importance based on success rate
    let newImportance = memory.metadata.importance
    if (success) {
      newImportance = Math.min(1.0, newImportance + 0.02)
    } else {
      newImportance = Math.max(0.1, newImportance - 0.01)
    }

    await this.store.update(id, {
      metadata: {
        ...memory.metadata,
        importance: newImportance,
        accessCount: memory.metadata.accessCount + 1,
        lastAccessed: Date.now(),
        successRate: newSuccessRate,
      },
      useCount: newUseCount,
      successCount: newSuccessCount,
      successRate: newSuccessRate,
    } as Partial<ProceduralMemory>)

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
    return `procedural-${Date.now()}-${Math.random().toString(36).slice(2, 8)}`
  }

  /**
   * Normalize action string for comparison
   */
  private normalizeAction(action: string): string {
    return action.toLowerCase().trim()
  }

  /**
   * Generate tags from input
   */
  private generateTags(input: CreateProceduralMemoryInput): string[] {
    const tags: string[] = [...(input.tags ?? [])]

    // Add tool tags
    for (const tool of input.tools) {
      tags.push(`tool:${tool.toLowerCase()}`)
    }

    return [...new Set(tags)]
  }
}

// ============================================================================
// Factory
// ============================================================================

/**
 * Create a procedural memory manager
 */
export function createProceduralMemory(
  store: VectorStore,
  embedder: Embedder
): ProceduralMemoryManager {
  return new ProceduralMemoryManager(store, embedder)
}
