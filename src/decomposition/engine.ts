/**
 * Delta9 Task Decomposition Engine
 *
 * Breaks complex tasks into subtasks with:
 * - Multiple decomposition strategies
 * - Automatic strategy selection
 * - Historical search for similar tasks
 * - Outcome recording for learning
 */

import { existsSync, mkdirSync, readFileSync, appendFileSync } from 'node:fs'
import { join, dirname } from 'node:path'
import { nanoid } from 'nanoid'
import {
  type Decomposition,
  type Subtask,
  type DecompositionStrategy,
  type DecompositionComplexity,
  type SubtaskComplexity,
  type DecompositionEngineConfig,
  type DecompositionRecord,
  type SimilarTask,
  type DecomposeResult,
  type ValidateResult,
  type SearchResult,
  type DecompositionEvent,
  type DecompositionEventListener,
  DecompositionRecordSchema,
  STRATEGY_DESCRIPTIONS,
  DEFAULT_DECOMPOSITION_CONFIG,
} from './types.js'
import { DecompositionValidator } from './validator.js'

// =============================================================================
// Decomposition Engine Class
// =============================================================================

export class DecompositionEngine {
  private config: Required<DecompositionEngineConfig>
  private validator: DecompositionValidator
  private storagePath: string
  private records: DecompositionRecord[] = []
  private eventListeners: Set<DecompositionEventListener> = new Set()

  constructor(config?: DecompositionEngineConfig) {
    // Merge config with defaults, ensuring baseDir is always defined
    // (process.cwd() may be undefined at module load time in some environments)
    const baseDir = config?.baseDir || process.cwd() || '.'
    this.config = {
      ...DEFAULT_DECOMPOSITION_CONFIG,
      ...config,
      baseDir,
    }
    this.validator = new DecompositionValidator(this.config)
    this.storagePath = join(this.config.baseDir, this.config.storagePath)

    if (this.config.enableHistoricalSearch) {
      this.loadHistory()
    }
  }

  // ===========================================================================
  // Decomposition
  // ===========================================================================

  /**
   * Create a decomposition for a task
   */
  decompose(
    taskId: string,
    taskDescription: string,
    options: {
      strategy?: DecompositionStrategy
      subtasks: Subtask[]
      missionId?: string
      context?: Record<string, unknown>
      useHistory?: boolean
    }
  ): DecomposeResult {
    const { strategy, subtasks, missionId, context, useHistory = true } = options

    // Auto-select strategy if not provided
    const selectedStrategy = strategy ?? this.selectStrategy(taskDescription, subtasks)

    // Search for similar tasks if enabled
    let similarTasks: SimilarTask[] = []
    if (useHistory && this.config.enableHistoricalSearch) {
      const searchResult = this.searchSimilarTasks(taskDescription, 3)
      if (searchResult.success) {
        similarTasks = searchResult.similar
      }
    }

    // Calculate overall complexity
    const totalComplexity = this.calculateTotalComplexity(subtasks)

    // Assign IDs and order if not present
    const processedSubtasks = subtasks.map((s, index) => ({
      ...s,
      id: s.id || nanoid(8),
      order: s.order ?? index + 1,
    }))

    // Create decomposition
    const decomposition: Decomposition = {
      id: nanoid(12),
      parentTaskId: taskId,
      taskDescription,
      strategy: selectedStrategy,
      subtasks: processedSubtasks,
      totalEstimatedComplexity: totalComplexity,
      createdAt: new Date().toISOString(),
      missionId,
      context: {
        ...context,
        similarTasks: similarTasks.map((t) => ({
          taskId: t.taskId,
          similarity: t.similarity,
          strategy: t.strategy,
          success: t.success,
        })),
      },
    }

    // Validate decomposition
    const quality = this.validator.validate(decomposition)

    // Update decomposition with validation results
    decomposition.validatedAt = new Date().toISOString()
    decomposition.validationScore = quality.score
    decomposition.validationIssues = quality.issues.map(
      (i) => `[${i.severity}] ${i.type}: ${i.message}`
    )

    // Store in history
    this.storeRecord({
      decomposition,
      recordedAt: new Date().toISOString(),
    })

    // Emit event
    this.emit({
      type: 'created',
      decompositionId: decomposition.id,
      parentTaskId: taskId,
      timestamp: new Date(),
      strategy: selectedStrategy,
      subtaskCount: subtasks.length,
      quality: quality.score,
    })

    return {
      success: true,
      decomposition,
      quality,
    }
  }

