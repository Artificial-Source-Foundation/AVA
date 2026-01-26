/**
 * Delta9 Operator Handoff Types
 *
 * Typed handoff contract for worker spawning.
 * Inspired by SWARM's WorkerHandoff pattern.
 *
 * Philosophy: "Workers receive complete context. No guessing required."
 */

import { z } from 'zod'

// =============================================================================
// Schemas
// =============================================================================

/**
 * Contract section - Explicit ownership and constraints
 */
export const HandoffContractSchema = z.object({
  /** Task ID being assigned */
  taskId: z.string(),
  /** Files the operator can modify (exclusive ownership) */
  filesOwned: z.array(z.string()),
  /** Files the operator can read but NOT modify */
  filesReadonly: z.array(z.string()),
  /** Success criteria to meet */
  successCriteria: z.array(z.string()),
  /** Things the operator must NOT do */
  mustNot: z.array(z.string()),
})

/**
 * Context section - Mission and task context
 */
export const HandoffContextSchema = z.object({
  /** Brief mission summary */
  missionSummary: z.string(),
  /** Operator's role in the mission */
  yourRole: z.string(),
  /** What other operators have done (relevant prior work) */
  priorWork: z.string(),
  /** What comes after this task */
  nextSteps: z.string(),
})

/**
 * Escalation section - What to do when blocked
 */
export const HandoffEscalationSchema = z.object({
  /** What to do if blocked */
  blockedAction: z.string(),
  /** What to do if scope changes needed */
  scopeChangeAction: z.string(),
})

/**
 * Full Operator Handoff
 */
export const OperatorHandoffSchema = z.object({
  contract: HandoffContractSchema,
  context: HandoffContextSchema,
  escalation: HandoffEscalationSchema,
})

// =============================================================================
// Types
// =============================================================================

export type HandoffContract = z.infer<typeof HandoffContractSchema>
export type HandoffContext = z.infer<typeof HandoffContextSchema>
export type HandoffEscalation = z.infer<typeof HandoffEscalationSchema>
export type OperatorHandoff = z.infer<typeof OperatorHandoffSchema>

// =============================================================================
// Defaults
// =============================================================================

export const DEFAULT_ESCALATION: HandoffEscalation = {
  blockedAction:
    'Report using task_complete with status explaining the blocker. Commander will handle.',
  scopeChangeAction:
    'Do NOT expand scope. Report what you found and let Commander decide next steps.',
}

export const DEFAULT_MUST_NOT: string[] = [
  'Do NOT modify files outside filesOwned list',
  'Do NOT refactor code beyond what is needed for the task',
  'Do NOT add features not in successCriteria',
  'Do NOT skip validation steps',
]
