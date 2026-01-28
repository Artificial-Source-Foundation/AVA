/**
 * Cost Optimizer Engine
 *
 * Selects optimal models based on:
 * - Budget constraints
 * - Task complexity
 * - Quality requirements
 * - Latency requirements
 */

import type {
  OptimizerConfig,
  ModelCostProfile,
  TaskRequirements,
  OptimizationResult,
  BudgetStatus,
} from './types.js'
import { DEFAULT_OPTIMIZER_CONFIG, DEFAULT_MODEL_PROFILES } from './types.js'
import { loadConfig } from '../lib/config.js'

// =============================================================================
// Cost Optimizer
// =============================================================================

export class CostOptimizer {
  private config: OptimizerConfig
  private cwd: string
  private models: Map<string, ModelCostProfile> = new Map()
  private budget: number = 10.0
  private spent: number = 0
  private usageHistory: Array<{
    modelId: string
    cost: number
    timestamp: string
    taskType: string
  }> = []

  constructor(config: Partial<OptimizerConfig> = {}, cwd: string = process.cwd()) {
    this.config = { ...DEFAULT_OPTIMIZER_CONFIG, ...config }
    this.cwd = cwd

    // Load default model profiles
    for (const profile of DEFAULT_MODEL_PROFILES) {
      this.models.set(profile.modelId, profile)
    }
  }

  // ===========================================================================
  // Model Registry
  // ===========================================================================

  /**
   * Add or update a model profile
   */
  registerModel(profile: ModelCostProfile): void {
    this.models.set(profile.modelId, profile)
  }

  /**
   * Remove a model
   */
  unregisterModel(modelId: string): void {
    this.models.delete(modelId)
  }

  /**
   * Get all registered models
   */
  getModels(): ModelCostProfile[] {
    return Array.from(this.models.values())
  }

  // ===========================================================================
  // Budget Management
  // ===========================================================================

  /**
   * Set the total budget
   */
  setBudget(amount: number): void {
    this.budget = amount
  }

  /**
   * Record spending
   */
  recordSpending(modelId: string, cost: number, taskType: string = 'unknown'): void {
    this.spent += cost
    this.usageHistory.push({
      modelId,
      cost,
      timestamp: new Date().toISOString(),
      taskType,
    })

    // Keep history limited
    if (this.usageHistory.length > 1000) {
      this.usageHistory = this.usageHistory.slice(-500)
    }
  }

  /**
   * Get current budget status
   */
  getBudgetStatus(): BudgetStatus {
    const remaining = Math.max(0, this.budget - this.spent)
    const percentUsed = this.budget > 0 ? this.spent / this.budget : 0

    // Project total based on recent spending rate
    const recentHistory = this.usageHistory.slice(-20)
    const avgCostPerTask =
      recentHistory.length > 0
        ? recentHistory.reduce((sum, h) => sum + h.cost, 0) / recentHistory.length
        : 0.05

    const shouldDowngrade =
      this.config.autoDowngrade && percentUsed >= this.config.downgradeThreshold

    // Recommend tier based on remaining budget
    let recommendedTier: BudgetStatus['recommendedTier']
    if (remaining < 0.5) {
      recommendedTier = 'budget'
    } else if (remaining < 2) {
      recommendedTier = 'standard'
    } else if (remaining < 5) {
      recommendedTier = 'premium'
    } else {
      recommendedTier = 'flagship'
    }

    return {
      totalBudget: this.budget,
      spent: this.spent,
      remaining,
      percentUsed,
      projectedTotal: this.spent + avgCostPerTask * 10, // Project next 10 tasks
      isOverBudget: this.spent >= this.budget,
      shouldDowngrade,
      recommendedTier,
    }
  }

  /**
   * Reset spending tracker
   */
  resetSpending(): void {
    this.spent = 0
    this.usageHistory = []
  }

  // ===========================================================================
  // Model Selection
  // ===========================================================================