  /**
   * Select the best strategy for a task
   */
  selectStrategy(taskDescription: string, subtasks?: Subtask[]): DecompositionStrategy {
    const desc = taskDescription.toLowerCase()

    // Check for explicit hints in description
    if (desc.includes('test') && (desc.includes('tdd') || desc.includes('test first'))) {
      return 'test_first'
    }

    if (desc.includes('refactor') || desc.includes('incremental')) {
      return 'incremental'
    }

    if (
      desc.includes('full stack') ||
      desc.includes('frontend') ||
      desc.includes('backend') ||
      desc.includes('database')
    ) {
      return 'layer_based'
    }

    if (desc.includes('feature') || desc.includes('functionality')) {
      return 'feature_based'
    }

    // Check subtasks for file patterns
    if (subtasks && subtasks.length > 0) {
      const filesCount = subtasks.filter((s) => s.files && s.files.length > 0).length
      if (filesCount > subtasks.length / 2) {
        return 'file_based'
      }
    }

    // Check historical success rates
    if (this.config.enableHistoricalSearch && this.records.length > 0) {
      const strategyStats = this.getStrategyStats()
      const bestStrategy = Object.entries(strategyStats).sort(
        ([, a], [, b]) => b.successRate - a.successRate
      )[0]
      if (bestStrategy && bestStrategy[1].successRate > 0.7) {
        return bestStrategy[0] as DecompositionStrategy
      }
    }

    // Default to feature-based
    return 'feature_based'
  }

  /**
   * Re-decompose with a different strategy
   */
  redecompose(
    decompositionId: string,
    newStrategy: DecompositionStrategy,
    newSubtasks?: Subtask[]
  ): DecomposeResult {
    // Find original decomposition
    const record = this.records.find((r) => r.decomposition.id === decompositionId)
    if (!record) {
      return {
        success: false,
        error: `Decomposition ${decompositionId} not found`,
      }
    }

    const original = record.decomposition

    // Create new decomposition
    return this.decompose(original.parentTaskId, original.taskDescription, {
      strategy: newStrategy,
      subtasks: newSubtasks ?? original.subtasks,
      missionId: original.missionId,
      context: {
        ...original.context,
        previousDecompositionId: decompositionId,
        previousStrategy: original.strategy,
      },
    })
  }

  // ===========================================================================
  // Validation
  // ===========================================================================

  /**
   * Validate a decomposition
   */
  validate(decomposition: Decomposition): ValidateResult {
    const quality = this.validator.validate(decomposition)

    // Emit event
    this.emit({
      type: 'validated',
      decompositionId: decomposition.id,
      parentTaskId: decomposition.parentTaskId,
      timestamp: new Date(),
      quality: quality.score,
    })

    return {
      success: true,
      quality,
    }
  }

  // ===========================================================================
  // Outcome Recording
  // ===========================================================================

  /**
   * Record the outcome of a decomposition execution
   */
  recordOutcome(decompositionId: string, success: boolean, duration?: number): boolean {
    const record = this.records.find((r) => r.decomposition.id === decompositionId)
    if (!record) {
      return false
    }

    record.success = success
    record.duration = duration

    // Update storage
    this.saveHistory()

    // Emit event
    this.emit({
      type: 'outcome_recorded',
      decompositionId,
      parentTaskId: record.decomposition.parentTaskId,
      timestamp: new Date(),
      success,
    })

    return true
  }

  // ===========================================================================
  // Historical Search
  // ===========================================================================

  /**
   * Search for similar tasks in history
   */
  searchSimilarTasks(description: string, limit: number = 5): SearchResult {
    if (!this.config.enableHistoricalSearch || this.records.length === 0) {
      return { success: true, similar: [] }
    }

    const similar: SimilarTask[] = []

    for (const record of this.records) {
      const similarity = this.calculateSimilarity(description, record.decomposition.taskDescription)

      if (similarity > 0.3) {
        // Threshold for relevance
        similar.push({
          taskId: record.decomposition.parentTaskId,
          description: record.decomposition.taskDescription,
          similarity,
          strategy: record.decomposition.strategy,
          success: record.success ?? false,
          subtaskCount: record.decomposition.subtasks.length,
          duration: record.duration,
          executedAt: record.recordedAt,
        })
      }
    }

    // Sort by similarity and take top N
    similar.sort((a, b) => b.similarity - a.similarity)

    return {
      success: true,
      similar: similar.slice(0, limit),
    }
  }

  /**
   * Calculate similarity between two descriptions
   * Uses a simple token-based similarity (Jaccard)
   */
  private calculateSimilarity(desc1: string, desc2: string): number {
    const tokenize = (text: string): Set<string> => {
      return new Set(
        text
          .toLowerCase()
          .replace(/[^\w\s]/g, '')
          .split(/\s+/)
          .filter((t) => t.length > 2)
      )
    }

    const tokens1 = tokenize(desc1)
    const tokens2 = tokenize(desc2)

    if (tokens1.size === 0 || tokens2.size === 0) {
      return 0
    }

    // Jaccard similarity
    const intersection = new Set([...tokens1].filter((t) => tokens2.has(t)))
    const union = new Set([...tokens1, ...tokens2])

    return intersection.size / union.size
  }

  // ===========================================================================
  // Complexity Calculation
  // ===========================================================================

  private calculateTotalComplexity(subtasks: Subtask[]): DecompositionComplexity {
    if (subtasks.length === 0) {
      return 'low'
    }

    // Calculate weighted complexity
    const weights: Record<SubtaskComplexity, number> = { low: 1, medium: 2, high: 3 }
    const totalWeight = subtasks.reduce((sum, s) => sum + weights[s.estimatedComplexity], 0)
    const avgWeight = totalWeight / subtasks.length

    // Factor in subtask count
    const countFactor = subtasks.length > 7 ? 1.5 : subtasks.length > 4 ? 1.2 : 1.0
    const adjustedWeight = avgWeight * countFactor

    if (adjustedWeight >= 4) return 'critical'
    if (adjustedWeight >= 2.5) return 'high'
    if (adjustedWeight >= 1.5) return 'medium'
    return 'low'
  }

