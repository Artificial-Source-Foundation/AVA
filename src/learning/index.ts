/**
 * Delta9 Learning System
 *
 * Adaptive learning from task outcomes with:
 * - Pattern recognition and tracking
 * - Confidence decay (90-day half-life)
 * - Anti-pattern detection
 * - Insights injection for prompts
 *
 * @example
 * ```typescript
 * import { getLearningEngine, generateCoordinatorInsights, formatInsightsForPrompt } from './learning'
 *
 * // Record outcomes
 * const engine = getLearningEngine()
 * engine.recordOutcome({
 *   taskId: 't1',
 *   success: true,
 *   agent: 'operator',
 *   duration: 5000,
 *   filesChanged: ['src/auth.ts'],
 *   patternsApplied: [],
 *   strategy: 'file-based'
 * })
 *
 * // Generate insights for prompts
 * const insights = generateCoordinatorInsights({ files: ['src/auth.ts'] })
 * const promptSection = formatInsightsForPrompt(insights)
 * ```
 */

// Types
export {
  PatternSourceSchema,
  type PatternSource,
  PatternCategorySchema,
  type PatternCategory,
  PatternSchema,
  type Pattern,
  OutcomeSchema,
  type Outcome,
  InsightTypeSchema,
  type InsightType,
  InsightSchema,
  type Insight,
  InsightBudgetSchema,
  type InsightBudget,
  LearningConfigSchema,
  type LearningConfig,
  LearningStoreSchema,
  type LearningStore,
} from './types.js'

// Engine
export {
  LearningEngine,
  getLearningEngine,
  resetLearningEngine,
} from './engine.js'

// Insights
export {
  InsightGenerator,
  getInsightGenerator,
  generateCoordinatorInsights,
  generateWorkerInsights,
  formatInsightsForPrompt,
  resetInsightGenerator,
} from './insights.js'
