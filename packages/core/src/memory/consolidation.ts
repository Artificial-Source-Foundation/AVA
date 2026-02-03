/**
 * Memory Consolidation
 *
 * Memory decay, merging, and promotion utilities.
 * Implements exponential decay formula for memory importance.
 */

import type { ConsolidationResult, MemoryEntry, VectorStore } from './types.js'
import { DUPLICATE_SIMILARITY_THRESHOLD, MIN_IMPORTANCE_THRESHOLD } from './types.js'

// ============================================================================
// Constants
// ============================================================================

/** Default decay rate (lambda) for exponential decay */
export const DEFAULT_DECAY_RATE = 0.001

/** Promotion boost amount */
const PROMOTION_BOOST = 0.1

/** Access count threshold for promotion */
const PROMOTION_ACCESS_THRESHOLD = 5

// ============================================================================
// Consolidation Engine
// ============================================================================

/**
 * Handles memory consolidation (decay, merge, promote)
 */
export class ConsolidationEngine {
  private decayRate: number

  constructor(
    private store: VectorStore,
    options?: { decayRate?: number }
  ) {
    this.decayRate = options?.decayRate ?? DEFAULT_DECAY_RATE
  }

  /**
   * Run full consolidation cycle
   */
  async consolidate(): Promise<ConsolidationResult> {
    // 1. Decay old, low-importance memories
    const decayed = await this.decayOldMemories()

    // 2. Merge similar semantic memories
    const merged = await this.mergeSimilarFacts()

    // 3. Promote frequently accessed memories
    const promoted = await this.promoteActiveMemories()

    // 4. Get remaining count
    const totalRemaining = await this.store.count()

    return {
      decayed,
      merged,
      promoted,
      totalRemaining,
    }
  }

  // ==========================================================================
  // Decay
  // ==========================================================================

  /**
   * Apply exponential decay to old memories
   * Formula: importance(t) = importance(0) × e^(-λt)
   *
   * @returns Number of memories removed due to decay
   */
  async decayOldMemories(): Promise<number> {
    const now = Date.now()
    const entries = await this.store.query({})
    let removed = 0

    for (const entry of entries) {
      const ageMs = now - entry.metadata.timestamp
      const ageHours = ageMs / (1000 * 60 * 60)

      // Apply decay formula
      const decayFactor = Math.exp(-this.decayRate * ageHours)
      let newImportance = entry.metadata.importance * decayFactor

      // Boost from recent access (counters decay)
      const lastAccessAgeHours = (now - entry.metadata.lastAccessed) / (1000 * 60 * 60)
      if (lastAccessAgeHours < 24) {
        // Recently accessed - reduce decay effect
        newImportance += (entry.metadata.importance - newImportance) * 0.5
      }

      // Additional boost from access count
      const accessBoost = Math.min(0.2, entry.metadata.accessCount * 0.01)
      newImportance += accessBoost

      // Check if below threshold
      if (newImportance < MIN_IMPORTANCE_THRESHOLD) {
        await this.store.delete(entry.id)
        removed++
      } else if (newImportance !== entry.metadata.importance) {
        // Update importance
        await this.store.update(entry.id, {
          metadata: {
            ...entry.metadata,
            importance: newImportance,
          },
        })
      }
    }

    return removed
  }

  // ==========================================================================
  // Merge
  // ==========================================================================

  /**
   * Merge similar semantic memories
   *
   * @returns Number of memories merged (removed duplicates)
   */
  async mergeSimilarFacts(): Promise<number> {
    const entries = await this.store.query({ type: 'semantic' })
    const processed = new Set<string>()
    let merged = 0

    for (const entry of entries) {
      if (processed.has(entry.id) || !entry.embedding) continue

      // Find similar memories
      const similar = await this.store.findSimilar(entry.embedding, 5, 'semantic')

      for (const result of similar) {
        if (result.memory.id === entry.id) continue
        if (processed.has(result.memory.id)) continue
        if (result.similarity < DUPLICATE_SIMILARITY_THRESHOLD) continue

        // Found a duplicate - merge into the more important one
        const keep =
          entry.metadata.importance >= result.memory.metadata.importance ? entry : result.memory
        const remove = keep === entry ? result.memory : entry

        // Transfer access count and boost importance
        const combinedAccessCount = keep.metadata.accessCount + remove.metadata.accessCount
        const boostedImportance = Math.min(1.0, keep.metadata.importance + 0.05)

        await this.store.update(keep.id, {
          metadata: {
            ...keep.metadata,
            accessCount: combinedAccessCount,
            importance: boostedImportance,
          },
        })

        // Delete the duplicate
        await this.store.delete(remove.id)
        processed.add(remove.id)
        merged++
      }

      processed.add(entry.id)
    }

    return merged
  }

  // ==========================================================================
  // Promote
  // ==========================================================================

  /**
   * Promote frequently accessed memories
   *
   * @returns Number of memories promoted
   */
  async promoteActiveMemories(): Promise<number> {
    const entries = await this.store.query({})
    let promoted = 0

    for (const entry of entries) {
      // Check if memory qualifies for promotion
      if (entry.metadata.accessCount >= PROMOTION_ACCESS_THRESHOLD) {
        const newImportance = Math.min(1.0, entry.metadata.importance + PROMOTION_BOOST)

        if (newImportance > entry.metadata.importance) {
          await this.store.update(entry.id, {
            metadata: {
              ...entry.metadata,
              importance: newImportance,
              // Reset access count after promotion
              accessCount: 0,
            },
          })
          promoted++
        }
      }
    }

    return promoted
  }

  // ==========================================================================
  // Utilities
  // ==========================================================================

  /**
   * Calculate current decay factor for a memory
   */
  calculateDecayFactor(entry: MemoryEntry): number {
    const now = Date.now()
    const ageMs = now - entry.metadata.timestamp
    const ageHours = ageMs / (1000 * 60 * 60)
    return Math.exp(-this.decayRate * ageHours)
  }

  /**
   * Estimate time until a memory would be removed (in hours)
   */
  estimateTimeToRemoval(entry: MemoryEntry): number {
    if (entry.metadata.importance <= MIN_IMPORTANCE_THRESHOLD) {
      return 0
    }

    // Solve for t: MIN_THRESHOLD = importance × e^(-λt)
    // t = -ln(MIN_THRESHOLD / importance) / λ
    const ratio = MIN_IMPORTANCE_THRESHOLD / entry.metadata.importance
    const hours = -Math.log(ratio) / this.decayRate

    return Math.max(0, hours)
  }

  /**
   * Get memories that will decay below threshold soon
   */
  async getAtRiskMemories(hoursUntilRemoval = 24): Promise<MemoryEntry[]> {
    const entries = await this.store.query({})

    return entries.filter((entry) => {
      const timeToRemoval = this.estimateTimeToRemoval(entry)
      return timeToRemoval > 0 && timeToRemoval < hoursUntilRemoval
    })
  }

  /**
   * Set decay rate
   */
  setDecayRate(rate: number): void {
    this.decayRate = Math.max(0, Math.min(1, rate))
  }

  /**
   * Get current decay rate
   */
  getDecayRate(): number {
    return this.decayRate
  }
}

// ============================================================================
// Factory
// ============================================================================

/**
 * Create a consolidation engine
 */
export function createConsolidationEngine(
  store: VectorStore,
  options?: { decayRate?: number }
): ConsolidationEngine {
  return new ConsolidationEngine(store, options)
}
