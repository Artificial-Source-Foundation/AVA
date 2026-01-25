/**
 * Replay System Types
 *
 * Re-run past missions with different parameters for comparison.
 */

import { z } from 'zod'

// =============================================================================
// Replay Configuration
// =============================================================================

export const replayConfigSchema = z.object({
  /** What to replay */
  mode: z.enum(['mission', 'objective', 'task', 'council']),
  /** Source mission/task ID */
  sourceId: z.string(),
  /** What to change */
  modifications: z.object({
    /** Different council mode */
    councilMode: z.enum(['none', 'quick', 'standard', 'xhigh']).optional(),
    /** Different models */
    models: z.record(z.string()).optional(), // e.g., { "oracle.cipher": "gpt-4o" }
    /** Different temperature */
    temperature: z.number().min(0).max(2).optional(),
    /** Different oracles */
    oracles: z.array(z.string()).optional(),
    /** Skip certain tasks */
    skipTasks: z.array(z.string()).optional(),
    /** Force certain decisions */
    forceDecisions: z.record(z.string()).optional(),
  }),
  /** Comparison options */
  comparison: z.object({
    /** Compare with original */
    compareWithOriginal: z.boolean().default(true),
    /** Metrics to compare */
    metrics: z.array(z.enum([
      'cost',
      'time',
      'quality',
      'success_rate',
      'council_consensus',
    ])).default(['cost', 'time', 'quality']),
  }).default({}),
})

export type ReplayConfig = z.infer<typeof replayConfigSchema>

// =============================================================================
// Mission Snapshot
// =============================================================================

export const missionSnapshotSchema = z.object({
  id: z.string(),
  title: z.string(),
  createdAt: z.string(),
  completedAt: z.string().optional(),
  status: z.enum(['pending', 'in_progress', 'paused', 'completed', 'failed', 'aborted']),
  objectives: z.array(z.object({
    id: z.string(),
    title: z.string(),
    status: z.string(),
    tasks: z.array(z.object({
      id: z.string(),
      description: z.string(),
      status: z.string(),
      agent: z.string().optional(),
      result: z.unknown().optional(),
      error: z.string().optional(),
      startedAt: z.string().optional(),
      completedAt: z.string().optional(),
    })),
  })),
  councilMode: z.string().optional(),
  councilResponses: z.array(z.object({
    oracleId: z.string(),
    recommendation: z.string(),
    confidence: z.number(),
    responseTime: z.number(),
  })).optional(),
  metrics: z.object({
    totalCost: z.number().optional(),
    totalTime: z.number().optional(),
    tasksCompleted: z.number(),
    tasksFailed: z.number(),
    councilConsensus: z.number().optional(),
  }).optional(),
  config: z.record(z.unknown()).optional(),
})

export type MissionSnapshot = z.infer<typeof missionSnapshotSchema>

// =============================================================================
// Replay Result
// =============================================================================

export const replayResultSchema = z.object({
  replayId: z.string(),
  sourceId: z.string(),
  mode: z.enum(['mission', 'objective', 'task', 'council']),
  startedAt: z.string(),
  completedAt: z.string().optional(),
  status: z.enum(['running', 'completed', 'failed', 'cancelled']),
  modifications: z.record(z.unknown()),
  results: z.object({
    success: z.boolean(),
    outcome: z.unknown().optional(),
    error: z.string().optional(),
  }),
  metrics: z.object({
    cost: z.number(),
    time: z.number(),
    tasksCompleted: z.number(),
    tasksFailed: z.number(),
  }),
  comparison: z.object({
    original: z.record(z.number()),
    replay: z.record(z.number()),
    improvements: z.array(z.object({
      metric: z.string(),
      originalValue: z.number(),
      replayValue: z.number(),
      change: z.number(), // percentage
      improved: z.boolean(),
    })),
    summary: z.string(),
  }).optional(),
})

export type ReplayResult = z.infer<typeof replayResultSchema>

// =============================================================================
// Comparison Report
// =============================================================================

export interface ComparisonReport {
  replayId: string
  sourceId: string
  timestamp: string
  modifications: Record<string, unknown>
  metrics: {
    original: Record<string, number>
    replay: Record<string, number>
    delta: Record<string, number>
    percentChange: Record<string, number>
  }
  improvements: string[]
  regressions: string[]
  recommendations: string[]
  winner: 'original' | 'replay' | 'tie'
  confidence: number
}

// =============================================================================
// Replay Events
// =============================================================================

export type ReplayEventType =
  | 'replay.started'
  | 'replay.task.started'
  | 'replay.task.completed'
  | 'replay.task.skipped'
  | 'replay.council.started'
  | 'replay.council.completed'
  | 'replay.completed'
  | 'replay.failed'
  | 'replay.cancelled'
  | 'replay.comparison.generated'

export interface ReplayEvent {
  type: ReplayEventType
  timestamp: string
  replayId: string
  data: Record<string, unknown>
}
