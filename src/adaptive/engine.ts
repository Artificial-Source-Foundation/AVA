/**
 * Adaptive Council Engine
 *
 * Learns which oracles perform best for different task categories.
 * Uses multi-armed bandit approach with exploration/exploitation.
 */

import { randomUUID } from 'node:crypto'
import type {
  AdaptiveConfig,
  TaskCategory,
  OraclePerformance,
  ConsultationRecord,
  OracleSelectionResult,
  PerformanceUpdate,
} from './types.js'
import { DEFAULT_ADAPTIVE_CONFIG, CATEGORY_KEYWORDS, taskCategorySchema } from './types.js'

// =============================================================================
// Adaptive Council Engine
// =============================================================================

export class AdaptiveCouncilEngine {
  private config: AdaptiveConfig
  private performance: Map<string, OraclePerformance> = new Map() // key: oracleId:category
  private consultations: ConsultationRecord[] = []
  private oracleIds: string[] = []

  constructor(config: Partial<AdaptiveConfig> = {}) {
    this.config = { ...DEFAULT_ADAPTIVE_CONFIG, ...config }
  }

  // ===========================================================================
  // Oracle Registration
  // ===========================================================================

  /**
   * Register oracles for adaptive tracking
   */
  registerOracles(oracleIds: string[]): void {
    this.oracleIds = oracleIds

    // Initialize performance records for all oracle-category combinations
    for (const oracleId of oracleIds) {
      for (const category of taskCategorySchema.options) {
        const key = this.getPerformanceKey(oracleId, category)
        if (!this.performance.has(key)) {
          this.performance.set(key, {
            oracleId,
            category,
            totalConsultations: 0,
            successfulRecommendations: 0,
            averageConfidence: 0.5,
            averageResponseTime: 0,
            consensusRate: 0.5,
            lastUpdated: new Date().toISOString(),
            performanceScore: 50, // Start at neutral
          })
        }
      }
    }
  }

  // ===========================================================================
  // Category Detection
  // ===========================================================================

  /**
   * Detect task category from description
   */
  detectCategory(taskDescription: string): TaskCategory {
    const description = taskDescription.toLowerCase()
    let bestCategory: TaskCategory = 'general'
    let bestScore = 0

    for (const [category, keywords] of Object.entries(CATEGORY_KEYWORDS)) {
      if (category === 'general') continue

      let score = 0
      for (const keyword of keywords) {
        if (description.includes(keyword)) {
          score += 1
        }
      }

      if (score > bestScore) {
        bestScore = score
        bestCategory = category as TaskCategory
      }
    }

    return bestCategory
  }

  // ===========================================================================
  // Oracle Selection
  // ===========================================================================

  /**
   * Select oracles based on learned performance
   */
  selectOracles(category: TaskCategory, requestedCount: number = 4): OracleSelectionResult {
    if (!this.config.enabled || !this.config.dynamicSelection) {
      // Return all oracles with equal weights
      return {
        selectedOracles: this.oracleIds.slice(0, requestedCount),
        weights: Object.fromEntries(this.oracleIds.map((id) => [id, 1 / this.oracleIds.length])),
        reason: 'Adaptive selection disabled - using equal weights',
        isExploration: false,
        categoryScores: {},
      }
    }

    // Check if we have enough samples
    const totalSamples = this.getTotalSamplesForCategory(category)
    if (totalSamples < this.config.minSamplesForAdaptation) {
      return {
        selectedOracles: this.oracleIds.slice(0, requestedCount),
        weights: Object.fromEntries(this.oracleIds.map((id) => [id, 1 / this.oracleIds.length])),
        reason: `Insufficient samples (${totalSamples}/${this.config.minSamplesForAdaptation}) - using equal weights`,
        isExploration: true,
        categoryScores: {},
      }
    }

    // Calculate performance scores for each oracle in this category
    const scores: Record<string, number> = {}
    for (const oracleId of this.oracleIds) {
      scores[oracleId] = this.getOracleScore(oracleId, category)
    }

    // Sort by score
    const sortedOracles = [...this.oracleIds].sort((a, b) => scores[b] - scores[a])

    // Exploration: randomly include a non-optimal oracle
    const isExploration = Math.random() < this.config.explorationRate
    let selectedOracles: string[]

    if (isExploration && sortedOracles.length > requestedCount) {
      // Include top (requestedCount - 1) and one random from the rest
      const topOracles = sortedOracles.slice(0, requestedCount - 1)
      const remainingOracles = sortedOracles.slice(requestedCount - 1)
      const randomOracle = remainingOracles[Math.floor(Math.random() * remainingOracles.length)]
      selectedOracles = [...topOracles, randomOracle]
    } else {
      selectedOracles = sortedOracles.slice(0, requestedCount)
    }

    // Calculate weights based on scores
    const totalScore = selectedOracles.reduce((sum, id) => sum + scores[id], 0)
    const weights: Record<string, number> = {}
    for (const oracleId of selectedOracles) {
      weights[oracleId] =
        totalScore > 0 ? scores[oracleId] / totalScore : 1 / selectedOracles.length
    }

    return {
      selectedOracles,
      weights,
      reason: isExploration
        ? `Exploration mode - testing alternative oracle`
        : `Performance-based selection for ${category}`,
      isExploration,
      categoryScores: { [category]: scores },
    }
  }

