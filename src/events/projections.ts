/**
 * Delta9 Event Projections
 *
 * Build materialized views from the event log.
 * Projections enable state reconstruction after context compaction.
 *
 * Available Projections:
 * - MissionProjection: Current mission state
 * - TaskProjection: Task statuses and history
 * - CouncilProjection: Oracle consultation history
 * - LearningProjection: Patterns and anti-patterns
 * - MetricsProjection: Performance metrics
 */

import type { Delta9Event } from './types.js'
import { EventStore, getEventStore } from './store.js'

// =============================================================================
// Projection Types
// =============================================================================

export interface MissionProjection {
  id: string | null
  name: string | null
  status: 'pending' | 'active' | 'completed' | 'failed' | 'aborted'
  objectives: string[]
  taskCount: number
  tasksCompleted: number
  tasksFailed: number
  budgetSpent: number
  budgetLimit: number | null
  startedAt: string | null
  completedAt: string | null
  duration: number | null
}

export interface TaskProjection {
  id: string
  title: string
  status: 'pending' | 'active' | 'completed' | 'failed' | 'skipped'
  agent: string | null
  attempts: number
  filesChanged: string[]
  duration: number | null
  error: string | null
}

export interface CouncilProjection {
  totalConsultations: number
  consultationsByMode: Record<string, number>
  oracleStats: Record<
    string,
    {
      consultations: number
      avgConfidence: number
      timeouts: number
    }
  >
  consensusRate: number
  avgConsultationTime: number
}

export interface PatternRecord {
  pattern: string
  context: string
  confidence: number
  source: 'success' | 'failure' | 'user' | 'inferred'
  applications: number
  successes: number
  failures: number
  lastApplied: string | null
  isAntiPattern: boolean
}

export interface LearningProjection {
  patterns: PatternRecord[]
  antiPatterns: PatternRecord[]
  totalPatterns: number
  totalAntiPatterns: number
  successRate: number
}

export interface MetricsProjection {
  totalTasks: number
  successfulTasks: number
  failedTasks: number
  totalDuration: number
  avgTaskDuration: number
  totalTokens: number
  totalBudgetSpent: number
  tasksByAgent: Record<string, number>
  errorsByCode: Record<string, number>
}

// =============================================================================
// Projection Reducers
// =============================================================================

const INITIAL_MISSION: MissionProjection = {
  id: null,
  name: null,
  status: 'pending',
  objectives: [],
  taskCount: 0,
  tasksCompleted: 0,
  tasksFailed: 0,
  budgetSpent: 0,
  budgetLimit: null,
  startedAt: null,
  completedAt: null,
  duration: null,
}

function missionReducer(state: MissionProjection, event: Delta9Event): MissionProjection {
  switch (event.type) {
    case 'mission.created':
      return {
        ...state,
        id: event.missionId || null,
        name: event.data.name,
        objectives: event.data.objectives,
        budgetLimit: event.data.budgetLimit || null,
        status: 'pending',
      }

    case 'mission.started':
      return {
        ...state,
        status: 'active',
        taskCount: event.data.taskCount,
        startedAt: event.timestamp,
      }

    case 'mission.completed':
      return {
        ...state,
        status: event.data.success ? 'completed' : 'failed',
        tasksCompleted: event.data.tasksCompleted,
        tasksFailed: event.data.tasksFailed,
        budgetSpent: event.data.budgetSpent || state.budgetSpent,
        completedAt: event.timestamp,
        duration: event.data.duration,
      }

    case 'mission.failed':
      return {
        ...state,
        status: 'failed',
        completedAt: event.timestamp,
      }

    case 'mission.aborted':
      return {
        ...state,
        status: 'aborted',
        completedAt: event.timestamp,
      }

    case 'task.completed':
      return {
        ...state,
        tasksCompleted: event.data.success ? state.tasksCompleted + 1 : state.tasksCompleted,
        tasksFailed: event.data.success ? state.tasksFailed : state.tasksFailed + 1,
      }

    default:
      return state
  }
}

