/**
 * LEGION Coordinator
 *
 * Orchestrates parallel operator execution:
 * - Task distribution across operators
 * - Dependency-aware scheduling
 * - Conflict detection and resolution
 * - Result merging
 */

import { randomUUID } from 'node:crypto'
import type {
  LegionConfig,
  LegionTask,
  LegionOperator,
  LegionStrike,
  Conflict,
  DistributionPlan,
  DistributionStrategy,
  LegionEvent,
  WaveResult,
  WavePolicy,
} from './types.js'
import { DEFAULT_LEGION_CONFIG } from './types.js'
import { loadConfig } from '../lib/config.js'

// =============================================================================
// Legion Coordinator
// =============================================================================

export class LegionCoordinator {
  private config: LegionConfig
  private cwd: string
  private operators: Map<string, LegionOperator> = new Map()
  private activeStrikes: Map<string, LegionStrike> = new Map()
  private events: LegionEvent[] = []
  private eventHandlers: Array<(event: LegionEvent) => void> = []

  constructor(config: Partial<LegionConfig> = {}, cwd: string = process.cwd()) {
    this.config = { ...DEFAULT_LEGION_CONFIG, ...config }
    this.cwd = cwd
  }

  // ===========================================================================
  // Strike Management
  // ===========================================================================

  /**
   * Initialize a new LEGION strike
   */
  async initializeStrike(
    missionId: string,
    tasks: Omit<LegionTask, 'id' | 'status' | 'retryCount' | 'filesModified'>[]
  ): Promise<LegionStrike> {
    const strikeId = `strike_${randomUUID().slice(0, 8)}`

    // Create task objects with IDs
    const legionTasks: LegionTask[] = tasks.map((t, i) => ({
      ...t,
      id: `${strikeId}_task_${i}`,
      status: 'pending' as const,
      retryCount: 0,
      filesModified: [],
    }))

    // Initialize operators
    const operators = this.initializeOperators()

    const strike: LegionStrike = {
      id: strikeId,
      missionId,
      status: 'planning',
      tasks: legionTasks,
      operators,
      startedAt: new Date().toISOString(),
      conflicts: [],
    }

    this.activeStrikes.set(strikeId, strike)
    this.emitEvent('legion.strike.started', strikeId, { taskCount: tasks.length })

    return strike
  }

  /**
   * Initialize operator pool
   */
  private initializeOperators(): LegionOperator[] {
    const operators: LegionOperator[] = []
    // Get operator model from config instead of hardcoding (tier 2 default)
    const delta9Config = loadConfig(this.cwd)
    const operatorModel = delta9Config.operators.tier2Model

    for (let i = 0; i < this.config.maxOperators; i++) {
      const operator: LegionOperator = {
        id: `operator_${i}`,
        model: operatorModel,
        status: 'idle',
        tasksCompleted: 0,
        tasksFailed: 0,
        averageTaskTime: 0,
        specialties: [],
      }
      operators.push(operator)
      this.operators.set(operator.id, operator)
    }

    return operators
  }

  // ===========================================================================
  // Task Distribution
  // ===========================================================================

  /**
   * Create a distribution plan for tasks
   */
  createDistributionPlan(
    strike: LegionStrike,
    strategy: DistributionStrategy = 'dependency_aware'
  ): DistributionPlan {
    const { tasks, operators } = strike
    const availableOperators = operators.filter((op) => op.status !== 'offline')

    // Build dependency graph
    const dependencyGraph = this.buildDependencyGraph(tasks)

    // Topological sort to get execution waves
    const waves = this.topologicalSort(tasks, dependencyGraph)

    // Assign tasks to operators
    const assignments: DistributionPlan['assignments'] = []
    let estimatedTime = 0

    for (let waveIndex = 0; waveIndex < waves.length; waveIndex++) {
      const waveTasks = waves[waveIndex]
      let waveTime = 0

      for (let i = 0; i < waveTasks.length; i++) {
        const task = waveTasks[i]
        const operator = this.selectOperator(task, availableOperators, strategy, i)

        assignments.push({
          taskId: task.id,
          operatorId: operator.id,
          reason: this.getAssignmentReason(strategy, task, operator),
          wave: waveIndex,
        })

        // Estimate time based on complexity
        const taskTime = this.estimateTaskTime(task)
        waveTime = Math.max(waveTime, taskTime)
      }

      estimatedTime += waveTime
    }

    return {
      strategy,
      assignments,
      waves: waves.length,
      estimatedTime,
    }
  }

