/**
 * Delta9 Learning System Types
 *
 * Type definitions for the learning and pattern detection system.
 */

import { z } from 'zod'

// =============================================================================
// Pattern Types
// =============================================================================

export const PatternSourceSchema = z.enum(['success', 'failure', 'user', 'inferred'])
export type PatternSource = z.infer<typeof PatternSourceSchema>

export const PatternCategorySchema = z.enum([
  'strategy', // Decomposition strategies (file-based, feature-based, etc.)
  'file', // File-specific patterns (auth.ts has race conditions)
  'agent', // Agent behavior patterns
  'tool', // Tool usage patterns
  'error', // Error patterns
  'general', // General patterns
])
export type PatternCategory = z.infer<typeof PatternCategorySchema>

export const PatternSchema = z.object({
  /** Unique pattern identifier */
  id: z.string(),
  /** Pattern description/name */
  pattern: z.string(),
  /** Category of pattern */
  category: PatternCategorySchema,
  /** Context where pattern applies */
  context: z.string(),
  /** Base confidence score (0-1) */
  baseConfidence: z.number().min(0).max(1),
  /** Current confidence after decay */
  currentConfidence: z.number().min(0).max(1),
  /** How pattern was learned */
  source: PatternSourceSchema,
  /** Number of times applied */
  applications: z.number().int().min(0),
  /** Successful applications */
  successes: z.number().int().min(0),
  /** Failed applications */
  failures: z.number().int().min(0),
  /** When pattern was first learned */
  createdAt: z.string().datetime(),
  /** When pattern was last applied */
  lastAppliedAt: z.string().datetime().optional(),
  /** When confidence was last updated */
  lastUpdatedAt: z.string().datetime(),
  /** Whether this is marked as anti-pattern */
  isAntiPattern: z.boolean(),
  /** Related files (for file patterns) */
  relatedFiles: z.array(z.string()).optional(),
  /** Related agents (for agent patterns) */
  relatedAgents: z.array(z.string()).optional(),
  /** Tags for filtering */
  tags: z.array(z.string()).optional(),
})

export type Pattern = z.infer<typeof PatternSchema>

// =============================================================================
// Outcome Types
// =============================================================================

export const OutcomeSchema = z.object({
  /** Unique outcome ID */
  id: z.string(),
  /** Task ID this outcome relates to */
  taskId: z.string(),
  /** Mission ID */
  missionId: z.string().optional(),
  /** Whether task succeeded */
  success: z.boolean(),
  /** Agent that executed the task */
  agent: z.string(),
  /** Duration in milliseconds */
  duration: z.number(),
  /** Files that were changed */
  filesChanged: z.array(z.string()),
  /** Error message if failed */
  error: z.string().optional(),
  /** Error code if failed */
  errorCode: z.string().optional(),
  /** Patterns that were applied */
  patternsApplied: z.array(z.string()),
  /** Strategy used (for decomposition) */
  strategy: z.string().optional(),
  /** Timestamp */
  timestamp: z.string().datetime(),
  /** Additional context */
  context: z.record(z.unknown()).optional(),
})

export type Outcome = z.infer<typeof OutcomeSchema>

// =============================================================================
// Insight Types
// =============================================================================

export const InsightTypeSchema = z.enum([
  'strategy', // Strategy success/failure insights
  'file', // File-specific gotchas
  'pattern', // Pattern recommendations
  'warning', // Anti-pattern warnings
  'tip', // General tips
])
export type InsightType = z.infer<typeof InsightTypeSchema>

export const InsightSchema = z.object({
  /** Insight type */
  type: InsightTypeSchema,
  /** Insight text */
  text: z.string(),
  /** Relevance score (0-1) */
  relevance: z.number().min(0).max(1),
  /** Source pattern ID */
  sourcePatternId: z.string().optional(),
  /** Related files */
  files: z.array(z.string()).optional(),
  /** Related agents */
  agents: z.array(z.string()).optional(),
})

export type Insight = z.infer<typeof InsightSchema>

export const InsightBudgetSchema = z.object({
  /** Max tokens for coordinator insights */
  coordinator: z.number().default(500),
  /** Max tokens for worker insights */
  worker: z.number().default(300),
  /** Max insights per category */
  maxPerCategory: z.number().default(3),
})

export type InsightBudget = z.infer<typeof InsightBudgetSchema>

// =============================================================================
// Learning Config
// =============================================================================

export const LearningConfigSchema = z.object({
  /** Enable learning system */
  enabled: z.boolean().default(true),
  /** Half-life for confidence decay in days */
  halfLifeDays: z.number().default(90),
  /** Failure rate threshold for anti-pattern detection (0-1) */
  antiPatternThreshold: z.number().min(0).max(1).default(0.6),
  /** Minimum applications before anti-pattern detection */
  minApplicationsForAntiPattern: z.number().int().min(1).default(5),
  /** Token budget for insights */
  insightBudget: InsightBudgetSchema.default({}),
  /** Path for learning data storage */
  storagePath: z.string().default('.delta9/learning.json'),
})

export type LearningConfig = z.infer<typeof LearningConfigSchema>

// =============================================================================
// Learning Store
// =============================================================================

export const LearningStoreSchema = z.object({
  /** Version for migrations */
  version: z.number().default(1),
  /** All patterns */
  patterns: z.array(PatternSchema),
  /** Recent outcomes (last 100) */
  outcomes: z.array(OutcomeSchema),
  /** Last decay calculation */
  lastDecayAt: z.string().datetime().optional(),
  /** Statistics */
  stats: z.object({
    totalPatterns: z.number(),
    totalAntiPatterns: z.number(),
    totalOutcomes: z.number(),
    successRate: z.number(),
  }),
})

export type LearningStore = z.infer<typeof LearningStoreSchema>
