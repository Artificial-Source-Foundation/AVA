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
} from './types.js'
import { DEFAULT_LEGION_CONFIG } from './types.js'

// =============================================================================
// Legion Coordinator
// =============================================================================

export class LegionCoordinator {
  private config: LegionConfig
  private operators: Map<string, LegionOperator> = new Map()
  private activeStrikes: Map<string, LegionStrike> = new Map()
  private events: LegionEvent[] = []
  private eventHandlers: Array<(event: LegionEvent) => void> = []

  constructor(config: Partial<LegionConfig> = {}) {
    this.config = { ...DEFAULT_LEGION_CONFIG, ...config }
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

    for (let i = 0; i < this.config.maxOperators; i++) {
      const operator: LegionOperator = {
        id: `operator_${i}`,
        model: 'anthropic/claude-sonnet-4', // Default model
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
    const availableOperators = operators.filter(op => op.status !== 'offline')

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
        const depsComplete = deps.every(d => completed.has(d))

        if (depsComplete) {
          wave.push(task)
        }
      }

      if (wave.length === 0 && completed.size < tasks.length) {
        // Circular dependency - break it
        const remaining = tasks.filter(t => !completed.has(t.id))
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
        const sorted = [...operators].sort((a, b) =>
          (a.tasksCompleted + (a.currentTask ? 1 : 0)) -
          (b.tasksCompleted + (b.currentTask ? 1 : 0))
        )
        return sorted[0]
      }

      case 'specialty_match': {
        // Find operator with matching specialty
        const taskKeywords = task.description.toLowerCase()
        const matched = operators.find(op =>
          op.specialties.some(s => taskKeywords.includes(s.toLowerCase()))
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
    executor: (task: LegionTask, operator: LegionOperator) => Promise<{
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

    // Execute wave by wave
    for (let waveIndex = 0; waveIndex < plan.waves; waveIndex++) {
      const waveAssignments = plan.assignments.filter(a => a.wave === waveIndex)

      // Execute tasks in parallel within each wave
      const wavePromises = waveAssignments.map(async assignment => {
        const task = strike.tasks.find(t => t.id === assignment.taskId)!
        const operator = strike.operators.find(o => o.id === assignment.operatorId)!

        return this.executeTask(strike, task, operator, executor)
      })

      await Promise.all(wavePromises)

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
    strike.metrics = this.calculateMetrics(strike)

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
    executor: (task: LegionTask, operator: LegionOperator) => Promise<{
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
      }
    } finally {
      task.completedAt = new Date().toISOString()
      operator.status = 'idle'
      operator.currentTask = undefined
      operator.lastActivityAt = new Date().toISOString()

      // Update average task time
      if (task.startedAt && task.completedAt) {
        const taskTime = new Date(task.completedAt).getTime() - new Date(task.startedAt).getTime()
        const totalTime = operator.averageTaskTime * (operator.tasksCompleted + operator.tasksFailed - 1)
        operator.averageTaskTime = (totalTime + taskTime) / (operator.tasksCompleted + operator.tasksFailed)
      }
    }
  }

  // ===========================================================================
  // Conflict Detection & Resolution
  // ===========================================================================

  /**
   * Detect conflicts between completed tasks
   */
  detectConflicts(strike: LegionStrike): Conflict[] {
    const conflicts: Conflict[] = []
    const completedTasks = strike.tasks.filter(t => t.status === 'completed')

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
    const failedTasks = strike.tasks.filter(t => t.status === 'failed')
    const unresolvedConflicts = strike.conflicts.filter(c => c.status !== 'resolved')

    if (failedTasks.length === 0 && unresolvedConflicts.length === 0) {
      return 'completed'
    }

    return 'failed'
  }

  /**
   * Calculate strike metrics
   */
  private calculateMetrics(strike: LegionStrike): LegionStrike['metrics'] {
    const completedTasks = strike.tasks.filter(t => t.status === 'completed')
    const failedTasks = strike.tasks.filter(t => t.status === 'failed')

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
      return this.events.filter(e => e.strikeId === strikeId)
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