  /**
   * Build dependency graph from tasks
   */
  private buildDependencyGraph(tasks: LegionTask[]): Map<string, string[]> {
    const graph = new Map<string, string[]>()

    for (const task of tasks) {
      graph.set(task.id, task.dependencies)
    }

    return graph
  }

  /**
   * Topological sort for dependency-aware execution
   */
  private topologicalSort(tasks: LegionTask[], graph: Map<string, string[]>): LegionTask[][] {
    const waves: LegionTask[][] = []
    const completed = new Set<string>()

    while (completed.size < tasks.length) {
      const wave: LegionTask[] = []

      for (const task of tasks) {
        if (completed.has(task.id)) continue

        // Check if all dependencies are completed
        const deps = graph.get(task.id) || []
        const depsComplete = deps.every((d) => completed.has(d))

        if (depsComplete) {
          wave.push(task)
        }
      }

      if (wave.length === 0 && completed.size < tasks.length) {
        // Circular dependency - break it
        const remaining = tasks.filter((t) => !completed.has(t.id))
        wave.push(remaining[0])
      }

      for (const task of wave) {
        completed.add(task.id)
      }

      if (wave.length > 0) {
        waves.push(wave)
      }
    }

    return waves
  }

  /**
   * Select best operator for a task
   */
  private selectOperator(
    task: LegionTask,
    operators: LegionOperator[],
    strategy: DistributionStrategy,
    index: number
  ): LegionOperator {
    switch (strategy) {
      case 'round_robin':
        return operators[index % operators.length]

      case 'load_balanced': {
        // Select operator with least current load
        const sorted = [...operators].sort(
          (a, b) =>
            a.tasksCompleted +
            (a.currentTask ? 1 : 0) -
            (b.tasksCompleted + (b.currentTask ? 1 : 0))
        )
        return sorted[0]
      }

      case 'specialty_match': {
        // Find operator with matching specialty
        const taskKeywords = task.description.toLowerCase()
        const matched = operators.find((op) =>
          op.specialties.some((s) => taskKeywords.includes(s.toLowerCase()))
        )
        return matched || operators[index % operators.length]
      }

      case 'complexity_aware': {
        // Complex tasks go to operators with better track records
        if (task.estimatedComplexity === 'high') {
          const sorted = [...operators].sort((a, b) => {
            const aSuccessRate = a.tasksCompleted / Math.max(1, a.tasksCompleted + a.tasksFailed)
            const bSuccessRate = b.tasksCompleted / Math.max(1, b.tasksCompleted + b.tasksFailed)
            return bSuccessRate - aSuccessRate
          })
          return sorted[0]
        }
        return operators[index % operators.length]
      }

      case 'dependency_aware':
      default:
        // Default to load balanced
        return this.selectOperator(task, operators, 'load_balanced', index)
    }
  }

  /**
   * Get reason for assignment (for logging)
   */
  private getAssignmentReason(
    strategy: DistributionStrategy,
    task: LegionTask,
    operator: LegionOperator
  ): string {
    switch (strategy) {
      case 'round_robin':
        return 'Round-robin distribution'
      case 'load_balanced':
        return `Lowest load (${operator.tasksCompleted} tasks completed)`
      case 'specialty_match':
        return `Specialty match: ${operator.specialties.join(', ') || 'general'}`
      case 'complexity_aware':
        return `Complexity-based (task: ${task.estimatedComplexity})`
      case 'dependency_aware':
        return 'Dependency-aware scheduling'
      default:
        return 'Default assignment'
    }
  }

  /**
   * Estimate task execution time in ms
   */
  private estimateTaskTime(task: LegionTask): number {
    const baseTime = 30000 // 30 seconds base
    const complexityMultiplier = {
      low: 1,
      medium: 2,
      high: 4,
    }
    return baseTime * complexityMultiplier[task.estimatedComplexity]
  }

  // ===========================================================================
  // Execution
  // ===========================================================================

