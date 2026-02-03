/**
 * Episodic Memory
 *
 * Session-based memories that capture what happened during interactions.
 * Records summaries, decisions, outcomes, and tool usage patterns.
 */

import type {
  CreateEpisodicMemoryInput,
  Embedder,
  EpisodicMemory,
  MemoryId,
  SimilarityResult,
  VectorStore,
} from './types.js'
import { DEFAULT_IMPORTANCE } from './types.js'

// ============================================================================
// Episodic Memory Manager
// ============================================================================

/**
 * Manages episodic (session) memories
 */
export class EpisodicMemoryManager {
  constructor(
    private store: VectorStore,
    private embedder: Embedder
  ) {}

  // ==========================================================================
  // Core Operations
  // ==========================================================================

  /**
   * Record a session as episodic memory
   */
  async recordSession(input: CreateEpisodicMemoryInput): Promise<MemoryId> {
    const id = this.generateId()
    const now = Date.now()

    // Build content for embedding
    const content = this.buildContent(input)

    // Generate embedding
    const embedding = await this.embedder.embed(content)

    // Calculate importance based on session characteristics
    const importance = this.calculateImportance(input)

    // Extract tags
    const tags = this.extractTags(input)

    const memory: EpisodicMemory = {
      id,
      type: 'episodic',
      content,
      embedding,
      metadata: {
        timestamp: now,
        importance,
        accessCount: 0,
        lastAccessed: now,
        tags,
        source: input.sessionId,
      },
      sessionId: input.sessionId,
      summary: input.summary,
      decisions: input.decisions,
      toolsUsed: input.toolsUsed,
      outcome: input.outcome,
      durationMinutes: input.durationMinutes,
    }

    await this.store.insert(memory)
    return id
  }

  /**
   * Recall similar past sessions
   */
  async recallSimilar(context: string, limit = 5): Promise<SimilarityResult[]> {
    const embedding = await this.embedder.embed(context)
    return this.store.findSimilar(embedding, limit, 'episodic')
  }

  /**
   * Get episodic memory by ID
   */
  async get(id: MemoryId): Promise<EpisodicMemory | null> {
    const entry = await this.store.get(id)
    if (!entry || entry.type !== 'episodic') return null
    return entry as EpisodicMemory
  }

