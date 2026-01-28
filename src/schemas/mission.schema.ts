/**
 * Delta9 Mission Schema
 *
 * Zod schemas for validating mission.json state.
 */

import { z } from 'zod'
import { councilModeSchema } from './config.schema.js'

// =============================================================================
// Status Schemas
// =============================================================================

export const missionStatusSchema = z.enum([
  'planning',
  'approved',
  'in_progress',
  'paused',
  'completed',
  'aborted',
])

export const objectiveStatusSchema = z.enum(['pending', 'in_progress', 'completed', 'failed'])

export const taskStatusSchema = z.enum(['pending', 'blocked', 'in_progress', 'completed', 'failed'])

export const validationStatusSchema = z.enum(['pass', 'fixable', 'fail'])

export const complexitySchema = z.enum(['low', 'medium', 'high', 'critical'])

// =============================================================================
// Validation Result Schema
// =============================================================================

export const validationResultSchema = z.object({
  status: validationStatusSchema,
  validatedAt: z.string().datetime(),
  summary: z.string(),
  issues: z.array(z.string()).optional(),
  suggestions: z.array(z.string()).optional(),
})

// =============================================================================
// Task Schema
// =============================================================================

// Helper: coerce string to array (some models return string instead of array)
const stringOrArrayToArray = z.preprocess(
  (val) => {
    if (typeof val === 'string') {
      return [val]
    }
    return val
  },
  z.array(z.string().min(1)).min(1)
)

export const taskSchema = z.object({
  id: z.string().min(1),
  description: z.string().min(1),
  status: taskStatusSchema,
  assignedTo: z.string().optional(),
  routedTo: z.string().optional(),
  workerSession: z.string().optional(),
  attempts: z.number().int().min(0).default(0),
  acceptanceCriteria: stringOrArrayToArray,
  validation: validationResultSchema.optional(),
  filesChanged: z.array(z.string()).optional(),
  tokensUsed: z.number().int().min(0).optional(),
  cost: z.number().min(0).optional(),
  dependencies: z.array(z.string()).optional(),
  startedAt: z.string().datetime().optional(),
  completedAt: z.string().datetime().optional(),
  error: z.string().optional(),
})

// =============================================================================
// Objective Schema
// =============================================================================

export const objectiveSchema = z.object({
  id: z.string().min(1),
  description: z.string().min(1),
  status: objectiveStatusSchema,
  tasks: z.array(taskSchema), // Allow empty - tasks can be added later
  checkpoint: z.string().optional(),
  startedAt: z.string().datetime().optional(),
  completedAt: z.string().datetime().optional(),
})

// =============================================================================
// Council Summary Schema
// =============================================================================

export const oracleOpinionSchema = z.object({
  oracle: z.string().min(1),
  recommendation: z.string().min(1),
  confidence: z.number().min(0).max(1),
  caveats: z.array(z.string()).optional(),
  suggestedTasks: z.array(z.string()).optional(),
})

export const councilSummarySchema = z.object({
  mode: councilModeSchema,
  consensus: z.array(z.string()),
  disagreementsResolved: z.array(z.string()).optional(),
  confidenceAvg: z.number().min(0).max(1),
  opinions: z.array(oracleOpinionSchema).optional(),
})

// =============================================================================
// Budget Schema
// =============================================================================

export const budgetBreakdownSchema = z.object({
  council: z.number().min(0).default(0),
  operators: z.number().min(0).default(0),
  validators: z.number().min(0).default(0),
  support: z.number().min(0).default(0),
})

export const budgetTrackingSchema = z.object({
  limit: z.number().min(0),
  spent: z.number().min(0).default(0),
  breakdown: budgetBreakdownSchema.default({
    council: 0,
    operators: 0,
    validators: 0,
    support: 0,
  }),
})

// =============================================================================
// Mission Schema
// =============================================================================

export const missionSchema = z.object({
  $schema: z.string().optional(),
  id: z.string().min(1),
  description: z.string().min(1),
  status: missionStatusSchema,
  complexity: complexitySchema,
  councilMode: councilModeSchema,
  councilSummary: councilSummarySchema.optional(),
  objectives: z.array(objectiveSchema),
  currentObjective: z.number().int().min(0).default(0),
  budget: budgetTrackingSchema,
  dependencies: z.record(z.string(), z.array(z.string())).optional(),
  createdAt: z.string().datetime(),
  updatedAt: z.string().datetime(),
  approvedAt: z.string().datetime().optional(),
  startedAt: z.string().datetime().optional(), // BUG-38: When mission started execution
  completedAt: z.string().datetime().optional(),
})

// =============================================================================
// History Event Schema
// =============================================================================

export const historyEventTypeSchema = z.enum([
  'mission_created',
  'mission_approved',
  'mission_paused',
  'mission_resumed',
  'mission_completed',
  'mission_aborted',
  'mission_status_changed', // BUG-38: State machine transitions
  'objective_started',
  'objective_completed',
  'objective_failed',
  'task_started',
  'task_completed',
  'task_failed',
  'task_retried',
  'task_unblocked',
  'dependencies_fixed',
  'validation_passed',
  'validation_fixable',
  'validation_failed',
  'checkpoint_created',
  'rollback_executed',
  'budget_warning',
  'budget_exceeded',
  'context_compacted',
  'council_convened',
  'council_completed',
  'xhigh_recon_completed',
  'background_task_started',
  'background_task_completed',
  'background_task_failed',
  'replan_triggered',
  'recovery_attempted',
])

export const historyEventSchema = z.object({
  type: historyEventTypeSchema,
  timestamp: z.string().datetime(),
  missionId: z.string().min(1),
  objectiveId: z.string().optional(),
  taskId: z.string().optional(),
  data: z.record(z.unknown()).optional(),
})

// =============================================================================
// Memory Entry Schema
// =============================================================================

export const memoryTypeSchema = z.enum(['pattern', 'failure', 'success', 'preference'])

export const memoryEntrySchema = z.object({
  id: z.string().min(1),
  type: memoryTypeSchema,
  content: z.string().min(1),
  confidence: z.number().min(0).max(1),
  occurrences: z.number().int().min(1).default(1),
  createdAt: z.string().datetime(),
  updatedAt: z.string().datetime(),
  tags: z.array(z.string()).optional(),
})

// =============================================================================
// Type Exports
// =============================================================================

export type MissionSchema = z.infer<typeof missionSchema>
export type TaskSchema = z.infer<typeof taskSchema>
export type ObjectiveSchema = z.infer<typeof objectiveSchema>
export type ValidationResultSchema = z.infer<typeof validationResultSchema>
export type HistoryEventSchema = z.infer<typeof historyEventSchema>
export type MemoryEntrySchema = z.infer<typeof memoryEntrySchema>

// =============================================================================
// Validation Helpers
// =============================================================================

export function validateMission(mission: unknown): MissionSchema {
  return missionSchema.parse(mission)
}

export function validateMissionSafe(
  mission: unknown
): { success: true; data: MissionSchema } | { success: false; error: z.ZodError } {
  return missionSchema.safeParse(mission)
}

export function validateTask(task: unknown): TaskSchema {
  return taskSchema.parse(task)
}

export function validateHistoryEvent(event: unknown): HistoryEventSchema {
  return historyEventSchema.parse(event)
}