  /**
   * Execute a strike (simulate parallel execution)
   */
  async executeStrike(
    strikeId: string,
    executor: (
      task: LegionTask,
      operator: LegionOperator
    ) => Promise<{
      success: boolean
      result?: unknown
      error?: string
      filesModified?: string[]
    }>
  ): Promise<LegionStrike> {
    const strike = this.activeStrikes.get(strikeId)
    if (!strike) {
      throw new Error(`Strike not found: ${strikeId}`)
    }

    strike.status = 'executing'
    const plan = this.createDistributionPlan(strike)

    // Track wave results for partial success tracking
    const waveResults: WaveResult[] = []

    // Execute wave by wave
    for (let waveIndex = 0; waveIndex < plan.waves; waveIndex++) {
      const waveStartTime = Date.now()
      const waveAssignments = plan.assignments.filter((a) => a.wave === waveIndex)
      const waveTaskIds = waveAssignments.map((a) => a.taskId)

      this.emitEvent('legion.wave.started', strikeId, {
        waveIndex,
        taskCount: waveAssignments.length,
      })

      // Execute tasks in parallel within each wave
      const wavePromises = waveAssignments.map(async (assignment) => {
        const task = strike.tasks.find((t) => t.id === assignment.taskId)!
        const operator = strike.operators.find((o) => o.id === assignment.operatorId)!

        return this.executeTask(strike, task, operator, executor)
      })

      await Promise.all(wavePromises)

      // Calculate wave result
      const waveTasks = strike.tasks.filter((t) => waveTaskIds.includes(t.id))
      const waveCompleted = waveTasks.filter((t) => t.status === 'completed').length
      const waveFailed = waveTasks.filter((t) => t.status === 'failed').length
      const waveDuration = Date.now() - waveStartTime
      const waveSuccessRate = waveTasks.length > 0 ? waveCompleted / waveTasks.length : 0

      const waveResult: WaveResult = {
        waveIndex,
        totalTasks: waveTasks.length,
        completedTasks: waveCompleted,
        failedTasks: waveFailed,
        successRate: Math.round(waveSuccessRate * 100) / 100,
        status: waveSuccessRate === 1 ? 'complete' : waveSuccessRate > 0 ? 'partial' : 'failed',
        durationMs: waveDuration,
        taskIds: waveTaskIds,
      }
      waveResults.push(waveResult)

      this.emitEvent('legion.wave.completed', strikeId, waveResult)

      // Check wave policy (A-2: Wave Advancement Policies)
      const policyResult = this.checkWavePolicy(waveResult)
      if (!policyResult.canAdvance) {
        this.emitEvent('legion.wave.policy_failed', strikeId, {
          waveIndex,
          policy: this.config.wavePolicy,
          successRate: waveResult.successRate,
          reason: policyResult.reason,
        })

        if (this.config.abortOnWaveFailure) {
          strike.status = 'failed'
          strike.completedAt = new Date().toISOString()
          strike.metrics = this.calculateMetrics(strike, waveResults)
          this.emitEvent('legion.strike.aborted', strikeId, {
            reason: `Wave ${waveIndex} failed policy check: ${policyResult.reason}`,
            metrics: strike.metrics,
          })
          return strike
        }

        if (!this.config.continueOnPartialWave) {
          // Stop processing further waves but don't abort
          break
        }
      }

      // Check for conflicts after each wave
      const conflicts = this.detectConflicts(strike)
      if (conflicts.length > 0) {
        strike.conflicts.push(...conflicts)

        if (this.config.autoResolveConflicts) {
          await this.resolveConflicts(strike, conflicts)
        }
      }
    }

    // Merge results
    strike.status = 'merging'
    this.emitEvent('legion.merge.started', strikeId, {})
    await this.mergeResults(strike)
    this.emitEvent('legion.merge.completed', strikeId, {})

    // Finalize
    strike.status = this.calculateStrikeStatus(strike)
    strike.completedAt = new Date().toISOString()
    strike.metrics = this.calculateMetrics(strike, waveResults)

    this.emitEvent(
      strike.status === 'completed' ? 'legion.strike.completed' : 'legion.strike.failed',
      strikeId,
      { metrics: strike.metrics }
    )

    return strike
  }