  /**
   * Get oracle score for a category
   */
  private getOracleScore(oracleId: string, category: TaskCategory): number {
    const key = this.getPerformanceKey(oracleId, category)
    const perf = this.performance.get(key)

    if (!perf || perf.totalConsultations === 0) {
      return 50 // Neutral score for unrated oracles
    }

    // Apply decay based on last update time
    const daysSinceUpdate = this.getDaysSinceUpdate(perf.lastUpdated)
    const decayFactor = Math.pow(1 - this.config.decayRate, daysSinceUpdate)

    return perf.performanceScore * decayFactor
  }

  // ===========================================================================
  // Learning / Performance Updates
  // ===========================================================================

  /**
   * Record a consultation
   */
  recordConsultation(
    missionId: string,
    taskId: string,
    oracleId: string,
    category: TaskCategory,
    recommendation: string,
    confidence: number,
    responseTime: number
  ): string {
    const record: ConsultationRecord = {
      id: `consult_${randomUUID().slice(0, 8)}`,
      missionId,
      taskId,
      category,
      oracleId,
      recommendation,
      confidence,
      responseTime,
      timestamp: new Date().toISOString(),
    }

    this.consultations.push(record)
    return record.id
  }

  /**
   * Update consultation outcome
   */
  updateOutcome(
    consultationId: string,
    wasAccepted: boolean,
    taskOutcome: 'success' | 'failure' | 'partial' | 'unknown',
    matchedConsensus: boolean
  ): void {
    const record = this.consultations.find((c) => c.id === consultationId)
    if (!record) return

    record.wasAccepted = wasAccepted
    record.taskOutcome = taskOutcome
    record.consensusMatch = matchedConsensus

    // Update performance
    this.updatePerformance({
      oracleId: record.oracleId,
      category: record.category,
      wasSuccessful: taskOutcome === 'success' || (taskOutcome === 'partial' && wasAccepted),
      confidence: record.confidence,
      responseTime: record.responseTime,
      matchedConsensus,
    })
  }

  /**
   * Update oracle performance metrics
   */
  updatePerformance(update: PerformanceUpdate): void {
    const key = this.getPerformanceKey(update.oracleId, update.category)
    let perf = this.performance.get(key)

    if (!perf) {
      perf = {
        oracleId: update.oracleId,
        category: update.category,
        totalConsultations: 0,
        successfulRecommendations: 0,
        averageConfidence: 0.5,
        averageResponseTime: 0,
        consensusRate: 0.5,
        lastUpdated: new Date().toISOString(),
        performanceScore: 50,
      }
      this.performance.set(key, perf)
    }

    // Incremental updates
    const lr = this.config.learningRate

    perf.totalConsultations++

    if (update.wasSuccessful) {
      perf.successfulRecommendations++
    }

    // Update rolling averages
    perf.averageConfidence = perf.averageConfidence * (1 - lr) + update.confidence * lr
    perf.averageResponseTime = perf.averageResponseTime * (1 - lr) + update.responseTime * lr
    perf.consensusRate = perf.consensusRate * (1 - lr) + (update.matchedConsensus ? 1 : 0) * lr

    // Calculate new performance score (0-100)
    const successRate = perf.successfulRecommendations / perf.totalConsultations
    const confidenceScore = perf.averageConfidence
    const consensusScore = perf.consensusRate
    const speedScore = Math.max(0, 1 - perf.averageResponseTime / 60000) // Penalty for slow responses

    // Weighted combination
    perf.performanceScore =
      successRate * 40 + confidenceScore * 25 + consensusScore * 20 + speedScore * 15

    perf.lastUpdated = new Date().toISOString()
  }

  // ===========================================================================
  // Bulk Learning from History
  // ===========================================================================

