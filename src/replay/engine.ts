/**
 * Replay Engine
 *
 * Re-run past missions with different parameters for A/B testing and comparison.
 */

import { randomUUID } from 'node:crypto'
import type {
  ReplayConfig,
  MissionSnapshot,
  ReplayResult,
  ComparisonReport,
  ReplayEvent,
  ReplayEventType,
} from './types.js'

// =============================================================================
// Replay Engine
// =============================================================================

export class ReplayEngine {
  private snapshots: Map<string, MissionSnapshot> = new Map()
  private replays: Map<string, ReplayResult> = new Map()
  private events: ReplayEvent[] = []
  private eventHandlers: Array<(event: ReplayEvent) => void> = []

  // ===========================================================================
  // Snapshot Management
  // ===========================================================================

  /**
   * Save a mission snapshot for future replay
   */
  saveSnapshot(snapshot: MissionSnapshot): void {
    this.snapshots.set(snapshot.id, snapshot)
  }

  /**
   * Get a snapshot by ID
   */
  getSnapshot(id: string): MissionSnapshot | undefined {
    return this.snapshots.get(id)
  }

  /**
   * List all snapshots
   */
  listSnapshots(): MissionSnapshot[] {
    return Array.from(this.snapshots.values())
      .sort((a, b) => new Date(b.createdAt).getTime() - new Date(a.createdAt).getTime())
  }

  /**
   * Delete a snapshot
   */
  deleteSnapshot(id: string): boolean {
    return this.snapshots.delete(id)
  }

  // ===========================================================================
  // Replay Execution
  // ===========================================================================

  /**
   * Start a replay
   */
  async startReplay(
    config: ReplayConfig,
    executor: (task: unknown, modifications: Record<string, unknown>) => Promise<{
      success: boolean
      result?: unknown
      error?: string
      cost?: number
      time?: number
    }>
  ): Promise<ReplayResult> {
    const snapshot = this.snapshots.get(config.sourceId)
    if (!snapshot) {
      throw new Error(`Snapshot not found: ${config.sourceId}`)
    }

    const replayId = `replay_${randomUUID().slice(0, 8)}`
    const startTime = Date.now()

    const result: ReplayResult = {
      replayId,
      sourceId: config.sourceId,
      mode: config.mode,
      startedAt: new Date().toISOString(),
      status: 'running',
      modifications: config.modifications,
      results: { success: false },
      metrics: {
        cost: 0,
        time: 0,
        tasksCompleted: 0,
        tasksFailed: 0,
      },
    }

    this.replays.set(replayId, result)
    this.emitEvent('replay.started', replayId, { sourceId: config.sourceId, mode: config.mode })

    try {
      switch (config.mode) {
        case 'mission':
          await this.replayMission(result, snapshot, config, executor)
          break
        case 'objective':
          await this.replayObjective(result, snapshot, config, executor)
          break
        case 'task':
          await this.replayTask(result, snapshot, config, executor)
          break
        case 'council':
          await this.replayCouncil(result, snapshot, config, executor)
          break
      }

      result.status = result.metrics.tasksFailed === 0 ? 'completed' : 'failed'
      result.results.success = result.status === 'completed'

      // Generate comparison if requested
      if (config.comparison.compareWithOriginal) {
        result.comparison = this.generateComparison(result, snapshot, config)
      }

      this.emitEvent(
        result.status === 'completed' ? 'replay.completed' : 'replay.failed',
        replayId,
        { metrics: result.metrics }
      )
    } catch (error) {
      result.status = 'failed'
      result.results.error = error instanceof Error ? error.message : String(error)
      this.emitEvent('replay.failed', replayId, { error: result.results.error })
    }

    result.completedAt = new Date().toISOString()
    result.metrics.time = Date.now() - startTime

    return result
  }

  /**
   * Replay entire mission
   */
  private async replayMission(
    result: ReplayResult,
    snapshot: MissionSnapshot,
    config: ReplayConfig,
    executor: (task: unknown, modifications: Record<string, unknown>) => Promise<{
      success: boolean
      result?: unknown
      error?: string
      cost?: number
      time?: number
    }>
  ): Promise<void> {
    for (const objective of snapshot.objectives) {
      for (const task of objective.tasks) {
        // Check if task should be skipped
        if (config.modifications.skipTasks?.includes(task.id)) {
          this.emitEvent('replay.task.skipped', result.replayId, { taskId: task.id })
          continue
        }

        this.emitEvent('replay.task.started', result.replayId, { taskId: task.id })

        const taskResult = await executor(task, config.modifications)

        if (taskResult.success) {
          result.metrics.tasksCompleted++
        } else {
          result.metrics.tasksFailed++
        }

        result.metrics.cost += taskResult.cost || 0

        this.emitEvent('replay.task.completed', result.replayId, {
          taskId: task.id,
          success: taskResult.success,
        })
      }
    }
  }