  /**
   * Execute a single task
   */
  private async executeTask(
    strike: LegionStrike,
    task: LegionTask,
    operator: LegionOperator,
    executor: (
      task: LegionTask,
      operator: LegionOperator
    ) => Promise<{
      success: boolean
      result?: unknown
      error?: string
      filesModified?: string[]
    }>
  ): Promise<void> {
    task.status = 'assigned'
    task.assignedOperator = operator.id
    operator.status = 'busy'
    operator.currentTask = task.id

    this.emitEvent('legion.task.assigned', strike.id, { taskId: task.id, operatorId: operator.id })

    task.status = 'running'
    task.startedAt = new Date().toISOString()
    this.emitEvent('legion.task.started', strike.id, { taskId: task.id })

    try {
      const result = await Promise.race([
        executor(task, operator),
        new Promise<never>((_, reject) =>
          setTimeout(() => reject(new Error('Task timeout')), this.config.taskTimeout)
        ),
      ])

      if (result.success) {
        task.status = 'completed'
        task.result = result.result
        task.filesModified = result.filesModified || []
        operator.tasksCompleted++
        this.emitEvent('legion.task.completed', strike.id, { taskId: task.id })
      } else {
        throw new Error(result.error || 'Task failed')
      }
    } catch (error) {
      task.status = 'failed'
      task.error = error instanceof Error ? error.message : String(error)
      operator.tasksFailed++
      this.emitEvent('legion.task.failed', strike.id, { taskId: task.id, error: task.error })

      // Retry if configured
      if (this.config.retryFailed && task.retryCount < this.config.maxRetries) {
        task.retryCount++
        task.status = 'pending'
        // Will be picked up in next wave or retry cycle
      } else {
        // A-6: Propagate failure to dependent tasks
        this.propagateDependencyFailure(strike, task)
      }
    } finally {
      task.completedAt = new Date().toISOString()
      operator.status = 'idle'
      operator.currentTask = undefined
      operator.lastActivityAt = new Date().toISOString()

      // Update average task time
      if (task.startedAt && task.completedAt) {
        const taskTime = new Date(task.completedAt).getTime() - new Date(task.startedAt).getTime()
        const totalTime =
          operator.averageTaskTime * (operator.tasksCompleted + operator.tasksFailed - 1)
        operator.averageTaskTime =
          (totalTime + taskTime) / (operator.tasksCompleted + operator.tasksFailed)
      }
    }
  }

  // ===========================================================================
  // Wave Policy Check (A-2)
  // ===========================================================================

  /**
   * Check if wave result meets the configured policy for advancement
   */
  private checkWavePolicy(waveResult: WaveResult): { canAdvance: boolean; reason: string } {
    const { successRate, totalTasks, completedTasks, failedTasks } = waveResult
    const policy = this.config.wavePolicy

    switch (policy) {
      case 'strict':
        // All tasks must succeed
        if (successRate === 1) {
          return { canAdvance: true, reason: 'All tasks completed successfully' }
        }
        return {
          canAdvance: false,
          reason: `Strict policy requires 100% success, got ${(successRate * 100).toFixed(0)}% (${failedTasks} failed)`,
        }

      case 'majority':
        // More than 50% must succeed
        if (successRate > 0.5) {
          return {
            canAdvance: true,
            reason: `Majority succeeded: ${(successRate * 100).toFixed(0)}%`,
          }
        }
        return {
          canAdvance: false,
          reason: `Majority policy requires >50% success, got ${(successRate * 100).toFixed(0)}%`,
        }

      case 'any':
        // At least one task must succeed
        if (completedTasks >= 1) {
          return {
            canAdvance: true,
            reason: `At least one task succeeded (${completedTasks}/${totalTasks})`,
          }
        }
        return {
          canAdvance: false,
          reason: `Any policy requires at least 1 success, all ${totalTasks} tasks failed`,
        }

      case 'threshold': {
        // Custom threshold
        const threshold = this.config.minWaveSuccessRate
        if (successRate >= threshold) {
          return {
            canAdvance: true,
            reason: `Success rate ${(successRate * 100).toFixed(0)}% meets threshold ${(threshold * 100).toFixed(0)}%`,
          }
        }
        return {
          canAdvance: false,
          reason: `Success rate ${(successRate * 100).toFixed(0)}% below threshold ${(threshold * 100).toFixed(0)}%`,
        }
      }

      default:
        // Default to majority
        return {
          canAdvance: successRate > 0.5,
          reason: `Default policy: ${successRate > 0.5 ? 'passed' : 'failed'}`,
        }
    }
  }

  // ===========================================================================
  // Dependency Failure Propagation (A-6)
  // ===========================================================================

