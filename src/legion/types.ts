/**
 * LEGION Mode Types
 *
 * Coordinated parallel operator execution for massive tasks.
 * Like a military strike team - multiple operators working in sync.
 */

import { z } from 'zod'

// =============================================================================
// Legion Configuration
// =============================================================================

/**
 * Wave advancement policy
 * - strict: All tasks must complete successfully (100%)
 * - majority: More than 50% must succeed
 * - any: At least one task must succeed
 * - threshold: Custom success rate threshold (use minWaveSuccessRate)
 */
export const wavePolicySchema = z.enum(['strict', 'majority', 'any', 'threshold'])
export type WavePolicy = z.infer<typeof wavePolicySchema>

export const legionConfigSchema = z.object({
  /** Enable LEGION mode */
  enabled: z.boolean().default(true),
  /** Maximum parallel operators */
  maxOperators: z.number().int().min(1).max(10).default(5),
  /** Minimum tasks to trigger LEGION mode */
  minTasksForLegion: z.number().int().min(2).max(20).default(3),
  /** Task timeout in milliseconds */
  taskTimeout: z.number().int().min(5000).max(600000).default(120000),
  /** Enable automatic conflict resolution */
  autoResolveConflicts: z.boolean().default(true),
  /** Merge strategy for results */
  mergeStrategy: z.enum(['sequential', 'parallel', 'smart']).default('smart'),
  /** Retry failed tasks */
  retryFailed: z.boolean().default(true),
  /** Max retries per task */
  maxRetries: z.number().int().min(0).max(3).default(2),
  /**
   * Wave advancement policy (A-2)
   * Controls how to handle partial wave completion:
   * - strict: All tasks must succeed (blocks on any failure)
   * - majority: >50% must succeed
   * - any: At least 1 task must succeed
   * - threshold: Use minWaveSuccessRate
   */
  wavePolicy: wavePolicySchema.default('majority'),
  /** Continue to next wave even if current wave has failures */
  continueOnPartialWave: z.boolean().default(true),
  /** Minimum success rate to advance (0-1), used with wavePolicy='threshold' */
  minWaveSuccessRate: z.number().min(0).max(1).default(0.5),
  /** Abort entire strike if wave fails policy check */
  abortOnWaveFailure: z.boolean().default(false),
})

export type LegionConfig = z.infer<typeof legionConfigSchema>

// =============================================================================
// Legion Task
// =============================================================================

export const legionTaskSchema = z.object({
  id: z.string(),
  missionId: z.string(),
  objectiveId: z.string(),
  description: z.string(),
  acceptanceCriteria: z.array(z.string()),
  dependencies: z.array(z.string()).default([]),
  priority: z.number().int().min(1).max(10).default(5),
  estimatedComplexity: z.enum(['low', 'medium', 'high']).default('medium'),
  assignedOperator: z.string().optional(),
  status: z
    .enum(['pending', 'assigned', 'running', 'completed', 'failed', 'conflict'])
    .default('pending'),
  result: z.unknown().optional(),
  error: z.string().optional(),
  startedAt: z.string().optional(),
  completedAt: z.string().optional(),
  retryCount: z.number().int().default(0),
  filesModified: z.array(z.string()).default([]),
})

export type LegionTask = z.infer<typeof legionTaskSchema>

// =============================================================================
// Legion Operator
// =============================================================================

export const legionOperatorSchema = z.object({
  id: z.string(),
  model: z.string(),
  status: z.enum(['idle', 'busy', 'error', 'offline']).default('idle'),
  currentTask: z.string().optional(),
  tasksCompleted: z.number().int().default(0),
  tasksFailed: z.number().int().default(0),
  averageTaskTime: z.number().default(0),
  lastActivityAt: z.string().optional(),
  specialties: z.array(z.string()).default([]),
})

export type LegionOperator = z.infer<typeof legionOperatorSchema>

// =============================================================================
// Conflict Resolution
// =============================================================================

