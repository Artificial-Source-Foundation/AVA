/**
 * Delta9 Subagent Types
 *
 * Lightweight abstraction over background tasks with:
 * - Human-readable aliases
 * - State tracking
 * - Output piping
 */

import { z } from 'zod'

// =============================================================================
// Schemas
// =============================================================================

export const SubagentStateSchema = z.enum(['spawning', 'active', 'idle', 'completed', 'failed'])
export type SubagentState = z.infer<typeof SubagentStateSchema>

export const SubagentSchema = z.object({
  /** Human-readable alias (e.g., "code_searcher", "doc_writer") */
  alias: z.string(),

  /** Background task ID (e.g., "bg_abc123") */
  taskId: z.string(),

  /** Session ID (if SDK is available) */
  sessionId: z.string().optional(),

  /** Agent type used */
  agentType: z.string(),

  /** Original prompt */
  prompt: z.string(),

  /** Current state */
  state: SubagentStateSchema,

  /** Parent session ID (caller that spawned this subagent) */
  parentSessionId: z.string().optional(),

  /** When subagent was spawned */
  spawnedAt: z.string(),

  /** When subagent completed */
  completedAt: z.string().optional(),

  /** Output (populated when completed) */
  output: z.string().optional(),

  /** Error (populated when failed) */
  error: z.string().optional(),

  /** Whether output has been delivered to parent */
  outputDelivered: z.boolean().default(false),
})

export type Subagent = z.infer<typeof SubagentSchema>

// =============================================================================
// Input Types
// =============================================================================

export interface SpawnSubagentInput {
  /** Human-readable alias for this subagent */
  alias: string

  /** Task prompt */
  prompt: string

  /** Agent type (operator, explorer, etc.) */
  agentType?: string

  /** Additional context */
  context?: string

  /** Skills to load */
  skills?: string[]

  /** Parent session ID for output piping */
  parentSessionId?: string
}

export interface SubagentOutput {
  alias: string
  taskId: string
  state: SubagentState
  output?: string
  error?: string
  duration?: number
}

// =============================================================================
// Query Types
// =============================================================================

export interface SubagentQuery {
  /** Filter by state */
  state?: SubagentState

  /** Filter by parent session */
  parentSessionId?: string

  /** Only include undelivered outputs */
  pendingDelivery?: boolean
}

export interface SubagentStats {
  total: number
  byState: Record<SubagentState, number>
  pendingDelivery: number
}