  /**
   * Learn from historical mission data
   */
  learnFromHistory(
    records: Array<{
      oracleId: string
      category: TaskCategory
      wasSuccessful: boolean
      confidence: number
      responseTime: number
      matchedConsensus: boolean
    }>
  ): void {
    for (const record of records) {
      this.updatePerformance(record)
    }
  }

  // ===========================================================================
  // Analysis & Reporting
  // ===========================================================================

  /**
   * Get performance summary for all oracles
   */
  getPerformanceSummary(): Record<string, Record<TaskCategory, OraclePerformance>> {
    const summary: Record<string, Record<TaskCategory, OraclePerformance>> = {}

    for (const [_key, perf] of this.performance) {
      if (!summary[perf.oracleId]) {
        summary[perf.oracleId] = {} as Record<TaskCategory, OraclePerformance>
      }
      summary[perf.oracleId][perf.category] = perf
    }

    return summary
  }

  /**
   * Get best oracle for a category
   */
  getBestOracle(category: TaskCategory): { oracleId: string; score: number } | null {
    let bestOracle: string | null = null
    let bestScore = -1

    for (const oracleId of this.oracleIds) {
      const score = this.getOracleScore(oracleId, category)
      if (score > bestScore) {
        bestScore = score
        bestOracle = oracleId
      }
    }

    return bestOracle ? { oracleId: bestOracle, score: bestScore } : null
  }

  /**
   * Get oracle specialties (categories where they excel)
   */
  getOracleSpecialties(oracleId: string, threshold: number = 70): TaskCategory[] {
    const specialties: TaskCategory[] = []

    for (const category of taskCategorySchema.options) {
      const score = this.getOracleScore(oracleId, category)
      if (score >= threshold) {
        specialties.push(category)
      }
    }

    return specialties
  }

  /**
   * Get recommendations for improving oracle performance
   */
  getOptimizationRecommendations(): Array<{
    oracleId: string
    recommendation: string
    priority: 'high' | 'medium' | 'low'
  }> {
    const recommendations: Array<{
      oracleId: string
      recommendation: string
      priority: 'high' | 'medium' | 'low'
    }> = []

    for (const oracleId of this.oracleIds) {
      // Check for underperforming categories
      for (const category of taskCategorySchema.options) {
        const perf = this.performance.get(this.getPerformanceKey(oracleId, category))
        if (!perf) continue

        if (perf.performanceScore < 30 && perf.totalConsultations >= 5) {
          recommendations.push({
            oracleId,
            recommendation: `Consider removing ${oracleId} from ${category} tasks (score: ${perf.performanceScore.toFixed(1)})`,
            priority: 'high',
          })
        } else if (perf.averageResponseTime > 30000 && perf.totalConsultations >= 3) {
          recommendations.push({
            oracleId,
            recommendation: `${oracleId} is slow for ${category} (avg: ${(perf.averageResponseTime / 1000).toFixed(1)}s)`,
            priority: 'medium',
          })
        }
      }
    }

    return recommendations
  }

  // ===========================================================================
  // Persistence
  // ===========================================================================

  /**
   * Export state for persistence
   */
  exportState(): {
    performance: Array<OraclePerformance>
    consultations: ConsultationRecord[]
    config: AdaptiveConfig
  } {
    return {
      performance: Array.from(this.performance.values()),
      consultations: this.consultations.slice(-1000), // Keep last 1000
      config: this.config,
    }
  }

  /**
   * Import state from persistence
   */
  importState(state: {
    performance: Array<OraclePerformance>
    consultations: ConsultationRecord[]
    config?: Partial<AdaptiveConfig>
  }): void {
    this.performance.clear()
    for (const perf of state.performance) {
      const key = this.getPerformanceKey(perf.oracleId, perf.category)
      this.performance.set(key, perf)
    }

    this.consultations = state.consultations || []

    if (state.config) {
      this.config = { ...this.config, ...state.config }
    }
  }

  // ===========================================================================
  // Utilities
  // ===========================================================================

  private getPerformanceKey(oracleId: string, category: TaskCategory): string {
    return `${oracleId}:${category}`
  }

  private getTotalSamplesForCategory(category: TaskCategory): number {
    let total = 0
    for (const oracleId of this.oracleIds) {
      const perf = this.performance.get(this.getPerformanceKey(oracleId, category))
      if (perf) {
        total += perf.totalConsultations
      }
    }
    return total
  }

  private getDaysSinceUpdate(lastUpdated: string): number {
    const now = Date.now()
    const updated = new Date(lastUpdated).getTime()
    return (now - updated) / (1000 * 60 * 60 * 24)
  }

  getConfig(): AdaptiveConfig {
    return { ...this.config }
  }
}