function taskReducer(
  state: Map<string, TaskProjection>,
  event: Delta9Event
): Map<string, TaskProjection> {
  const newState = new Map(state)

  switch (event.type) {
    case 'task.created': {
      newState.set(event.data.taskId, {
        id: event.data.taskId,
        title: event.data.title,
        status: 'pending',
        agent: event.data.assignedAgent || null,
        attempts: 0,
        filesChanged: [],
        duration: null,
        error: null,
      })
      break
    }

    case 'task.started': {
      const task = newState.get(event.data.taskId)
      if (task) {
        newState.set(event.data.taskId, {
          ...task,
          status: 'active',
          agent: event.data.agent,
          attempts: task.attempts + 1,
        })
      }
      break
    }

    case 'task.completed': {
      const task = newState.get(event.data.taskId)
      if (task) {
        newState.set(event.data.taskId, {
          ...task,
          status: event.data.success ? 'completed' : 'failed',
          filesChanged: event.data.filesChanged || task.filesChanged,
          duration: event.data.duration,
        })
      }
      break
    }

    case 'task.failed': {
      const task = newState.get(event.data.taskId)
      if (task) {
        newState.set(event.data.taskId, {
          ...task,
          status: 'failed',
          error: event.data.error,
        })
      }
      break
    }

    case 'task.skipped': {
      const task = newState.get(event.data.taskId)
      if (task) {
        newState.set(event.data.taskId, {
          ...task,
          status: 'skipped',
        })
      }
      break
    }
  }

  return newState
}

function councilReducer(state: CouncilProjection, event: Delta9Event): CouncilProjection {
  switch (event.type) {
    case 'council.convened':
      return {
        ...state,
        totalConsultations: state.totalConsultations + 1,
        consultationsByMode: {
          ...state.consultationsByMode,
          [event.data.mode]: (state.consultationsByMode[event.data.mode] || 0) + 1,
        },
      }

    case 'council.oracle_responded': {
      const oracleName = event.data.oracle
      const existing = state.oracleStats[oracleName] || {
        consultations: 0,
        avgConfidence: 0,
        timeouts: 0,
      }

      const newCount = existing.consultations + 1
      const newAvgConfidence =
        (existing.avgConfidence * existing.consultations + event.data.confidence) / newCount

      return {
        ...state,
        oracleStats: {
          ...state.oracleStats,
          [oracleName]: {
            consultations: newCount,
            avgConfidence: newAvgConfidence,
            timeouts: existing.timeouts,
          },
        },
      }
    }

    case 'council.timeout': {
      const newOracleStats = { ...state.oracleStats }
      for (const oracle of event.data.timedOutOracles) {
        const existing = newOracleStats[oracle] || {
          consultations: 0,
          avgConfidence: 0,
          timeouts: 0,
        }
        newOracleStats[oracle] = {
          ...existing,
          timeouts: existing.timeouts + 1,
        }
      }
      return {
        ...state,
        oracleStats: newOracleStats,
      }
    }

    default:
      return state
  }
}

function learningReducer(
  state: Map<string, PatternRecord>,
  event: Delta9Event
): Map<string, PatternRecord> {
  const newState = new Map(state)

  switch (event.type) {
    case 'learning.pattern_learned': {
      const existing = newState.get(event.data.pattern)
      if (existing) {
        newState.set(event.data.pattern, {
          ...existing,
          confidence: event.data.confidence,
        })
      } else {
        newState.set(event.data.pattern, {
          pattern: event.data.pattern,
          context: event.data.context,
          confidence: event.data.confidence,
          source: event.data.source,
          applications: 0,
          successes: 0,
          failures: 0,
          lastApplied: null,
          isAntiPattern: false,
        })
      }
      break
    }

    case 'learning.pattern_applied': {
      const existing = newState.get(event.data.pattern)
      if (existing) {
        newState.set(event.data.pattern, {
          ...existing,
          applications: existing.applications + 1,
          successes: event.data.success ? existing.successes + 1 : existing.successes,
          failures: event.data.success ? existing.failures : existing.failures + 1,
          lastApplied: event.timestamp,
        })
      }
      break
    }

    case 'learning.anti_pattern_detected': {
      const existing = newState.get(event.data.pattern)
      if (existing) {
        newState.set(event.data.pattern, {
          ...existing,
          isAntiPattern: true,
        })
      }
      break
    }
  }

  return newState
}

function metricsReducer(state: MetricsProjection, event: Delta9Event): MetricsProjection {
  switch (event.type) {
    case 'task.completed':
      return {
        ...state,
        totalTasks: state.totalTasks + 1,
        successfulTasks: event.data.success ? state.successfulTasks + 1 : state.successfulTasks,
        failedTasks: event.data.success ? state.failedTasks : state.failedTasks + 1,
        totalDuration: state.totalDuration + event.data.duration,
        avgTaskDuration: (state.totalDuration + event.data.duration) / (state.totalTasks + 1),
        totalTokens: state.totalTokens + (event.data.tokensUsed || 0),
      }

    case 'agent.completed':
      return {
        ...state,
        tasksByAgent: {
          ...state.tasksByAgent,
          [event.data.agent]: (state.tasksByAgent[event.data.agent] || 0) + 1,
        },
      }

    case 'agent.error':
      return {
        ...state,
        errorsByCode: {
          ...state.errorsByCode,
          [event.data.errorCode || 'unknown']:
            (state.errorsByCode[event.data.errorCode || 'unknown'] || 0) + 1,
        },
      }

    case 'system.budget_warning':
    case 'system.budget_exceeded':
      return {
        ...state,
        totalBudgetSpent: event.data.spent,
      }

    default:
      return state
  }
}