  /**
   * Propagate failure to all tasks that depend on a failed task.
   * This prevents dependent tasks from executing and marks them appropriately.
   */
  private propagateDependencyFailure(strike: LegionStrike, failedTask: LegionTask): void {
    const affectedTasks = this.findDependentTasks(strike, failedTask.id)

    if (affectedTasks.length === 0) return

    this.emitEvent('legion.task.dependency_failed', strike.id, {
      failedTaskId: failedTask.id,
      affectedTaskIds: affectedTasks.map((t) => t.id),
      affectedCount: affectedTasks.length,
    })

    for (const task of affectedTasks) {
      if (task.status === 'pending' || task.status === 'assigned') {
        task.status = 'failed'
        task.error = `Dependency failed: task ${failedTask.id} (${failedTask.description.slice(0, 50)})`
        task.completedAt = new Date().toISOString()

        this.emitEvent('legion.task.failed', strike.id, {
          taskId: task.id,
          error: task.error,
          reason: 'dependency_failure',
          dependsOn: failedTask.id,
        })
      }
    }
  }

  /**
   * Find all tasks that transitively depend on a given task
   */
  private findDependentTasks(strike: LegionStrike, taskId: string): LegionTask[] {
    const dependents: LegionTask[] = []
    const visited = new Set<string>()

    const findRecursive = (targetId: string): void => {
      for (const task of strike.tasks) {
        if (visited.has(task.id)) continue
        if (task.dependencies.includes(targetId)) {
          visited.add(task.id)
          dependents.push(task)
          // Recursively find tasks that depend on this one
          findRecursive(task.id)
        }
      }
    }

    findRecursive(taskId)
    return dependents
  }

  /**
   * Check if a task can execute (all dependencies satisfied)
   */
  canExecuteTask(strike: LegionStrike, task: LegionTask): { canExecute: boolean; reason: string } {
    for (const depId of task.dependencies) {
      const depTask = strike.tasks.find((t) => t.id === depId)
      if (!depTask) {
        return { canExecute: false, reason: `Dependency ${depId} not found` }
      }
      if (depTask.status === 'failed') {
        return {
          canExecute: false,
          reason: `Dependency ${depId} failed: ${depTask.error || 'unknown error'}`,
        }
      }
      if (depTask.status !== 'completed') {
        return { canExecute: false, reason: `Dependency ${depId} not yet completed` }
      }
    }
    return { canExecute: true, reason: 'All dependencies satisfied' }
  }

  /**
   * Get recommended policy based on task characteristics
   */
  getRecommendedPolicy(tasks: LegionTask[]): WavePolicy {
    // If tasks have many dependencies, use strict (failures cascade)
    const avgDeps = tasks.reduce((sum, t) => sum + t.dependencies.length, 0) / tasks.length
    if (avgDeps > 2) {
      return 'strict'
    }

    // If tasks are high priority, use strict
    const avgPriority = tasks.reduce((sum, t) => sum + t.priority, 0) / tasks.length
    if (avgPriority >= 8) {
      return 'strict'
    }

    // If tasks are complex, be more lenient
    const complexTasks = tasks.filter((t) => t.estimatedComplexity === 'high').length
    if (complexTasks > tasks.length / 2) {
      return 'any'
    }

    // Default to majority
    return 'majority'
  }

  // ===========================================================================
  // Conflict Detection & Resolution
  // ===========================================================================

  /**
   * Detect conflicts between completed tasks
   */
  detectConflicts(strike: LegionStrike): Conflict[] {
    const conflicts: Conflict[] = []
    const completedTasks = strike.tasks.filter((t) => t.status === 'completed')

    // Check for file collisions
    const fileModifications = new Map<string, string[]>()

    for (const task of completedTasks) {
      for (const file of task.filesModified) {
        const existing = fileModifications.get(file) || []
        existing.push(task.id)
        fileModifications.set(file, existing)
      }
    }

    for (const [file, taskIds] of fileModifications) {
      if (taskIds.length > 1) {
        conflicts.push({
          id: `conflict_${randomUUID().slice(0, 8)}`,
          strikeId: strike.id,
          taskIds,
          conflictType: 'file_collision',
          files: [file],
          description: `Multiple tasks modified ${file}`,
          status: 'detected',
        })
      }
    }

    return conflicts
  }

