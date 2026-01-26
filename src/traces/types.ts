/**
 * Delta9 Decision Trace Types
 *
 * Structured recording of WHY decisions were made, enabling:
 * - Decision audit trails
 * - Precedent-based reasoning
 * - Learning from past decisions
 * - Debugging decision chains
 */

import { z } from 'zod'

// =============================================================================
// Decision Types
// =============================================================================

export const DecisionTypeSchema = z.enum([
  'decomposition_strategy', // Why this decomposition approach?
  'agent_assignment', // Why this agent for this task?
  'council_consensus', // How did oracles agree/disagree?
  'validation_override', // Why override validation result?
  'conflict_resolution', // How was file/task conflict resolved?
  'model_selection', // Why this model for this task?
  'retry_strategy', // Why retry vs fail?
  'priority_change', // Why change task priority?
  'task_skip', // Why skip this task?
  'budget_decision', // Why approve/deny budget usage?
])

export type DecisionType = z.infer<typeof DecisionTypeSchema>

// =============================================================================
// Decision Trace Schema
// =============================================================================

export const DecisionTraceSchema = z.object({
  /** Unique trace ID */
  id: z.string(),

  /** Type of decision being traced */
  type: DecisionTypeSchema,

  /** What was decided (the outcome) */
  decision: z.string(),

  /** Why this decision was made (reasoning) */
  reasoning: z.string(),

  /** What alternatives were considered */
  alternatives: z.array(z.string()).default([]),

  /** Confidence level (0-1) in the decision */
  confidence: z.number().min(0).max(1),

  /** Links to prior decisions that informed this one */
  precedentIds: z.array(z.string()).optional(),

  /** Context that influenced the decision */
  context: z
    .object({
      taskId: z.string().optional(),
      missionId: z.string().optional(),
      agentId: z.string().optional(),
      sessionId: z.string().optional(),
      files: z.array(z.string()).optional(),
      tags: z.array(z.string()).optional(),
    })
    .optional(),

  /** Timestamp when decision was made */
  timestamp: z.string().datetime(),

  /** Who/what made the decision */
  decidedBy: z.string().optional(),
})

export type DecisionTrace = z.infer<typeof DecisionTraceSchema>

// =============================================================================
// Query Schema
// =============================================================================

export const TraceQuerySchema = z.object({
  /** Filter by decision type */
  type: DecisionTypeSchema.optional(),

  /** Filter by task ID */
  taskId: z.string().optional(),

  /** Filter by mission ID */
  missionId: z.string().optional(),

  /** Filter by agent ID */
  agentId: z.string().optional(),

  /** Filter by session ID */
  sessionId: z.string().optional(),

  /** Filter traces since timestamp */
  since: z.string().optional(),

  /** Filter traces until timestamp */
  until: z.string().optional(),

  /** Search in decision/reasoning text */
  search: z.string().optional(),

  /** Minimum confidence threshold */
  minConfidence: z.number().min(0).max(1).optional(),

  /** Maximum results to return */
  limit: z.number().default(50),

  /** Include precedent chain */
  includePrecedents: z.boolean().default(false),
})

export type TraceQuery = z.infer<typeof TraceQuerySchema>

// =============================================================================
// Trace Result
// =============================================================================

export const TraceResultSchema = z.object({
  traces: z.array(DecisionTraceSchema),
  total: z.number(),
  hasMore: z.boolean(),
})

export type TraceResult = z.infer<typeof TraceResultSchema>

// =============================================================================
// Precedent Chain
// =============================================================================

export const PrecedentChainSchema = z.object({
  /** Root decision trace */
  root: DecisionTraceSchema,

  /** Chain of precedents (oldest first) */
  chain: z.array(DecisionTraceSchema),

  /** Total depth of chain */
  depth: z.number(),
})

export type PrecedentChain = z.infer<typeof PrecedentChainSchema>

// =============================================================================
// Decision Statistics
// =============================================================================

export const TraceStatsSchema = z.object({
  /** Total traces recorded */
  total: z.number(),

  /** Breakdown by decision type */
  byType: z.record(DecisionTypeSchema, z.number()),

  /** Average confidence by type */
  avgConfidenceByType: z.record(DecisionTypeSchema, z.number()),

  /** Most referenced precedents */
  topPrecedents: z.array(
    z.object({
      id: z.string(),
      decision: z.string(),
      referenceCount: z.number(),
    })
  ),

  /** Recent activity */
  recentCount: z.number(),
})

export type TraceStats = z.infer<typeof TraceStatsSchema>

// =============================================================================
// Helper Types
// =============================================================================

export interface CreateTraceInput {
  type: DecisionType
  decision: string
  reasoning: string
  alternatives?: string[]
  confidence: number
  precedentIds?: string[]
  context?: DecisionTrace['context']
  decidedBy?: string
}