export const conflictSchema = z.object({
  id: z.string(),
  strikeId: z.string(),
  taskIds: z.array(z.string()),
  conflictType: z.enum([
    'file_collision',
    'dependency_cycle',
    'resource_contention',
    'merge_conflict',
  ]),
  files: z.array(z.string()),
  description: z.string(),
  suggestedResolution: z.string().optional(),
  status: z.enum(['detected', 'analyzing', 'resolved', 'escalated']).default('detected'),
  resolution: z
    .object({
      strategy: z.enum(['merge', 'prefer_first', 'prefer_last', 'manual', 'retry_sequential']),
      appliedAt: z.string(),
      appliedBy: z.string(),
    })
    .optional(),
})

export type Conflict = z.infer<typeof conflictSchema>

// =============================================================================
// Wave Result (Partial Success Tracking)
// =============================================================================

export const waveResultSchema = z.object({
  /** Wave index (0-based) */
  waveIndex: z.number().int(),
  /** Total tasks in this wave */
  totalTasks: z.number().int(),
  /** Successfully completed tasks */
  completedTasks: z.number().int(),
  /** Failed tasks */
  failedTasks: z.number().int(),
  /** Success rate (0-1) */
  successRate: z.number(),
  /** Wave status */
  status: z.enum(['complete', 'partial', 'failed']),
  /** Wave duration in ms */
  durationMs: z.number().int(),
  /** Task IDs in this wave */
  taskIds: z.array(z.string()),
})

export type WaveResult = z.infer<typeof waveResultSchema>

// =============================================================================
// Legion Strike (Execution Unit)
// =============================================================================

export const legionStrikeSchema = z.object({
  id: z.string(),
  missionId: z.string(),
  status: z.enum(['planning', 'executing', 'merging', 'completed', 'failed']).default('planning'),
  tasks: z.array(legionTaskSchema),
  operators: z.array(legionOperatorSchema),
  startedAt: z.string(),
  completedAt: z.string().optional(),
  conflicts: z.array(conflictSchema).default([]),
  metrics: z
    .object({
      totalTasks: z.number().int(),
      completedTasks: z.number().int(),
      failedTasks: z.number().int(),
      parallelism: z.number(),
      totalTime: z.number(),
      averageTaskTime: z.number(),
      /** Per-wave results for partial success tracking */
      waveResults: z.array(waveResultSchema).optional(),
    })
    .optional(),
})

export type LegionStrike = z.infer<typeof legionStrikeSchema>

// =============================================================================
// Task Distribution Strategy
// =============================================================================

export type DistributionStrategy =
  | 'round_robin' // Simple rotation
  | 'load_balanced' // Based on operator load
  | 'specialty_match' // Match task to operator specialty
  | 'complexity_aware' // Complex tasks to stronger models
  | 'dependency_aware' // Respect task dependencies

export interface DistributionPlan {
  strategy: DistributionStrategy
  assignments: Array<{
    taskId: string
    operatorId: string
    reason: string
    wave: number // Execution wave (for dependencies)
  }>
  waves: number
  estimatedTime: number
}

// =============================================================================
// Legion Events
// =============================================================================

export type LegionEventType =
  | 'legion.strike.started'
  | 'legion.strike.completed'
  | 'legion.strike.failed'
  | 'legion.strike.aborted'
  | 'legion.wave.started'
  | 'legion.wave.completed'
  | 'legion.wave.policy_failed'
  | 'legion.task.assigned'
  | 'legion.task.started'
  | 'legion.task.completed'
  | 'legion.task.failed'
  | 'legion.task.dependency_failed'
  | 'legion.conflict.detected'
  | 'legion.conflict.resolved'
  | 'legion.operator.joined'
  | 'legion.operator.left'
  | 'legion.merge.started'
  | 'legion.merge.completed'

export interface LegionEvent {
  type: LegionEventType
  timestamp: string
  strikeId: string
  data: Record<string, unknown>
}

// =============================================================================
// Default Configuration
// =============================================================================

export const DEFAULT_LEGION_CONFIG: LegionConfig = {
  enabled: true,
  maxOperators: 5,
  minTasksForLegion: 3,
  taskTimeout: 120000,
  autoResolveConflicts: true,
  mergeStrategy: 'smart',
  retryFailed: true,
  maxRetries: 2,
  // Wave advancement policies (A-2)
  wavePolicy: 'majority',
  continueOnPartialWave: true,
  minWaveSuccessRate: 0.5,
  abortOnWaveFailure: false,
}
