/**
 * Delta9 Learning Insights
 *
 * Generates context-aware insights for prompt injection.
 * Insights are token-budgeted and prioritized by relevance.
 *
 * Insight Types:
 * - Strategy: Success rates by decomposition approach
 * - File: File-specific gotchas and patterns
 * - Pattern: Recommended patterns for the task
 * - Warning: Anti-pattern warnings
 * - Tip: General tips from successful outcomes
 */

import { Insight, InsightBudget, InsightBudgetSchema, PatternCategory } from './types.js'
import { LearningEngine, getLearningEngine } from './engine.js'

// =============================================================================
// Constants
// =============================================================================

const DEFAULT_BUDGET: InsightBudget = {
  coordinator: 500,
  worker: 300,
  maxPerCategory: 3,
}

// Rough token estimates per insight type
const TOKENS_PER_INSIGHT = {
  strategy: 50,
  file: 40,
  pattern: 45,
  warning: 60,
  tip: 35,
}

// =============================================================================
// Insight Generator
// =============================================================================

export class InsightGenerator {
  private engine: LearningEngine
  private budget: InsightBudget

  constructor(engine?: LearningEngine, budget?: Partial<InsightBudget>) {
    this.engine = engine || getLearningEngine()
    this.budget = InsightBudgetSchema.parse(budget || DEFAULT_BUDGET)
  }

  /**
   * Generate insights for a coordinator/commander prompt
   */
  generateCoordinatorInsights(context: {
    taskDescription?: string
    files?: string[]
    strategy?: string
  }): Insight[] {
    const insights: Insight[] = []
    let tokensUsed = 0

    // 1. Strategy insights (most important for coordinators)
    const strategyInsights = this.getStrategyInsights()
    for (const insight of strategyInsights.slice(0, this.budget.maxPerCategory)) {
      if (tokensUsed + TOKENS_PER_INSIGHT.strategy > this.budget.coordinator) break
      insights.push(insight)
      tokensUsed += TOKENS_PER_INSIGHT.strategy
    }

    // 2. Anti-pattern warnings
    const warnings = this.getAntiPatternWarnings(context.files)
    for (const warning of warnings.slice(0, this.budget.maxPerCategory)) {
      if (tokensUsed + TOKENS_PER_INSIGHT.warning > this.budget.coordinator) break
      insights.push(warning)
      tokensUsed += TOKENS_PER_INSIGHT.warning
    }

    // 3. General pattern insights
    const patternInsights = this.getPatternInsights()
    for (const insight of patternInsights.slice(0, this.budget.maxPerCategory)) {
      if (tokensUsed + TOKENS_PER_INSIGHT.pattern > this.budget.coordinator) break
      insights.push(insight)
      tokensUsed += TOKENS_PER_INSIGHT.pattern
    }

    return insights.sort((a, b) => b.relevance - a.relevance)
  }

  /**
   * Generate insights for a worker/operator prompt
   */
  generateWorkerInsights(context: {
    taskDescription?: string
    files: string[]
    agent?: string
  }): Insight[] {
    const insights: Insight[] = []
    let tokensUsed = 0

    // 1. File-specific insights (most important for workers)
    const fileInsights = this.getFileInsights(context.files)
    for (const insight of fileInsights.slice(0, this.budget.maxPerCategory)) {
      if (tokensUsed + TOKENS_PER_INSIGHT.file > this.budget.worker) break
      insights.push(insight)
      tokensUsed += TOKENS_PER_INSIGHT.file
    }

    // 2. Anti-pattern warnings for files
    const warnings = this.getAntiPatternWarnings(context.files)
    for (const warning of warnings.slice(0, 2)) {
      if (tokensUsed + TOKENS_PER_INSIGHT.warning > this.budget.worker) break
      insights.push(warning)
      tokensUsed += TOKENS_PER_INSIGHT.warning
    }

    // 3. Agent-specific tips
    if (context.agent) {
      const agentTips = this.getAgentTips(context.agent)
      for (const tip of agentTips.slice(0, 2)) {
        if (tokensUsed + TOKENS_PER_INSIGHT.tip > this.budget.worker) break
        insights.push(tip)
        tokensUsed += TOKENS_PER_INSIGHT.tip
      }
    }

    return insights.sort((a, b) => b.relevance - a.relevance)
  }

  /**
   * Get strategy success rate insights
   */
  private getStrategyInsights(): Insight[] {
    const rates = this.engine.getStrategySuccessRates()

    return rates
      .filter((r) => r.applications >= 3) // Only include strategies with enough data
      .map((r) => ({
        type: 'strategy' as const,
        text: `Strategy "${r.strategy}": ${Math.round(r.successRate * 100)}% success rate (${r.applications} uses)`,
        relevance: r.successRate * (Math.min(r.applications, 10) / 10), // Weight by usage
        sourcePatternId: undefined,
      }))
  }

  /**
   * Get file-specific insights
   */
  private getFileInsights(files: string[]): Insight[] {
    if (!files || files.length === 0) return []

    const patterns = this.engine.getPatternsForFiles(files)

    return patterns.map((p) => ({
      type: 'file' as const,
      text: `${p.pattern}: ${p.context}`,
      relevance: p.currentConfidence * (p.isAntiPattern ? 1.5 : 1), // Boost anti-patterns
      sourcePatternId: p.id,
      files: p.relatedFiles,
    }))
  }

