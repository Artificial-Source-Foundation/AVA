/**
 * Delta9 Learning Engine
 *
 * Core learning system with:
 * - Outcome tracking and analysis
 * - Confidence decay (90-day half-life)
 * - Anti-pattern detection (60% failure threshold)
 * - Pattern management
 *
 * Inspired by swarm-plugin's learning.ts
 */

import { existsSync, readFileSync, writeFileSync, mkdirSync } from 'node:fs'
import { dirname, join } from 'node:path'
import { nanoid } from 'nanoid'
import {
  Pattern,
  PatternCategory,
  PatternSource,
  Outcome,
  LearningConfig,
  LearningConfigSchema,
  LearningStore,
  LearningStoreSchema,
} from './types.js'
import { getEventStore } from '../events/store.js'

// =============================================================================
// Constants
// =============================================================================

const MAX_OUTCOMES_STORED = 100

// =============================================================================
// Learning Engine Class
// =============================================================================

export class LearningEngine {
  private config: LearningConfig
  private store: LearningStore
  private storePath: string
  private dirty: boolean = false

  constructor(config?: Partial<LearningConfig>, baseDir?: string) {
    this.config = LearningConfigSchema.parse(config || {})
    this.storePath = join(baseDir || process.cwd(), this.config.storagePath)
    this.store = this.loadStore()
  }

  // ===========================================================================
  // Pattern Management
  // ===========================================================================

  /**
   * Learn a new pattern or update existing
   */
  learnPattern(
    pattern: string,
    category: PatternCategory,
    context: string,
    source: PatternSource,
    options: {
      confidence?: number
      relatedFiles?: string[]
      relatedAgents?: string[]
      tags?: string[]
    } = {}
  ): Pattern {
    const now = new Date().toISOString()
    const confidence = options.confidence ?? 0.5

    // Check if pattern already exists
    const existing = this.store.patterns.find(
      (p) => p.pattern === pattern && p.category === category
    )

    if (existing) {
      // Update existing pattern
      existing.baseConfidence = Math.max(existing.baseConfidence, confidence)
      existing.currentConfidence = this.calculateDecayedConfidence(
        existing.baseConfidence,
        new Date(existing.lastUpdatedAt)
      )
      existing.lastUpdatedAt = now
      if (options.relatedFiles) {
        existing.relatedFiles = [...new Set([...(existing.relatedFiles || []), ...options.relatedFiles])]
      }
      if (options.relatedAgents) {
        existing.relatedAgents = [...new Set([...(existing.relatedAgents || []), ...options.relatedAgents])]
      }
      if (options.tags) {
        existing.tags = [...new Set([...(existing.tags || []), ...options.tags])]
      }

      this.markDirty()
      this.emitPatternEvent(existing, 'updated')
      return existing
    }

    // Create new pattern
    const newPattern: Pattern = {
      id: nanoid(12),
      pattern,
      category,
      context,
      baseConfidence: confidence,
      currentConfidence: confidence,
      source,
      applications: 0,
      successes: 0,
      failures: 0,
      createdAt: now,
      lastUpdatedAt: now,
      isAntiPattern: false,
      relatedFiles: options.relatedFiles,
      relatedAgents: options.relatedAgents,
      tags: options.tags,
    }

    this.store.patterns.push(newPattern)
    this.store.stats.totalPatterns++
    this.markDirty()
    this.emitPatternEvent(newPattern, 'learned')

    return newPattern
  }

  /**
   * Record pattern application result
   */
  applyPattern(patternId: string, success: boolean, taskId?: string): void {
    const pattern = this.store.patterns.find((p) => p.id === patternId)
    if (!pattern) return

    pattern.applications++
    if (success) {
      pattern.successes++
    } else {
      pattern.failures++
    }
    pattern.lastAppliedAt = new Date().toISOString()

    // Check for anti-pattern promotion
    this.checkAntiPattern(pattern)

    this.markDirty()

    // Emit event
    const eventStore = getEventStore()
    eventStore.append('learning.pattern_applied', {
      pattern: pattern.pattern,
      taskId: taskId || 'unknown',
      success,
    })
  }

  /**
   * Get pattern by ID
   */
  getPattern(patternId: string): Pattern | undefined {
    return this.store.patterns.find((p) => p.id === patternId)
  }