  /**
   * Resolve conflicts
   */
  async resolveConflicts(strike: LegionStrike, conflicts: Conflict[]): Promise<void> {
    for (const conflict of conflicts) {
      this.emitEvent('legion.conflict.detected', strike.id, { conflictId: conflict.id })

      conflict.status = 'analyzing'

      // Auto-resolution based on merge strategy
      let strategy: 'merge' | 'prefer_first' | 'prefer_last' | 'manual' | 'retry_sequential'

      switch (this.config.mergeStrategy) {
        case 'sequential':
          strategy = 'prefer_last'
          break
        case 'parallel':
          strategy = 'merge'
          break
        case 'smart':
        default:
          // Smart: prefer merge for small changes, sequential for large
          strategy = conflict.files.length > 3 ? 'retry_sequential' : 'merge'
      }

      conflict.resolution = {
        strategy,
        appliedAt: new Date().toISOString(),
        appliedBy: 'legion_coordinator',
      }
      conflict.status = 'resolved'

      this.emitEvent('legion.conflict.resolved', strike.id, {
        conflictId: conflict.id,
        strategy,
      })
    }
  }

  /**
   * Merge results from all tasks
   */
  async mergeResults(_strike: LegionStrike): Promise<void> {
    // For now, results are already stored per-task
    // This would be where we'd apply conflict resolutions and
    // combine outputs if needed
  }

  // ===========================================================================
  // Metrics & Status
  // ===========================================================================

  /**
   * Calculate final strike status
   */
  private calculateStrikeStatus(strike: LegionStrike): 'completed' | 'failed' {
    const failedTasks = strike.tasks.filter((t) => t.status === 'failed')
    const unresolvedConflicts = strike.conflicts.filter((c) => c.status !== 'resolved')

    if (failedTasks.length === 0 && unresolvedConflicts.length === 0) {
      return 'completed'
    }

    return 'failed'
  }

  /**
   * Calculate strike metrics
   */
  private calculateMetrics(
    strike: LegionStrike,
    waveResults: WaveResult[] = []
  ): LegionStrike['metrics'] {
    const completedTasks = strike.tasks.filter((t) => t.status === 'completed')
    const failedTasks = strike.tasks.filter((t) => t.status === 'failed')

    const startTime = new Date(strike.startedAt).getTime()
    const endTime = strike.completedAt ? new Date(strike.completedAt).getTime() : Date.now()
    const totalTime = endTime - startTime

    // Calculate average task time from completed tasks
    let totalTaskTime = 0
    for (const task of completedTasks) {
      if (task.startedAt && task.completedAt) {
        totalTaskTime += new Date(task.completedAt).getTime() - new Date(task.startedAt).getTime()
      }
    }

    // Calculate parallelism (how many tasks ran concurrently on average)
    const theoreticalSequentialTime = totalTaskTime
    const parallelism = theoreticalSequentialTime > 0 ? theoreticalSequentialTime / totalTime : 1

    return {
      totalTasks: strike.tasks.length,
      completedTasks: completedTasks.length,
      failedTasks: failedTasks.length,
      parallelism: Math.round(parallelism * 100) / 100,
      totalTime,
      averageTaskTime: completedTasks.length > 0 ? totalTaskTime / completedTasks.length : 0,
      waveResults: waveResults.length > 0 ? waveResults : undefined,
    }
  }

  // ===========================================================================
  // Event System
  // ===========================================================================

  /**
   * Emit a Legion event
   */
  private emitEvent(
    type: LegionEvent['type'],
    strikeId: string,
    data: Record<string, unknown>
  ): void {
    const event: LegionEvent = {
      type,
      timestamp: new Date().toISOString(),
      strikeId,
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

  /**
   * Subscribe to Legion events
   */
  onEvent(handler: (event: LegionEvent) => void): () => void {
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

  getStrike(strikeId: string): LegionStrike | undefined {
    return this.activeStrikes.get(strikeId)
  }

  getActiveStrikes(): LegionStrike[] {
    return Array.from(this.activeStrikes.values())
  }

  getEvents(strikeId?: string): LegionEvent[] {
    if (strikeId) {
      return this.events.filter((e) => e.strikeId === strikeId)
    }
    return [...this.events]
  }

  getConfig(): LegionConfig {
    return { ...this.config }
  }

  /**
   * Check if task count warrants LEGION mode
   */
  shouldUseLegion(taskCount: number): boolean {
    return this.config.enabled && taskCount >= this.config.minTasksForLegion
  }
}