  /**
   * Select optimal model for a task
   */
  selectModel(requirements: TaskRequirements): OptimizationResult {
    if (!this.config.enabled) {
      // Return default model from config (tier 2 is default)
      const delta9Config = loadConfig(this.cwd)
      const defaultModel = delta9Config.operators.tier2Model
      const profile = this.models.get(defaultModel)
      return {
        selectedModel: defaultModel,
        estimatedCost: profile ? this.estimateCost(profile, requirements) : 0.01,
        estimatedLatency: profile?.averageLatency || 3000,
        qualityScore: profile?.qualityScore || 90,
        reason: 'Cost optimization disabled - using default model',
        alternatives: [],
        warnings: [],
        isDowngraded: false,
      }
    }

    const budgetStatus = this.getBudgetStatus()
    const candidates = this.getCandidateModels(requirements, budgetStatus)

    if (candidates.length === 0) {
      // No suitable models - return cheapest available
      const cheapest = this.getCheapestModel()
      return {
        selectedModel: cheapest.modelId,
        estimatedCost: this.estimateCost(cheapest, requirements),
        estimatedLatency: cheapest.averageLatency,
        qualityScore: cheapest.qualityScore,
        reason: 'No models meet requirements - using cheapest available',
        alternatives: [],
        warnings: ['No models meet quality/capability requirements'],
        isDowngraded: true,
      }
    }

    // Score and rank candidates
    const scored = candidates.map((model) => ({
      model,
      score: this.scoreModel(model, requirements, budgetStatus),
      cost: this.estimateCost(model, requirements),
    }))

    scored.sort((a, b) => b.score - a.score)

    const selected = scored[0]
    const alternatives = scored.slice(1, 4).map((s) => ({
      model: s.model.modelId,
      cost: s.cost,
      quality: s.model.qualityScore,
      reason: this.getSelectionReason(s.model, requirements),
    }))

    const warnings: string[] = []

    // Check for downgrade
    const isDowngraded =
      budgetStatus.shouldDowngrade &&
      selected.model.tier !== 'flagship' &&
      requirements.complexity === 'critical'

    if (isDowngraded) {
      warnings.push('Model downgraded due to budget constraints')
    }

    if (budgetStatus.percentUsed > 0.9) {
      warnings.push('Budget nearly exhausted')
    }

    if (
      selected.model.qualityScore < (requirements.minQuality || this.config.minQualityThreshold)
    ) {
      warnings.push('Selected model below quality threshold')
    }

    return {
      selectedModel: selected.model.modelId,
      estimatedCost: selected.cost,
      estimatedLatency: selected.model.averageLatency,
      qualityScore: selected.model.qualityScore,
      reason: this.getSelectionReason(selected.model, requirements),
      alternatives,
      warnings,
      isDowngraded,
    }
  }

  /**
   * Get candidate models that meet basic requirements
   */
  private getCandidateModels(
    requirements: TaskRequirements,
    budgetStatus: BudgetStatus
  ): ModelCostProfile[] {
    const candidates: ModelCostProfile[] = []
    const minQuality = requirements.minQuality || this.config.minQualityThreshold
    const maxLatency = requirements.maxLatency || this.config.maxLatencyThreshold

    for (const model of this.models.values()) {
      // Check quality threshold
      if (model.qualityScore < minQuality) continue

      // Check latency threshold
      if (model.averageLatency > maxLatency) continue

      // Check capabilities
      if (requirements.requiredCapabilities.length > 0) {
        const hasAllCapabilities = requirements.requiredCapabilities.every((cap) =>
          model.capabilities.includes(cap)
        )
        if (!hasAllCapabilities) continue
      }

      // Check context window
      const totalTokens = requirements.estimatedInputTokens + requirements.estimatedOutputTokens
      if (totalTokens > model.maxContextTokens) continue

      // Check budget in strict mode
      if (this.config.budgetMode === 'strict') {
        const cost = this.estimateCost(model, requirements)
        if (this.spent + cost > this.budget) continue
      }

      // Check tier if downgrading
      if (budgetStatus.shouldDowngrade) {
        const tierOrder = ['budget', 'standard', 'premium', 'flagship']
        const recommendedIndex = tierOrder.indexOf(budgetStatus.recommendedTier)
        const modelIndex = tierOrder.indexOf(model.tier)
        if (modelIndex > recommendedIndex + 1) continue // Allow one tier above recommended
      }

      candidates.push(model)
    }

    return candidates
  }

  /**
   * Score a model for selection
   */
  private scoreModel(
    model: ModelCostProfile,
    requirements: TaskRequirements,
    budgetStatus: BudgetStatus
  ): number {
    const cost = this.estimateCost(model, requirements)

    // Base scores (0-100)
    const qualityScore = model.qualityScore
    const costScore = 100 - Math.min(100, (cost / budgetStatus.remaining) * 100)
    const latencyScore =
      100 - Math.min(100, (model.averageLatency / this.config.maxLatencyThreshold) * 100)

    // Weights based on requirements
    let qualityWeight = 0.4
    let costWeight = 0.35
    let latencyWeight = 0.25

    // Adjust weights based on priority
    switch (requirements.priority) {
      case 'critical':
        qualityWeight = 0.6
        costWeight = 0.2
        latencyWeight = 0.2
        break
      case 'high':
        qualityWeight = 0.5
        costWeight = 0.25
        latencyWeight = 0.25
        break
      case 'low':
        qualityWeight = 0.25
        costWeight = 0.5
        latencyWeight = 0.25
        break
    }

    // Adjust for budget status
    if (budgetStatus.percentUsed > 0.7) {
      costWeight += 0.15
      qualityWeight -= 0.1
      latencyWeight -= 0.05
    }

    // Complexity bonus for high-quality models on complex tasks
    let complexityBonus = 0
    if (requirements.complexity === 'critical' && model.tier === 'flagship') {
      complexityBonus = 10
    } else if (requirements.complexity === 'high' && model.tier !== 'budget') {
      complexityBonus = 5
    }

    return (
      qualityScore * qualityWeight +
      costScore * costWeight +
      latencyScore * latencyWeight +
      complexityBonus
    )
  }