  /**
   * Replay single objective
   */
  private async replayObjective(
    result: ReplayResult,
    snapshot: MissionSnapshot,
    config: ReplayConfig,
    executor: (task: unknown, modifications: Record<string, unknown>) => Promise<{
      success: boolean
      result?: unknown
      error?: string
      cost?: number
      time?: number
    }>
  ): Promise<void> {
    // Find objective by ID (sourceId should be objective ID)
    const objective = snapshot.objectives.find(o =>
      o.id === config.sourceId || o.tasks.some(t => t.id.startsWith(config.sourceId))
    )

    if (!objective) {
      throw new Error(`Objective not found in snapshot`)
    }

    for (const task of objective.tasks) {
      if (config.modifications.skipTasks?.includes(task.id)) {
        continue
      }

      const taskResult = await executor(task, config.modifications)

      if (taskResult.success) {
        result.metrics.tasksCompleted++
      } else {
        result.metrics.tasksFailed++
      }

      result.metrics.cost += taskResult.cost || 0
    }
  }

  /**
   * Replay single task
   */
  private async replayTask(
    result: ReplayResult,
    snapshot: MissionSnapshot,
    config: ReplayConfig,
    executor: (task: unknown, modifications: Record<string, unknown>) => Promise<{
      success: boolean
      result?: unknown
      error?: string
      cost?: number
      time?: number
    }>
  ): Promise<void> {
    // Find task by ID
    let targetTask: unknown = null

    for (const objective of snapshot.objectives) {
      for (const task of objective.tasks) {
        if (task.id === config.sourceId) {
          targetTask = task
          break
        }
      }
      if (targetTask) break
    }

    if (!targetTask) {
      throw new Error(`Task not found: ${config.sourceId}`)
    }

    const taskResult = await executor(targetTask, config.modifications)

    if (taskResult.success) {
      result.metrics.tasksCompleted++
    } else {
      result.metrics.tasksFailed++
    }

    result.metrics.cost += taskResult.cost || 0
    result.results.outcome = taskResult.result
  }

  /**
   * Replay council deliberation
   */
  private async replayCouncil(
    result: ReplayResult,
    snapshot: MissionSnapshot,
    config: ReplayConfig,
    executor: (task: unknown, modifications: Record<string, unknown>) => Promise<{
      success: boolean
      result?: unknown
      error?: string
      cost?: number
      time?: number
    }>
  ): Promise<void> {
    this.emitEvent('replay.council.started', result.replayId, {
      oracles: config.modifications.oracles,
      mode: config.modifications.councilMode,
    })

    // Execute council with modifications
    const councilResult = await executor(
      { type: 'council', snapshot: snapshot.councilResponses },
      config.modifications
    )

    result.metrics.cost += councilResult.cost || 0
    result.results.outcome = councilResult.result
    result.metrics.tasksCompleted = councilResult.success ? 1 : 0
    result.metrics.tasksFailed = councilResult.success ? 0 : 1

    this.emitEvent('replay.council.completed', result.replayId, {
      success: councilResult.success,
    })
  }

  // ===========================================================================
  // Comparison
  // ===========================================================================

  /**
   * Generate comparison between replay and original
   */
  private generateComparison(
    result: ReplayResult,
    snapshot: MissionSnapshot,
    config: ReplayConfig
  ): ReplayResult['comparison'] {
    const original = snapshot.metrics || {
      totalCost: 0,
      totalTime: 0,
      tasksCompleted: snapshot.objectives.reduce((sum, o) =>
        sum + o.tasks.filter(t => t.status === 'completed').length, 0),
      tasksFailed: snapshot.objectives.reduce((sum, o) =>
        sum + o.tasks.filter(t => t.status === 'failed').length, 0),
      councilConsensus: 0,
    }

    const originalMetrics: Record<string, number> = {
      cost: original.totalCost || 0,
      time: original.totalTime || 0,
      tasksCompleted: original.tasksCompleted,
      tasksFailed: original.tasksFailed,
    }

    const replayMetrics: Record<string, number> = {
      cost: result.metrics.cost,
      time: result.metrics.time,
      tasksCompleted: result.metrics.tasksCompleted,
      tasksFailed: result.metrics.tasksFailed,
    }

    const improvements = config.comparison.metrics.map(metric => {
      const origValue = originalMetrics[metric] || 0
      const replayValue = replayMetrics[metric] || 0

      // Lower is better for cost, time, tasksFailed
      const lowerIsBetter = ['cost', 'time', 'tasksFailed'].includes(metric)
      const change = origValue > 0 ? ((replayValue - origValue) / origValue) * 100 : 0
      const improved = lowerIsBetter ? replayValue < origValue : replayValue > origValue

      return {
        metric,
        originalValue: origValue,
        replayValue,
        change: Math.round(change * 100) / 100,
        improved,
      }
    })

    const improvedCount = improvements.filter(i => i.improved).length
    const totalMetrics = improvements.length
    const summary = improvedCount > totalMetrics / 2
      ? `Replay improved ${improvedCount}/${totalMetrics} metrics`
      : improvedCount === totalMetrics / 2
        ? `Replay tied with original`
        : `Original performed better on ${totalMetrics - improvedCount}/${totalMetrics} metrics`

    return {
      original: originalMetrics,
      replay: replayMetrics,
      improvements,
      summary,
    }
  }