// =============================================================================
// Projection Builder
// =============================================================================

export class ProjectionBuilder {
  private store: EventStore

  constructor(store?: EventStore) {
    this.store = store || getEventStore()
  }

  /**
   * Build mission projection from events
   */
  buildMissionProjection(missionId?: string): MissionProjection {
    const query = missionId ? { missionId } : undefined
    return this.store.replay(missionReducer, INITIAL_MISSION, query)
  }

  /**
   * Build task projections from events
   */
  buildTaskProjections(missionId?: string): Map<string, TaskProjection> {
    const query = missionId ? { missionId } : undefined
    return this.store.replay(taskReducer, new Map(), query)
  }

  /**
   * Build council projection from events
   */
  buildCouncilProjection(): CouncilProjection {
    const initial: CouncilProjection = {
      totalConsultations: 0,
      consultationsByMode: {},
      oracleStats: {},
      consensusRate: 0,
      avgConsultationTime: 0,
    }

    const projection = this.store.replay(councilReducer, initial, {
      category: 'council',
    })

    // Calculate consensus rate
    const consensusEvents = this.store.query({
      types: ['council.consensus'],
    })

    if (consensusEvents.length > 0) {
      const consensusCount = consensusEvents.filter(
        (e) => (e.data as { hasConsensus: boolean }).hasConsensus
      ).length
      projection.consensusRate = consensusCount / consensusEvents.length
    }

    return projection
  }

  /**
   * Build learning projection from events
   */
  buildLearningProjection(): LearningProjection {
    const patternMap = this.store.replay(learningReducer, new Map(), {
      category: 'learning',
    })

    const patterns: PatternRecord[] = []
    const antiPatterns: PatternRecord[] = []

    for (const record of patternMap.values()) {
      if (record.isAntiPattern) {
        antiPatterns.push(record)
      } else {
        patterns.push(record)
      }
    }

    const totalApplications = [...patternMap.values()].reduce((sum, p) => sum + p.applications, 0)
    const totalSuccesses = [...patternMap.values()].reduce((sum, p) => sum + p.successes, 0)

    return {
      patterns,
      antiPatterns,
      totalPatterns: patterns.length,
      totalAntiPatterns: antiPatterns.length,
      successRate: totalApplications > 0 ? totalSuccesses / totalApplications : 0,
    }
  }

  /**
   * Build metrics projection from events
   */
  buildMetricsProjection(): MetricsProjection {
    const initial: MetricsProjection = {
      totalTasks: 0,
      successfulTasks: 0,
      failedTasks: 0,
      totalDuration: 0,
      avgTaskDuration: 0,
      totalTokens: 0,
      totalBudgetSpent: 0,
      tasksByAgent: {},
      errorsByCode: {},
    }

    return this.store.replay(metricsReducer, initial)
  }

  /**
   * Build all projections at once
   */
  buildAll(missionId?: string): {
    mission: MissionProjection
    tasks: Map<string, TaskProjection>
    council: CouncilProjection
    learning: LearningProjection
    metrics: MetricsProjection
  } {
    return {
      mission: this.buildMissionProjection(missionId),
      tasks: this.buildTaskProjections(missionId),
      council: this.buildCouncilProjection(),
      learning: this.buildLearningProjection(),
      metrics: this.buildMetricsProjection(),
    }
  }
}

// =============================================================================
// Convenience Functions
// =============================================================================

/**
 * Get current mission state from events
 */
export function getCurrentMissionState(missionId?: string): MissionProjection {
  const builder = new ProjectionBuilder()
  return builder.buildMissionProjection(missionId)
}

/**
 * Get task states from events
 */
export function getTaskStates(missionId?: string): Map<string, TaskProjection> {
  const builder = new ProjectionBuilder()
  return builder.buildTaskProjections(missionId)
}

/**
 * Get learning insights
 */
export function getLearningInsights(): LearningProjection {
  const builder = new ProjectionBuilder()
  return builder.buildLearningProjection()
}

/**
 * Get performance metrics
 */
export function getMetrics(): MetricsProjection {
  const builder = new ProjectionBuilder()
  return builder.buildMetricsProjection()
}