  /**
   * Get patterns by category
   */
  getPatternsByCategory(category: PatternCategory): Pattern[] {
    return this.store.patterns
      .filter((p) => p.category === category && !p.isAntiPattern)
      .sort((a, b) => b.currentConfidence - a.currentConfidence)
  }

  /**
   * Get patterns for specific files
   */
  getPatternsForFiles(files: string[]): Pattern[] {
    return this.store.patterns.filter(
      (p) =>
        p.category === 'file' &&
        p.relatedFiles?.some((f) => files.some((file) => file.includes(f) || f.includes(file)))
    )
  }

  /**
   * Get all anti-patterns
   */
  getAntiPatterns(): Pattern[] {
    return this.store.patterns.filter((p) => p.isAntiPattern)
  }

  /**
   * Get top patterns for prompt injection
   */
  getTopPatterns(limit: number = 5, category?: PatternCategory): Pattern[] {
    let patterns = this.store.patterns.filter((p) => !p.isAntiPattern)

    if (category) {
      patterns = patterns.filter((p) => p.category === category)
    }

    // Apply decay to all patterns
    this.applyDecay()

    return patterns
      .sort((a, b) => b.currentConfidence - a.currentConfidence)
      .slice(0, limit)
  }

  // ===========================================================================
  // Outcome Tracking
  // ===========================================================================

  /**
   * Record a task outcome
   */
  recordOutcome(outcome: Omit<Outcome, 'id' | 'timestamp'>): Outcome {
    const fullOutcome: Outcome = {
      ...outcome,
      id: nanoid(12),
      timestamp: new Date().toISOString(),
    }

    // Add to store (keep last N outcomes)
    this.store.outcomes.push(fullOutcome)
    if (this.store.outcomes.length > MAX_OUTCOMES_STORED) {
      this.store.outcomes = this.store.outcomes.slice(-MAX_OUTCOMES_STORED)
    }

    // Update stats
    this.store.stats.totalOutcomes++
    this.updateSuccessRate()

    // Update patterns that were applied
    for (const patternId of outcome.patternsApplied) {
      this.applyPattern(patternId, outcome.success, outcome.taskId)
    }

    // Learn from files if task failed
    if (!fullOutcome.success && fullOutcome.filesChanged.length > 0) {
      this.learnFromFailure(fullOutcome)
    }

    // Learn strategy pattern if provided
    if (outcome.strategy) {
      this.updateStrategyPattern(outcome.strategy, outcome.success)
    }

    this.markDirty()
    return fullOutcome
  }

  /**
   * Get recent outcomes
   */
  getRecentOutcomes(limit: number = 10): Outcome[] {
    return this.store.outcomes.slice(-limit).reverse()
  }

  /**
   * Get outcomes for a specific task
   */
  getOutcomesForTask(taskId: string): Outcome[] {
    return this.store.outcomes.filter((o) => o.taskId === taskId)
  }

  /**
   * Get outcomes for specific agent
   */
  getOutcomesForAgent(agent: string): Outcome[] {
    return this.store.outcomes.filter((o) => o.agent === agent)
  }

  /**
   * Get success rate for an agent
   */
  getAgentSuccessRate(agent: string): number {
    const outcomes = this.getOutcomesForAgent(agent)
    if (outcomes.length === 0) return 0
    return outcomes.filter((o) => o.success).length / outcomes.length
  }

  // ===========================================================================
  // Confidence Decay
  // ===========================================================================

  /**
   * Calculate decayed confidence score
   * Uses exponential decay with configurable half-life
   */
  calculateDecayedConfidence(baseConfidence: number, lastUpdate: Date): number {
    const now = new Date()
    const daysSinceUpdate = (now.getTime() - lastUpdate.getTime()) / (1000 * 60 * 60 * 24)
    const halfLife = this.config.halfLifeDays

    // Exponential decay: C(t) = C0 * 0.5^(t/halfLife)
    const decayFactor = Math.pow(0.5, daysSinceUpdate / halfLife)
    return baseConfidence * decayFactor
  }