  /**
   * Generate detailed comparison report
   */
  generateComparisonReport(replayId: string): ComparisonReport | null {
    const replay = this.replays.get(replayId)
    if (!replay || !replay.comparison) {
      return null
    }

    const snapshot = this.snapshots.get(replay.sourceId)
    if (!snapshot) {
      return null
    }

    const improvements: string[] = []
    const regressions: string[] = []

    for (const imp of replay.comparison.improvements) {
      if (imp.improved) {
        improvements.push(`${imp.metric}: ${Math.abs(imp.change).toFixed(1)}% improvement`)
      } else if (imp.change !== 0) {
        regressions.push(`${imp.metric}: ${Math.abs(imp.change).toFixed(1)}% regression`)
      }
    }

    const recommendations: string[] = []
    if (improvements.length > regressions.length) {
      recommendations.push('Consider adopting the replay configuration')
      for (const [key, value] of Object.entries(replay.modifications)) {
        if (value !== undefined) {
          recommendations.push(`Keep modification: ${key}`)
        }
      }
    } else {
      recommendations.push('Original configuration performed better')
    }

    const delta: Record<string, number> = {}
    const percentChange: Record<string, number> = {}

    for (const metric of Object.keys(replay.comparison.original)) {
      delta[metric] = replay.comparison.replay[metric] - replay.comparison.original[metric]
      percentChange[metric] = replay.comparison.original[metric] > 0
        ? (delta[metric] / replay.comparison.original[metric]) * 100
        : 0
    }

    const winner = improvements.length > regressions.length
      ? 'replay'
      : improvements.length < regressions.length
        ? 'original'
        : 'tie'

    return {
      replayId,
      sourceId: replay.sourceId,
      timestamp: new Date().toISOString(),
      modifications: replay.modifications as Record<string, unknown>,
      metrics: {
        original: replay.comparison.original,
        replay: replay.comparison.replay,
        delta,
        percentChange,
      },
      improvements,
      regressions,
      recommendations,
      winner,
      confidence: Math.abs(improvements.length - regressions.length) / Math.max(1, improvements.length + regressions.length),
    }
  }

  // ===========================================================================
  // Event System
  // ===========================================================================

  private emitEvent(type: ReplayEventType, replayId: string, data: Record<string, unknown>): void {
    const event: ReplayEvent = {
      type,
      timestamp: new Date().toISOString(),
      replayId,
      data,
    }

    this.events.push(event)

    for (const handler of this.eventHandlers) {
      try {
        handler(event)
      } catch {
        // Ignore handler errors
      }
    }
  }

  onEvent(handler: (event: ReplayEvent) => void): () => void {
    this.eventHandlers.push(handler)
    return () => {
      const index = this.eventHandlers.indexOf(handler)
      if (index >= 0) {
        this.eventHandlers.splice(index, 1)
      }
    }
  }

  // ===========================================================================
  // Getters
  // ===========================================================================

  getReplay(replayId: string): ReplayResult | undefined {
    return this.replays.get(replayId)
  }

  listReplays(): ReplayResult[] {
    return Array.from(this.replays.values())
      .sort((a, b) => new Date(b.startedAt).getTime() - new Date(a.startedAt).getTime())
  }

  getEvents(replayId?: string): ReplayEvent[] {
    if (replayId) {
      return this.events.filter(e => e.replayId === replayId)
    }
    return [...this.events]
  }

  /**
   * Cancel a running replay
   */
  cancelReplay(replayId: string): boolean {
    const replay = this.replays.get(replayId)
    if (replay && replay.status === 'running') {
      replay.status = 'cancelled'
      replay.completedAt = new Date().toISOString()
      this.emitEvent('replay.cancelled', replayId, {})
      return true
    }
    return false
  }
}