  /**
   * Get anti-pattern warnings
   */
  private getAntiPatternWarnings(files?: string[]): Insight[] {
    const antiPatterns = this.engine.getAntiPatterns()

    // Filter by files if provided
    let relevant = antiPatterns
    if (files && files.length > 0) {
      relevant = antiPatterns.filter(
        (p) =>
          !p.relatedFiles ||
          p.relatedFiles.some((f) =>
            files.some((file) => file.includes(f) || f.includes(file))
          )
      )
    }

    return relevant.map((p) => ({
      type: 'warning' as const,
      text: `WARNING: ${p.pattern} has ${Math.round((p.failures / p.applications) * 100)}% failure rate`,
      relevance: p.failures / p.applications,
      sourcePatternId: p.id,
      files: p.relatedFiles,
      agents: p.relatedAgents,
    }))
  }

  /**
   * Get general pattern insights
   */
  private getPatternInsights(category?: PatternCategory): Insight[] {
    const patterns = category
      ? this.engine.getPatternsByCategory(category)
      : this.engine.getTopPatterns(10)

    return patterns
      .filter((p) => p.currentConfidence > 0.5)
      .map((p) => ({
        type: 'pattern' as const,
        text: `Recommended: ${p.pattern} (${Math.round(p.currentConfidence * 100)}% confidence)`,
        relevance: p.currentConfidence,
        sourcePatternId: p.id,
      }))
  }

  /**
   * Get agent-specific tips
   */
  private getAgentTips(agent: string): Insight[] {
    const outcomes = this.engine.getOutcomesForAgent(agent)
    const successRate = this.engine.getAgentSuccessRate(agent)

    const tips: Insight[] = []

    // Success rate tip
    if (outcomes.length >= 5) {
      tips.push({
        type: 'tip' as const,
        text: `${agent} has ${Math.round(successRate * 100)}% success rate (${outcomes.length} tasks)`,
        relevance: 0.5,
        agents: [agent],
      })
    }

    // Common error patterns for this agent
    const agentPatterns = this.engine
      .getAntiPatterns()
      .filter((p) => p.relatedAgents?.includes(agent))

    for (const pattern of agentPatterns.slice(0, 2)) {
      tips.push({
        type: 'tip' as const,
        text: `Watch out: ${pattern.pattern}`,
        relevance: 0.7,
        sourcePatternId: pattern.id,
        agents: [agent],
      })
    }

    return tips
  }

  /**
   * Format insights for prompt injection
   */
  formatForPrompt(insights: Insight[]): string {
    if (insights.length === 0) return ''

    const sections: string[] = []

    // Group by type
    const byType = new Map<string, Insight[]>()
    for (const insight of insights) {
      const existing = byType.get(insight.type) || []
      existing.push(insight)
      byType.set(insight.type, existing)
    }

    // Format each section
    if (byType.has('warning')) {
      sections.push(
        '**Warnings from past failures:**\n' +
          byType
            .get('warning')!
            .map((i) => `- ${i.text}`)
            .join('\n')
      )
    }

    if (byType.has('strategy')) {
      sections.push(
        '**Strategy insights:**\n' +
          byType
            .get('strategy')!
            .map((i) => `- ${i.text}`)
            .join('\n')
      )
    }

    if (byType.has('file')) {
      sections.push(
        '**File-specific notes:**\n' +
          byType
            .get('file')!
            .map((i) => `- ${i.text}`)
            .join('\n')
      )
    }

    if (byType.has('pattern')) {
      sections.push(
        '**Recommended patterns:**\n' +
          byType
            .get('pattern')!
            .map((i) => `- ${i.text}`)
            .join('\n')
      )
    }

    if (byType.has('tip')) {
      sections.push(
        '**Tips:**\n' +
          byType
            .get('tip')!
            .map((i) => `- ${i.text}`)
            .join('\n')
      )
    }

    return sections.join('\n\n')
  }
}

// =============================================================================
// Convenience Functions
// =============================================================================

let defaultGenerator: InsightGenerator | null = null

/**
 * Get the default insight generator
 */
export function getInsightGenerator(): InsightGenerator {
  if (!defaultGenerator) {
    defaultGenerator = new InsightGenerator()
  }
  return defaultGenerator
}

/**
 * Generate coordinator insights
 */
export function generateCoordinatorInsights(context: {
  taskDescription?: string
  files?: string[]
  strategy?: string
}): Insight[] {
  return getInsightGenerator().generateCoordinatorInsights(context)
}

/**
 * Generate worker insights
 */
export function generateWorkerInsights(context: {
  taskDescription?: string
  files: string[]
  agent?: string
}): Insight[] {
  return getInsightGenerator().generateWorkerInsights(context)
}

/**
 * Format insights for prompt injection
 */
export function formatInsightsForPrompt(insights: Insight[]): string {
  return getInsightGenerator().formatForPrompt(insights)
}

/**
 * Reset the default generator (for testing)
 */
export function resetInsightGenerator(): void {
  defaultGenerator = null
}