  /**
   * Apply decay to all patterns
   */
  applyDecay(): void {
    const now = new Date()
    const lastDecay = this.store.lastDecayAt ? new Date(this.store.lastDecayAt) : null

    // Only apply decay once per day
    if (lastDecay && now.getTime() - lastDecay.getTime() < 24 * 60 * 60 * 1000) {
      return
    }

    for (const pattern of this.store.patterns) {
      pattern.currentConfidence = this.calculateDecayedConfidence(
        pattern.baseConfidence,
        new Date(pattern.lastUpdatedAt)
      )
    }

    this.store.lastDecayAt = now.toISOString()
    this.markDirty()
  }

  /**
   * Refresh a pattern's confidence (resets decay)
   */
  refreshPattern(patternId: string): void {
    const pattern = this.store.patterns.find((p) => p.id === patternId)
    if (!pattern) return

    pattern.lastUpdatedAt = new Date().toISOString()
    pattern.currentConfidence = pattern.baseConfidence
    this.markDirty()
  }

  // ===========================================================================
  // Anti-Pattern Detection
  // ===========================================================================

  /**
   * Check if a pattern should be marked as anti-pattern
   */
  private checkAntiPattern(pattern: Pattern): void {
    if (pattern.isAntiPattern) return
    if (pattern.applications < this.config.minApplicationsForAntiPattern) return

    const failureRate = pattern.failures / pattern.applications

    if (failureRate >= this.config.antiPatternThreshold) {
      pattern.isAntiPattern = true
      this.store.stats.totalAntiPatterns++

      // Emit event
      const eventStore = getEventStore()
      eventStore.append('learning.anti_pattern_detected', {
        pattern: pattern.pattern,
        failureRate,
        occurrences: pattern.applications,
      })
    }
  }

  /**
   * Manually mark a pattern as anti-pattern
   */
  markAsAntiPattern(patternId: string): void {
    const pattern = this.store.patterns.find((p) => p.id === patternId)
    if (!pattern || pattern.isAntiPattern) return

    pattern.isAntiPattern = true
    this.store.stats.totalAntiPatterns++
    this.markDirty()
  }

  /**
   * Rehabilitate an anti-pattern (give it another chance)
   */
  rehabilitatePattern(patternId: string): void {
    const pattern = this.store.patterns.find((p) => p.id === patternId)
    if (!pattern || !pattern.isAntiPattern) return

    pattern.isAntiPattern = false
    pattern.applications = 0
    pattern.successes = 0
    pattern.failures = 0
    pattern.lastUpdatedAt = new Date().toISOString()
    this.store.stats.totalAntiPatterns--
    this.markDirty()
  }

  // ===========================================================================
  // Learning from Failures
  // ===========================================================================

  /**
   * Learn patterns from a failed outcome
   */
  private learnFromFailure(outcome: Outcome): void {
    // Learn file-specific patterns
    for (const file of outcome.filesChanged) {
      const existingPattern = this.store.patterns.find(
        (p) => p.category === 'file' && p.relatedFiles?.includes(file)
      )

      if (existingPattern) {
        // Update existing file pattern
        existingPattern.failures++
        existingPattern.applications++
        this.checkAntiPattern(existingPattern)
      } else {
        // Create new file pattern
        this.learnPattern(
          `File ${file} may have issues`,
          'file',
          `Failure context: ${outcome.error || 'unknown'}`,
          'failure',
          {
            confidence: 0.3,
            relatedFiles: [file],
            relatedAgents: [outcome.agent],
            tags: outcome.errorCode ? [outcome.errorCode] : undefined,
          }
        )
      }
    }

    // Learn error pattern
    if (outcome.errorCode) {
      this.learnPattern(
        `Error: ${outcome.errorCode}`,
        'error',
        outcome.error || 'Unknown error',
        'failure',
        {
          confidence: 0.4,
          relatedAgents: [outcome.agent],
          tags: [outcome.errorCode],
        }
      )
    }
  }