  /**
   * Get selection reason
   */
  private getSelectionReason(model: ModelCostProfile, requirements: TaskRequirements): string {
    const reasons: string[] = []

    if (model.tier === 'flagship') {
      reasons.push('highest quality')
    } else if (model.tier === 'budget') {
      reasons.push('cost-effective')
    }

    if (model.averageLatency < 2000) {
      reasons.push('fast response')
    }

    if (requirements.requiredCapabilities.length > 0) {
      reasons.push('meets capability requirements')
    }

    if (requirements.complexity === 'critical' && model.qualityScore >= 90) {
      reasons.push('suitable for critical tasks')
    }

    return reasons.length > 0 ? reasons.join(', ') : 'general purpose'
  }

  /**
   * Estimate cost for a model and task
   */
  estimateCost(model: ModelCostProfile, requirements: TaskRequirements): number {
    const inputCost = (requirements.estimatedInputTokens / 1000) * model.inputCostPer1k
    const outputCost = (requirements.estimatedOutputTokens / 1000) * model.outputCostPer1k
    return inputCost + outputCost
  }

  /**
   * Get cheapest available model
   */
  private getCheapestModel(): ModelCostProfile {
    let cheapest: ModelCostProfile | null = null
    let lowestCost = Infinity

    for (const model of this.models.values()) {
      const avgCost = model.inputCostPer1k + model.outputCostPer1k
      if (avgCost < lowestCost) {
        lowestCost = avgCost
        cheapest = model
      }
    }

    return cheapest || DEFAULT_MODEL_PROFILES[0]
  }

  // ===========================================================================
  // Batch Optimization
  // ===========================================================================

  /**
   * Optimize model selection for multiple tasks
   */
  optimizeBatch(tasks: TaskRequirements[]): Array<OptimizationResult & { taskIndex: number }> {
    const results: Array<OptimizationResult & { taskIndex: number }> = []

    // Sort tasks by priority (critical first)
    const priorityOrder = { critical: 0, high: 1, normal: 2, low: 3 }
    const indexed = tasks.map((t, i) => ({ task: t, index: i }))
    indexed.sort((a, b) => priorityOrder[a.task.priority] - priorityOrder[b.task.priority])

    for (const { task, index } of indexed) {
      const result = this.selectModel(task)
      results.push({ ...result, taskIndex: index })

      // Track estimated spending for subsequent selections
      this.spent += result.estimatedCost * 0.5 // Conservative estimate
    }

    // Reset the temporary spending
    this.spent -= results.reduce((sum, r) => sum + r.estimatedCost * 0.5, 0)

    // Sort back by original index
    results.sort((a, b) => a.taskIndex - b.taskIndex)

    return results
  }

  // ===========================================================================
  // Analytics
  // ===========================================================================

  /**
   * Get spending breakdown by model
   */
  getSpendingByModel(): Record<string, { total: number; count: number; avgCost: number }> {
    const breakdown: Record<string, { total: number; count: number; avgCost: number }> = {}

    for (const usage of this.usageHistory) {
      if (!breakdown[usage.modelId]) {
        breakdown[usage.modelId] = { total: 0, count: 0, avgCost: 0 }
      }
      breakdown[usage.modelId].total += usage.cost
      breakdown[usage.modelId].count++
    }

    for (const modelId of Object.keys(breakdown)) {
      breakdown[modelId].avgCost = breakdown[modelId].total / breakdown[modelId].count
    }

    return breakdown
  }

  /**
   * Get spending by task type
   */
  getSpendingByTaskType(): Record<string, number> {
    const breakdown: Record<string, number> = {}

    for (const usage of this.usageHistory) {
      breakdown[usage.taskType] = (breakdown[usage.taskType] || 0) + usage.cost
    }

    return breakdown
  }

  /**
   * Get cost savings recommendations
   */
  getCostSavingsRecommendations(): Array<{
    recommendation: string
    potentialSavings: number
    impact: 'low' | 'medium' | 'high'
  }> {
    const recommendations: Array<{
      recommendation: string
      potentialSavings: number
      impact: 'low' | 'medium' | 'high'
    }> = []

    const spendingByModel = this.getSpendingByModel()

    // Check for overuse of expensive models
    const flagshipSpending = Object.entries(spendingByModel)
      .filter(([modelId]) => this.models.get(modelId)?.tier === 'flagship')
      .reduce((sum, [, data]) => sum + data.total, 0)

    if (flagshipSpending > this.spent * 0.5) {
      recommendations.push({
        recommendation: 'Consider using premium tier models for non-critical tasks',
        potentialSavings: flagshipSpending * 0.3,
        impact: 'high',
      })
    }

    // Check for tasks that could use budget models
    const spendingByTask = this.getSpendingByTaskType()
    if (spendingByTask['validation'] && spendingByTask['validation'] > this.spent * 0.2) {
      recommendations.push({
        recommendation: 'Use budget models for validation tasks',
        potentialSavings: spendingByTask['validation'] * 0.5,
        impact: 'medium',
      })
    }

    return recommendations
  }

  // ===========================================================================
  // Configuration
  // ===========================================================================

  getConfig(): OptimizerConfig {
    return { ...this.config }
  }

  updateConfig(updates: Partial<OptimizerConfig>): void {
    this.config = { ...this.config, ...updates }
  }
}