  /**
   * Update access metadata (called when memory is recalled)
   */
  async recordAccess(id: MemoryId): Promise<void> {
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
   * Get recent sessions
   */
  async getRecent(limit = 10): Promise<EpisodicMemory[]> {
    const entries = await this.store.query({
      type: 'episodic',
      limit,
      orderBy: 'timestamp',
      order: 'desc',
    })
    return entries as EpisodicMemory[]
  }

  /**
   * Get sessions by outcome
   */
  async getByOutcome(outcome: EpisodicMemory['outcome'], limit = 10): Promise<EpisodicMemory[]> {
    const entries = await this.store.query({
      type: 'episodic',
      limit: limit * 2, // Over-fetch then filter
      orderBy: 'timestamp',
      order: 'desc',
    })

    return (entries as EpisodicMemory[]).filter((e) => e.outcome === outcome).slice(0, limit)
  }

  /**
   * Get sessions that used specific tools
   */
  async getByTools(tools: string[], limit = 10): Promise<EpisodicMemory[]> {
    const entries = await this.store.query({
      type: 'episodic',
      limit: limit * 3, // Over-fetch then filter
      orderBy: 'timestamp',
      order: 'desc',
    })

    return (entries as EpisodicMemory[])
      .filter((e) => tools.some((t) => e.toolsUsed.includes(t)))
      .slice(0, limit)
  }

  /**
   * Count total episodic memories
   */
  async count(): Promise<number> {
    return this.store.count('episodic')
  }

  // ==========================================================================
  // Analysis
  // ==========================================================================

  /**
   * Get tool usage statistics from episodic memories
   */
  async getToolStats(): Promise<Map<string, { useCount: number; successRate: number }>> {
    const entries = await this.store.query({ type: 'episodic' })
    const stats = new Map<string, { uses: number; successes: number }>()

    for (const entry of entries as EpisodicMemory[]) {
      const isSuccess = entry.outcome === 'success'

      for (const tool of entry.toolsUsed) {
        const current = stats.get(tool) ?? { uses: 0, successes: 0 }
        current.uses++
        if (isSuccess) current.successes++
        stats.set(tool, current)
      }
    }

    // Convert to final format
    const result = new Map<string, { useCount: number; successRate: number }>()
    for (const [tool, { uses, successes }] of stats) {
      result.set(tool, {
        useCount: uses,
        successRate: uses > 0 ? successes / uses : 0,
      })
    }

    return result
  }

  /**
   * Get common decision patterns
   */
  async getCommonDecisions(limit = 10): Promise<{ decision: string; count: number }[]> {
    const entries = await this.store.query({ type: 'episodic' })
    const decisionCounts = new Map<string, number>()

    for (const entry of entries as EpisodicMemory[]) {
      for (const decision of entry.decisions) {
        const normalized = decision.toLowerCase().trim()
        decisionCounts.set(normalized, (decisionCounts.get(normalized) ?? 0) + 1)
      }
    }

    return Array.from(decisionCounts.entries())
      .sort((a, b) => b[1] - a[1])
      .slice(0, limit)
      .map(([decision, count]) => ({ decision, count }))
  }

  // ==========================================================================
  // Helpers
  // ==========================================================================

  /**
   * Generate unique ID
   */
  private generateId(): MemoryId {
    return `episodic-${Date.now()}-${Math.random().toString(36).slice(2, 8)}`
  }

  /**
   * Build searchable content from input
   */
  private buildContent(input: CreateEpisodicMemoryInput): string {
    const parts = [
      input.summary,
      `Decisions: ${input.decisions.join('; ')}`,
      `Tools: ${input.toolsUsed.join(', ')}`,
      `Outcome: ${input.outcome}`,
    ]
    return parts.join('\n')
  }

  /**
   * Calculate importance based on session characteristics
   */
  private calculateImportance(input: CreateEpisodicMemoryInput): number {
    let importance = DEFAULT_IMPORTANCE

    // Boost for successful outcomes
    if (input.outcome === 'success') {
      importance += 0.1
    } else if (input.outcome === 'failure') {
      // Failures are also important to remember
      importance += 0.05
    }

    // Boost for longer sessions (more substantial work)
    if (input.durationMinutes > 30) {
      importance += 0.1
    } else if (input.durationMinutes > 60) {
      importance += 0.15
    }

    // Boost for sessions with many decisions
    if (input.decisions.length > 3) {
      importance += 0.05
    }

    // Boost for sessions with many tools
    if (input.toolsUsed.length > 5) {
      importance += 0.05
    }

    // Cap at 1.0
    return Math.min(1.0, importance)
  }

  /**
   * Extract tags from input
   */
  private extractTags(input: CreateEpisodicMemoryInput): string[] {
    const tags: string[] = [...(input.tags ?? [])]

    // Add outcome as tag
    tags.push(`outcome:${input.outcome}`)

    // Add common tool categories as tags
    const toolCategories = this.categorizeTools(input.toolsUsed)
    tags.push(...toolCategories)

    // Remove duplicates
    return [...new Set(tags)]
  }

  /**
   * Categorize tools into high-level categories
   */
  private categorizeTools(tools: string[]): string[] {
    const categories: string[] = []

    const fileTools = ['read', 'write', 'create', 'delete', 'glob', 'grep']
    const codeTools = ['lint', 'typecheck', 'test', 'build']
    const gitTools = ['commit', 'push', 'pull', 'branch']

    if (tools.some((t) => fileTools.some((ft) => t.toLowerCase().includes(ft)))) {
      categories.push('category:files')
    }
    if (tools.some((t) => codeTools.some((ct) => t.toLowerCase().includes(ct)))) {
      categories.push('category:code')
    }
    if (tools.some((t) => gitTools.some((gt) => t.toLowerCase().includes(gt)))) {
      categories.push('category:git')
    }
    if (tools.includes('bash') || tools.includes('shell')) {
      categories.push('category:shell')
    }

    return categories
  }
}

// ============================================================================
// Factory
// ============================================================================

/**
 * Create an episodic memory manager
 */
export function createEpisodicMemory(
  store: VectorStore,
  embedder: Embedder
): EpisodicMemoryManager {
  return new EpisodicMemoryManager(store, embedder)
}