  /**
   * Update strategy pattern based on outcome
   */
  private updateStrategyPattern(strategy: string, success: boolean): void {
    const existing = this.store.patterns.find(
      (p) => p.category === 'strategy' && p.pattern === strategy
    )

    if (existing) {
      existing.applications++
      if (success) {
        existing.successes++
        // Boost confidence on success
        existing.baseConfidence = Math.min(1, existing.baseConfidence + 0.05)
      } else {
        existing.failures++
        // Reduce confidence on failure
        existing.baseConfidence = Math.max(0, existing.baseConfidence - 0.03)
      }
      existing.currentConfidence = existing.baseConfidence
      existing.lastUpdatedAt = new Date().toISOString()
      this.checkAntiPattern(existing)
    } else {
      // Create new strategy pattern and count this as first application
      const pattern = this.learnPattern(strategy, 'strategy', `Decomposition strategy: ${strategy}`, 'inferred', {
        confidence: success ? 0.6 : 0.4,
      })
      pattern.applications = 1
      if (success) {
        pattern.successes = 1
      } else {
        pattern.failures = 1
      }
    }

    this.markDirty()
  }

  // ===========================================================================
  // Statistics
  // ===========================================================================

  /**
   * Get learning statistics
   */
  getStats(): LearningStore['stats'] {
    return { ...this.store.stats }
  }

  /**
   * Update success rate in stats
   */
  private updateSuccessRate(): void {
    const outcomes = this.store.outcomes
    if (outcomes.length === 0) {
      this.store.stats.successRate = 0
      return
    }
    this.store.stats.successRate = outcomes.filter((o) => o.success).length / outcomes.length
  }

  /**
   * Get strategy success rates
   */
  getStrategySuccessRates(): Array<{ strategy: string; successRate: number; applications: number }> {
    const strategyPatterns = this.store.patterns.filter((p) => p.category === 'strategy')

    return strategyPatterns
      .map((p) => ({
        strategy: p.pattern,
        successRate: p.applications > 0 ? p.successes / p.applications : 0,
        applications: p.applications,
      }))
      .sort((a, b) => b.successRate - a.successRate)
  }

  // ===========================================================================
  // Persistence
  // ===========================================================================

  private loadStore(): LearningStore {
    if (existsSync(this.storePath)) {
      try {
        const content = readFileSync(this.storePath, 'utf-8')
        return LearningStoreSchema.parse(JSON.parse(content))
      } catch {
        // Invalid store, start fresh
      }
    }

    return {
      version: 1,
      patterns: [],
      outcomes: [],
      stats: {
        totalPatterns: 0,
        totalAntiPatterns: 0,
        totalOutcomes: 0,
        successRate: 0,
      },
    }
  }

  private saveStore(): void {
    if (!this.dirty) return

    try {
      const dir = dirname(this.storePath)
      if (!existsSync(dir)) {
        mkdirSync(dir, { recursive: true })
      }
      writeFileSync(this.storePath, JSON.stringify(this.store, null, 2))
      this.dirty = false
    } catch {
      // Save error, will retry next time
    }
  }

  private markDirty(): void {
    this.dirty = true
    // Auto-save after a delay
    setTimeout(() => this.saveStore(), 1000)
  }

  /**
   * Force save
   */
  save(): void {
    this.dirty = true
    this.saveStore()
  }

  /**
   * Clear all learning data (for testing)
   */
  clear(): void {
    this.store = {
      version: 1,
      patterns: [],
      outcomes: [],
      stats: {
        totalPatterns: 0,
        totalAntiPatterns: 0,
        totalOutcomes: 0,
        successRate: 0,
      },
    }
    this.markDirty()
  }

  // ===========================================================================
  // Event Emission
  // ===========================================================================

  private emitPatternEvent(pattern: Pattern, action: 'learned' | 'updated'): void {
    const eventStore = getEventStore()

    if (action === 'learned') {
      eventStore.append('learning.pattern_learned', {
        pattern: pattern.pattern,
        context: pattern.context,
        confidence: pattern.currentConfidence,
        source: pattern.source,
      })
    }
  }
}

// =============================================================================
// Singleton Instance
// =============================================================================

let defaultEngine: LearningEngine | null = null

/**
 * Get the default learning engine instance
 */
export function getLearningEngine(config?: Partial<LearningConfig>): LearningEngine {
  if (!defaultEngine) {
    defaultEngine = new LearningEngine(config)
  }
  return defaultEngine
}

/**
 * Reset the default learning engine (for testing)
 */
export function resetLearningEngine(): void {
  defaultEngine = null
}