  // ===========================================================================
  // Strategy Statistics
  // ===========================================================================

  private getStrategyStats(): Record<
    string,
    { count: number; successes: number; successRate: number }
  > {
    const stats: Record<string, { count: number; successes: number; successRate: number }> = {}

    for (const record of this.records) {
      const strategy = record.decomposition.strategy
      if (!stats[strategy]) {
        stats[strategy] = { count: 0, successes: 0, successRate: 0 }
      }
      stats[strategy].count++
      if (record.success) {
        stats[strategy].successes++
      }
    }

    // Calculate success rates
    for (const strategy of Object.keys(stats)) {
      stats[strategy].successRate =
        stats[strategy].count > 0 ? stats[strategy].successes / stats[strategy].count : 0
    }

    return stats
  }

  // ===========================================================================
  // Strategy Info
  // ===========================================================================

  /**
   * Get available strategies with descriptions
   */
  getStrategies(): { strategy: DecompositionStrategy; description: string }[] {
    return Object.entries(STRATEGY_DESCRIPTIONS).map(([strategy, description]) => ({
      strategy: strategy as DecompositionStrategy,
      description,
    }))
  }

  // ===========================================================================
  // Event System
  // ===========================================================================

  on(listener: DecompositionEventListener): void {
    this.eventListeners.add(listener)
  }

  off(listener: DecompositionEventListener): void {
    this.eventListeners.delete(listener)
  }

  private emit(event: DecompositionEvent): void {
    for (const listener of this.eventListeners) {
      try {
        listener(event)
      } catch {
        // Ignore listener errors
      }
    }
  }

  // ===========================================================================
  // Persistence
  // ===========================================================================

  private ensureDirectory(): void {
    const dir = dirname(this.storagePath)
    if (!existsSync(dir)) {
      mkdirSync(dir, { recursive: true })
    }
  }

  private loadHistory(): void {
    if (!existsSync(this.storagePath)) {
      return
    }

    try {
      const content = readFileSync(this.storagePath, 'utf-8')
      const lines = content.trim().split('\n').filter(Boolean)

      for (const line of lines) {
        try {
          const record = JSON.parse(line)
          const parsed = DecompositionRecordSchema.parse(record)
          this.records.push(parsed)
        } catch {
          // Skip invalid records
        }
      }
    } catch {
      // File read error, start fresh
      this.records = []
    }
  }

  private storeRecord(record: DecompositionRecord): void {
    this.records.push(record)

    // Persist to disk
    this.ensureDirectory()
    try {
      appendFileSync(this.storagePath, JSON.stringify(record) + '\n')
    } catch {
      // Persistence error, log but don't throw
    }
  }

  private saveHistory(): void {
    this.ensureDirectory()
    try {
      const content = this.records.map((r) => JSON.stringify(r)).join('\n') + '\n'
      const fs = require('node:fs')
      fs.writeFileSync(this.storagePath, content)
    } catch {
      // Ignore save errors
    }
  }

  // ===========================================================================
  // Statistics
  // ===========================================================================

  getStats(): {
    totalDecompositions: number
    byStrategy: Record<string, number>
    successRate: number
    averageSubtaskCount: number
  } {
    const byStrategy: Record<string, number> = {}
    let successCount = 0
    let totalSubtasks = 0

    for (const record of this.records) {
      const strategy = record.decomposition.strategy
      byStrategy[strategy] = (byStrategy[strategy] || 0) + 1
      if (record.success) successCount++
      totalSubtasks += record.decomposition.subtasks.length
    }

    return {
      totalDecompositions: this.records.length,
      byStrategy,
      successRate: this.records.length > 0 ? successCount / this.records.length : 0,
      averageSubtaskCount: this.records.length > 0 ? totalSubtasks / this.records.length : 0,
    }
  }

  /**
   * Clear all history (for testing)
   */
  clear(): void {
    this.records = []
    if (existsSync(this.storagePath)) {
      const fs = require('node:fs')
      fs.writeFileSync(this.storagePath, '')
    }
  }

  /**
   * Destroy the engine
   */
  destroy(): void {
    this.records = []
    this.eventListeners.clear()
  }
}

// =============================================================================
// Singleton Instance
// =============================================================================

let globalEngine: DecompositionEngine | null = null

/**
 * Get the global decomposition engine instance
 */
export function getDecompositionEngine(config?: DecompositionEngineConfig): DecompositionEngine {
  if (!globalEngine) {
    globalEngine = new DecompositionEngine(config)
  }
  return globalEngine
}

/**
 * Reset the global engine (for testing)
 */
export function resetDecompositionEngine(): void {
  if (globalEngine) {
    globalEngine.destroy()
    globalEngine = null
  }
}
