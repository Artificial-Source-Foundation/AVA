/**
 * Delta9 Mission State Manager
 *
 * Manages the mission.json state file with CRUD operations.
 * This is the source of truth for mission state.
 */

import { readFileSync, writeFileSync } from 'node:fs'
import { nanoid } from 'nanoid'
import { getNamedLogger } from '../lib/logger.js'

const log = getNamedLogger('mission')
import type {
  Mission,
  Objective,
  Task,
  ValidationResult,
  Complexity,
  MissionProgress,
  BudgetTracking,
} from '../types/mission.js'
import type { CouncilMode } from '../types/config.js'
import { validateMission } from '../schemas/mission.schema.js'
import { getMissionPath, getMissionMdPath, ensureDelta9Dir, missionExists } from '../lib/paths.js'
import { getBudgetLimit } from '../lib/config.js'
import { generateMissionMarkdown } from './markdown.js'
import { appendHistory } from './history.js'

// =============================================================================
// Mission State Class
// =============================================================================

export class MissionState {
  private mission: Mission | null = null
  private cwd: string

  constructor(cwd: string) {
    this.cwd = cwd
  }

  // ===========================================================================
  // Lifecycle
  // ===========================================================================

  /**
   * Create a new mission
   */
  create(
    description: string,
    options: {
      councilMode?: CouncilMode
      complexity?: Complexity
      budgetLimit?: number
    } = {}
  ): Mission {
    const {
      councilMode = 'standard',
      complexity = 'medium',
      budgetLimit = getBudgetLimit(this.cwd),
    } = options

    const now = new Date().toISOString()

    this.mission = {
      id: `mission_${nanoid(10)}`,
      description,
      status: 'planning',
      complexity,
      councilMode,
      objectives: [],
      currentObjective: 0,
      budget: {
        limit: budgetLimit,
        spent: 0,
        breakdown: {
          council: 0,
          operators: 0,
          validators: 0,
          support: 0,
        },
      },
      createdAt: now,
      updatedAt: now,
    }

    this.save()

    // Log history event
    appendHistory(this.cwd, {
      type: 'mission_created',
      timestamp: now,
      missionId: this.mission.id,
      data: { description, councilMode, complexity },
    })

    return this.mission
  }

  /**
   * Load existing mission from disk
   */
  load(): Mission | null {
    if (!missionExists(this.cwd)) {
      this.mission = null
      return null
    }

    try {
      const content = readFileSync(getMissionPath(this.cwd), 'utf-8')
      const data = JSON.parse(content)
      this.mission = validateMission(data)
      return this.mission
    } catch (error) {
      log.error(`Failed to load mission: ${error instanceof Error ? error.message : String(error)}`)
      this.mission = null
      return null
    }
  }

  /**
   * Save current mission to disk
   */
  save(): void {
    if (!this.mission) {
      return
    }

    ensureDelta9Dir(this.cwd)

    // Update timestamp
    this.mission.updatedAt = new Date().toISOString()

    // Save mission.json
    writeFileSync(getMissionPath(this.cwd), JSON.stringify(this.mission, null, 2), 'utf-8')

    // Generate and save mission.md
    const markdown = generateMissionMarkdown(this.mission)
    writeFileSync(getMissionMdPath(this.cwd), markdown, 'utf-8')
  }

  /**
   * Clear mission state (does not delete files)
   */
  clear(): void {
    this.mission = null
  }

  // ===========================================================================
  // Getters
  // ===========================================================================

  /**
   * Get current mission
   */
  getMission(): Mission | null {
    return this.mission
  }

  /**
   * Get mission ID
   */
  getMissionId(): string | null {
    return this.mission?.id ?? null
  }

  /**
   * Get an objective by ID
   */
  getObjective(id: string): Objective | null {
    return this.mission?.objectives.find((o) => o.id === id) ?? null
  }

  /**
   * Get the current objective
   */
  getCurrentObjective(): Objective | null {
    if (!this.mission || this.mission.objectives.length === 0) {
      return null
    }
    return this.mission.objectives[this.mission.currentObjective] ?? null
  }

  /**
   * Get a task by ID (searches all objectives)
   */
  getTask(taskId: string): Task | null {
    if (!this.mission) return null

    for (const objective of this.mission.objectives) {
      const task = objective.tasks.find((t) => t.id === taskId)
      if (task) return task
    }
    return null
  }

  /**
   * Get the next task that's ready to execute
   */
  getNextTask(): Task | null {
    if (!this.mission) return null

    // First, check current objective
    const currentObj = this.getCurrentObjective()
    if (currentObj) {
      const readyTask = this.findReadyTask(currentObj)
      if (readyTask) return readyTask
    }

    // If current objective is complete, check next objectives
    for (let i = this.mission.currentObjective + 1; i < this.mission.objectives.length; i++) {
      const objective = this.mission.objectives[i]
      if (objective.status === 'pending') {
        const readyTask = this.findReadyTask(objective)
        if (readyTask) {
          // Update current objective
          this.mission.currentObjective = i
          objective.status = 'in_progress'
          objective.startedAt = new Date().toISOString()
          this.save()
          return readyTask
        }
      }
    }

    return null
  }

  /**
   * Find a ready task in an objective
   */
  private findReadyTask(objective: Objective): Task | null {
    for (const task of objective.tasks) {
      if (task.status === 'pending' && this.areTaskDependenciesMet(task)) {
        return task
      }
    }
    return null
  }

  /**
   * Check if task dependencies are met
   */
  areTaskDependenciesMet(task: Task): boolean {
    if (!task.dependencies || task.dependencies.length === 0) {
      return true
    }

    for (const depId of task.dependencies) {
      const depTask = this.getTask(depId)
      if (!depTask || depTask.status !== 'completed') {
        return false
      }
    }
    return true
  }

  /**
   * BUG-36 FIX: Re-evaluate blocked tasks after a task completes
   * Transitions blocked → pending if all dependencies met
   *
   * @param completedTaskId - ID of the task that just completed
   * @returns Array of task IDs that were unblocked
   */
  resolveDependenciesAfterCompletion(completedTaskId: string): string[] {
    if (!this.mission) return []

    const unblocked: string[] = []

    for (const objective of this.mission.objectives) {
      for (const task of objective.tasks) {
        // Skip if not blocked or doesn't depend on completed task
        if (task.status !== 'blocked' && task.status !== 'pending') continue
        if (!task.dependencies?.includes(completedTaskId)) continue

        // Check if ALL dependencies now met
        if (this.areTaskDependenciesMet(task)) {
          // Only update if it was blocked
          if (task.status === 'blocked') {
            task.status = 'pending'
            unblocked.push(task.id)

            appendHistory(this.cwd, {
              type: 'task_unblocked',
              timestamp: new Date().toISOString(),
              missionId: this.mission.id,
              taskId: task.id,
              data: {
                unblockedBy: completedTaskId,
                taskDescription: task.description, // BUG-37 FIX
              },
            })
          }
        }
      }
    }

    if (unblocked.length > 0) {
      this.save()
      log.info(`[mission] Task ${completedTaskId} completed, unblocked: ${unblocked.join(', ')}`)
    }

    return unblocked
  }

  /**
   * Get all blocked tasks
   */
  getBlockedTasks(): Task[] {
    if (!this.mission) return []

    const blocked: Task[] = []
    for (const objective of this.mission.objectives) {
      for (const task of objective.tasks) {
        if (task.status === 'pending' && !this.areTaskDependenciesMet(task)) {
          blocked.push(task)
        }
      }
    }
    return blocked
  }

  /**
   * Get all ready tasks
   */
  getReadyTasks(): Task[] {
    if (!this.mission) return []

    const ready: Task[] = []
    for (const objective of this.mission.objectives) {
      if (objective.status === 'pending' || objective.status === 'in_progress') {
        for (const task of objective.tasks) {
          if (task.status === 'pending' && this.areTaskDependenciesMet(task)) {
            ready.push(task)
          }
        }
      }
    }
    return ready
  }

  /**
   * Get mission progress
   */
  getProgress(): MissionProgress {
    if (!this.mission) {
      return {
        total: 0,
        completed: 0,
        inProgress: 0,
        failed: 0,
        blocked: 0,
        pending: 0,
        percentage: 0,
      }
    }

    let total = 0
    let completed = 0
    let inProgress = 0
    let failed = 0
    let blocked = 0
    let pending = 0

    for (const objective of this.mission.objectives) {
      for (const task of objective.tasks) {
        total++
        switch (task.status) {
          case 'completed':
            completed++
            break
          case 'in_progress':
            inProgress++
            break
          case 'failed':
            failed++
            break
          case 'blocked':
            blocked++
            break
          case 'pending':
            if (!this.areTaskDependenciesMet(task)) {
              blocked++
            } else {
              pending++
            }
            break
        }
      }
    }

    const percentage = total > 0 ? Math.round((completed / total) * 100) : 0

    return { total, completed, inProgress, failed, blocked, pending, percentage }
  }

  // ===========================================================================
  // Updates
  // ===========================================================================

  /**
   * Update mission fields
   */
  updateMission(updates: Partial<Mission>): void {
    if (!this.mission) return

    Object.assign(this.mission, updates)
    this.save()
  }

  /**
   * Add an objective to the mission
   */
  addObjective(objectiveData: Omit<Objective, 'id' | 'status' | 'tasks'>): Objective {
    if (!this.mission) {
      throw new Error('No active mission')
    }

    const objective: Objective = {
      id: `obj_${nanoid(6)}`,
      status: 'pending',
      tasks: [],
      ...objectiveData,
    }

    this.mission.objectives.push(objective)
    this.save()

    return objective
  }

  /**
   * Update an objective
   */
  updateObjective(id: string, updates: Partial<Objective>): void {
    const objective = this.getObjective(id)
    if (!objective) return

    Object.assign(objective, updates)
    this.save()
  }

  /**
   * Add a task to an objective
   */
  addTask(objectiveId: string, taskData: Omit<Task, 'id' | 'status' | 'attempts'>): Task {
    const objective = this.getObjective(objectiveId)
    if (!objective) {
      throw new Error(`Objective ${objectiveId} not found`)
    }

    const task: Task = {
      id: `task_${nanoid(6)}`,
      status: 'pending',
      attempts: 0,
      ...taskData,
    }

    objective.tasks.push(task)
    this.save()

    return task
  }

  /**
   * Update a task
   */
  updateTask(taskId: string, updates: Partial<Task>): void {
    const task = this.getTask(taskId)
    if (!task) return

    Object.assign(task, updates)
    this.save()
  }

  // ===========================================================================
  // Status Transitions
  // ===========================================================================

  /**
   * Approve the mission (transition from planning to approved)
   */
  approveMission(): void {
    if (!this.mission || this.mission.status !== 'planning') return

    this.mission.status = 'approved'
    this.mission.approvedAt = new Date().toISOString()
    this.save()

    appendHistory(this.cwd, {
      type: 'mission_approved',
      timestamp: this.mission.approvedAt,
      missionId: this.mission.id,
    })
  }

  /**
   * Start mission execution
   */
  startMission(): void {
    if (!this.mission) return
    if (this.mission.status !== 'approved' && this.mission.status !== 'paused') return

    this.mission.status = 'in_progress'

    // Start first objective if none started
    if (this.mission.objectives.length > 0 && this.mission.currentObjective === 0) {
      const firstObj = this.mission.objectives[0]
      if (firstObj.status === 'pending') {
        firstObj.status = 'in_progress'
        firstObj.startedAt = new Date().toISOString()
      }
    }

    this.save()
  }

  /**
   * Pause the mission
   */
  pauseMission(): void {
    if (!this.mission || this.mission.status !== 'in_progress') return

    this.mission.status = 'paused'
    this.save()

    appendHistory(this.cwd, {
      type: 'mission_paused',
      timestamp: new Date().toISOString(),
      missionId: this.mission.id,
    })
  }

  /**
   * Abort the mission
   */
  abortMission(): void {
    if (!this.mission) return

    this.mission.status = 'aborted'
    this.save()

    appendHistory(this.cwd, {
      type: 'mission_aborted',
      timestamp: new Date().toISOString(),
      missionId: this.mission.id,
    })
  }

  /**
   * Start a task
   */
  startTask(taskId: string, assignee: string): void {
    const task = this.getTask(taskId)
    if (!task || task.status !== 'pending') return

    task.status = 'in_progress'
    task.assignedTo = assignee
    task.startedAt = new Date().toISOString()
    task.attempts++

    this.save()

    // BUG-38 FIX: Auto-transition mission status on first task start
    this.autoTransitionMissionStatus()

    appendHistory(this.cwd, {
      type: 'task_started',
      timestamp: task.startedAt,
      missionId: this.mission!.id,
      taskId,
      data: {
        assignee,
        attempt: task.attempts,
        taskDescription: task.description, // BUG-37 FIX: Include task description
      },
    })
  }

  /**
   * Complete a task with validation
   */
  completeTask(taskId: string, validation: ValidationResult): void {
    const task = this.getTask(taskId)
    if (!task) return

    task.validation = validation
    task.completedAt = new Date().toISOString()

    if (validation.status === 'pass') {
      task.status = 'completed'

      appendHistory(this.cwd, {
        type: 'task_completed',
        timestamp: task.completedAt,
        missionId: this.mission!.id,
        taskId,
        data: {
          attempts: task.attempts,
          taskDescription: task.description, // BUG-37 FIX: Include task description
        },
      })

      // BUG-36 FIX: Auto-resolve dependencies - unblock waiting tasks
      this.resolveDependenciesAfterCompletion(taskId)

      // Check if objective is complete
      this.checkObjectiveCompletion()
    } else if (validation.status === 'fixable') {
      // Keep task in progress for retry
      task.status = 'in_progress'

      appendHistory(this.cwd, {
        type: 'validation_fixable',
        timestamp: new Date().toISOString(),
        missionId: this.mission!.id,
        taskId,
        data: { issues: validation.issues, suggestions: validation.suggestions },
      })
    } else {
      task.status = 'failed'
      task.error = validation.summary

      appendHistory(this.cwd, {
        type: 'task_failed',
        timestamp: task.completedAt,
        missionId: this.mission!.id,
        taskId,
        data: { reason: validation.summary },
      })
    }

    this.save()
  }

  /**
   * Fail a task
   */
  failTask(taskId: string, reason: string): void {
    const task = this.getTask(taskId)
    if (!task) return

    task.status = 'failed'
    task.error = reason
    task.completedAt = new Date().toISOString()

    this.save()

    appendHistory(this.cwd, {
      type: 'task_failed',
      timestamp: task.completedAt,
      missionId: this.mission!.id,
      taskId,
      data: {
        reason,
        taskDescription: task.description, // BUG-37 FIX: Include task description
      },
    })
  }

  /**
   * BUG-38 FIX: Auto-transition mission status based on task progress
   *
   * State machine:
   * - planning → in_progress (on first task start)
   * - in_progress → completed (when 100% tasks done)
   */
  autoTransitionMissionStatus(): void {
    if (!this.mission) return

    const progress = this.getProgress()
    const currentStatus = this.mission.status

    // planning → in_progress (on first task start)
    if (currentStatus === 'planning' && progress.inProgress > 0) {
      this.mission.status = 'in_progress'
      this.mission.startedAt = new Date().toISOString()
      this.save()

      appendHistory(this.cwd, {
        type: 'mission_status_changed',
        timestamp: this.mission.startedAt,
        missionId: this.mission.id,
        data: { from: 'planning', to: 'in_progress', trigger: 'first_task_started' },
      })

      log.info(`[mission] Status changed: planning → in_progress`)
      return
    }

    // in_progress → completed (when 100% done)
    if (currentStatus === 'in_progress' && progress.percentage === 100) {
      this.mission.status = 'completed'
      this.mission.completedAt = new Date().toISOString()
      this.save()

      appendHistory(this.cwd, {
        type: 'mission_completed',
        timestamp: this.mission.completedAt,
        missionId: this.mission.id,
        data: { from: 'in_progress', to: 'completed', trigger: 'all_tasks_done' },
      })

      log.info(`[mission] Status changed: in_progress → completed`)
    }
  }

  /**
   * Check if current objective is complete
   */
  private checkObjectiveCompletion(): void {
    if (!this.mission) return

    const objective = this.getCurrentObjective()
    if (!objective) return

    const allComplete = objective.tasks.every((t) => t.status === 'completed')

    if (allComplete) {
      objective.status = 'completed'
      objective.completedAt = new Date().toISOString()

      appendHistory(this.cwd, {
        type: 'objective_completed',
        timestamp: objective.completedAt,
        missionId: this.mission.id,
        objectiveId: objective.id,
      })

      // Check if mission is complete via state machine
      this.autoTransitionMissionStatus()
    }
  }

  // ===========================================================================
  // Budget
  // ===========================================================================

  /**
   * Add cost to budget
   */
  addCost(amount: number, category: keyof BudgetTracking['breakdown']): void {
    if (!this.mission) return

    this.mission.budget.spent += amount
    this.mission.budget.breakdown[category] += amount

    // Check budget warnings
    const percentage = this.mission.budget.spent / this.mission.budget.limit

    if (percentage >= 0.9) {
      appendHistory(this.cwd, {
        type: 'budget_exceeded',
        timestamp: new Date().toISOString(),
        missionId: this.mission.id,
        data: { spent: this.mission.budget.spent, limit: this.mission.budget.limit },
      })
    } else if (percentage >= 0.7) {
      appendHistory(this.cwd, {
        type: 'budget_warning',
        timestamp: new Date().toISOString(),
        missionId: this.mission.id,
        data: { spent: this.mission.budget.spent, limit: this.mission.budget.limit },
      })
    }

    this.save()
  }

  /**
   * Get budget status
   */
  getBudgetStatus(): { spent: number; limit: number; percentage: number; remaining: number } {
    if (!this.mission) {
      return { spent: 0, limit: 0, percentage: 0, remaining: 0 }
    }

    const { spent, limit } = this.mission.budget
    const percentage = limit > 0 ? Math.round((spent / limit) * 100) : 0
    const remaining = Math.max(0, limit - spent)

    return { spent, limit, percentage, remaining }
  }

  // ===========================================================================
  // Validation (H-8)
  // ===========================================================================

  /**
   * Validate mission state integrity
   *
   * Checks:
   * - Unique IDs across objectives and tasks
   * - Valid status transitions
   * - Budget consistency (breakdown totals match spent)
   * - Task dependency validity (referenced tasks exist)
   * - Current objective index validity
   * - Timestamp consistency
   *
   * @returns Validation result with errors and warnings
   */
  validate(): StateValidationResult {
    const errors: StateValidationIssue[] = []
    const warnings: StateValidationIssue[] = []

    if (!this.mission) {
      return {
        isValid: false,
        errors: [{ code: 'NO_MISSION', message: 'No mission loaded', path: 'mission' }],
        warnings: [],
        summary: 'No mission loaded',
      }
    }

    // Track all IDs for uniqueness check
    const seenIds = new Set<string>()

    // Check mission-level fields
    if (!this.mission.id) {
      errors.push({ code: 'MISSING_ID', message: 'Mission ID is missing', path: 'mission.id' })
    } else {
      seenIds.add(this.mission.id)
    }

    // Validate currentObjective index
    if (this.mission.currentObjective < 0) {
      errors.push({
        code: 'INVALID_INDEX',
        message: `currentObjective cannot be negative: ${this.mission.currentObjective}`,
        path: 'mission.currentObjective',
      })
    } else if (
      this.mission.objectives.length > 0 &&
      this.mission.currentObjective >= this.mission.objectives.length
    ) {
      errors.push({
        code: 'INVALID_INDEX',
        message: `currentObjective (${this.mission.currentObjective}) exceeds objectives array length (${this.mission.objectives.length})`,
        path: 'mission.currentObjective',
      })
    }

    // Validate budget consistency
    const budgetErrors = this.validateBudget()
    errors.push(...budgetErrors.errors)
    warnings.push(...budgetErrors.warnings)

    // Validate timestamps
    const timestampErrors = this.validateTimestamps()
    errors.push(...timestampErrors.errors)
    warnings.push(...timestampErrors.warnings)

    // Collect all task IDs for dependency validation
    const allTaskIds = new Set<string>()
    for (const objective of this.mission.objectives) {
      for (const task of objective.tasks) {
        allTaskIds.add(task.id)
      }
    }

    // Validate objectives and tasks
    for (let objIndex = 0; objIndex < this.mission.objectives.length; objIndex++) {
      const objective = this.mission.objectives[objIndex]
      const objPath = `mission.objectives[${objIndex}]`

      // Check objective ID uniqueness
      if (!objective.id) {
        errors.push({
          code: 'MISSING_ID',
          message: 'Objective ID is missing',
          path: `${objPath}.id`,
        })
      } else if (seenIds.has(objective.id)) {
        errors.push({
          code: 'DUPLICATE_ID',
          message: `Duplicate ID: ${objective.id}`,
          path: `${objPath}.id`,
        })
      } else {
        seenIds.add(objective.id)
      }

      // Check objective has tasks
      if (objective.tasks.length === 0 && objective.status !== 'completed') {
        warnings.push({
          code: 'EMPTY_OBJECTIVE',
          message: `Objective "${objective.id}" has no tasks`,
          path: `${objPath}.tasks`,
        })
      }

      // Validate tasks within objective
      for (let taskIndex = 0; taskIndex < objective.tasks.length; taskIndex++) {
        const task = objective.tasks[taskIndex]
        const taskPath = `${objPath}.tasks[${taskIndex}]`

        // Check task ID uniqueness
        if (!task.id) {
          errors.push({ code: 'MISSING_ID', message: 'Task ID is missing', path: `${taskPath}.id` })
        } else if (seenIds.has(task.id)) {
          errors.push({
            code: 'DUPLICATE_ID',
            message: `Duplicate ID: ${task.id}`,
            path: `${taskPath}.id`,
          })
        } else {
          seenIds.add(task.id)
        }

        // Check task has acceptance criteria
        if (!task.acceptanceCriteria || task.acceptanceCriteria.length === 0) {
          warnings.push({
            code: 'MISSING_CRITERIA',
            message: `Task "${task.id}" has no acceptance criteria`,
            path: `${taskPath}.acceptanceCriteria`,
          })
        }

        // Validate task dependencies
        if (task.dependencies && task.dependencies.length > 0) {
          for (const depId of task.dependencies) {
            if (!allTaskIds.has(depId)) {
              errors.push({
                code: 'INVALID_DEPENDENCY',
                message: `Task "${task.id}" references non-existent dependency: ${depId}`,
                path: `${taskPath}.dependencies`,
              })
            }

            // Check for self-dependency
            if (depId === task.id) {
              errors.push({
                code: 'SELF_DEPENDENCY',
                message: `Task "${task.id}" has self-dependency`,
                path: `${taskPath}.dependencies`,
              })
            }
          }
        }

        // Check for circular dependencies
        const circularDep = this.detectCircularDependency(task.id, allTaskIds)
        if (circularDep) {
          errors.push({
            code: 'CIRCULAR_DEPENDENCY',
            message: `Circular dependency detected: ${circularDep.join(' -> ')}`,
            path: `${taskPath}.dependencies`,
          })
        }

        // Validate status consistency
        if (task.status === 'completed' && !task.completedAt) {
          warnings.push({
            code: 'MISSING_TIMESTAMP',
            message: `Task "${task.id}" is completed but has no completedAt timestamp`,
            path: `${taskPath}.completedAt`,
          })
        }

        if (task.status === 'in_progress' && !task.startedAt) {
          warnings.push({
            code: 'MISSING_TIMESTAMP',
            message: `Task "${task.id}" is in_progress but has no startedAt timestamp`,
            path: `${taskPath}.startedAt`,
          })
        }

        // Check attempts consistency
        if (task.attempts < 0) {
          errors.push({
            code: 'INVALID_VALUE',
            message: `Task "${task.id}" has negative attempts: ${task.attempts}`,
            path: `${taskPath}.attempts`,
          })
        }
      }
    }

    const isValid = errors.length === 0
    const summary = this.generateValidationSummary(errors, warnings)

    return { isValid, errors, warnings, summary }
  }

  /**
   * Validate budget consistency
   */
  private validateBudget(): { errors: StateValidationIssue[]; warnings: StateValidationIssue[] } {
    const errors: StateValidationIssue[] = []
    const warnings: StateValidationIssue[] = []

    if (!this.mission) return { errors, warnings }

    const { budget } = this.mission
    const { spent, limit, breakdown } = budget

    // Check breakdown totals match spent
    const breakdownTotal =
      breakdown.council + breakdown.operators + breakdown.validators + breakdown.support

    // Allow small floating point differences
    if (Math.abs(breakdownTotal - spent) > 0.001) {
      errors.push({
        code: 'BUDGET_MISMATCH',
        message: `Budget breakdown total ($${breakdownTotal.toFixed(4)}) does not match spent ($${spent.toFixed(4)})`,
        path: 'mission.budget',
      })
    }

    // Check for negative values
    if (spent < 0) {
      errors.push({
        code: 'INVALID_VALUE',
        message: `Budget spent cannot be negative: $${spent}`,
        path: 'mission.budget.spent',
      })
    }

    if (limit < 0) {
      errors.push({
        code: 'INVALID_VALUE',
        message: `Budget limit cannot be negative: $${limit}`,
        path: 'mission.budget.limit',
      })
    }

    // Check individual breakdown values
    for (const [category, value] of Object.entries(breakdown)) {
      if (value < 0) {
        errors.push({
          code: 'INVALID_VALUE',
          message: `Budget breakdown.${category} cannot be negative: $${value}`,
          path: `mission.budget.breakdown.${category}`,
        })
      }
    }

    // Warning if over budget
    if (spent > limit) {
      warnings.push({
        code: 'OVER_BUDGET',
        message: `Mission is over budget: spent $${spent.toFixed(2)} of $${limit.toFixed(2)} limit`,
        path: 'mission.budget',
      })
    }

    return { errors, warnings }
  }

  /**
   * Validate timestamp consistency
   */
  private validateTimestamps(): {
    errors: StateValidationIssue[]
    warnings: StateValidationIssue[]
  } {
    const errors: StateValidationIssue[] = []
    const warnings: StateValidationIssue[] = []

    if (!this.mission) return { errors, warnings }

    // Check required timestamps exist
    if (!this.mission.createdAt) {
      errors.push({
        code: 'MISSING_TIMESTAMP',
        message: 'Mission createdAt timestamp is missing',
        path: 'mission.createdAt',
      })
    }

    // Validate timestamp order
    const created = this.mission.createdAt ? new Date(this.mission.createdAt).getTime() : 0
    const updated = this.mission.updatedAt ? new Date(this.mission.updatedAt).getTime() : 0
    const approved = this.mission.approvedAt ? new Date(this.mission.approvedAt).getTime() : 0
    const completed = this.mission.completedAt ? new Date(this.mission.completedAt).getTime() : 0

    if (created > 0 && updated > 0 && updated < created) {
      warnings.push({
        code: 'TIMESTAMP_ORDER',
        message: 'updatedAt is before createdAt',
        path: 'mission.updatedAt',
      })
    }

    if (created > 0 && approved > 0 && approved < created) {
      warnings.push({
        code: 'TIMESTAMP_ORDER',
        message: 'approvedAt is before createdAt',
        path: 'mission.approvedAt',
      })
    }

    if (approved > 0 && completed > 0 && completed < approved) {
      warnings.push({
        code: 'TIMESTAMP_ORDER',
        message: 'completedAt is before approvedAt',
        path: 'mission.completedAt',
      })
    }

    return { errors, warnings }
  }

  /**
   * Detect circular dependencies in task graph
   */
  private detectCircularDependency(
    startTaskId: string,
    allTaskIds: Set<string>,
    visited: Set<string> = new Set(),
    path: string[] = []
  ): string[] | null {
    if (visited.has(startTaskId)) {
      // Found cycle - return the path from the repeated node
      const cycleStart = path.indexOf(startTaskId)
      return [...path.slice(cycleStart), startTaskId]
    }

    const task = this.getTask(startTaskId)
    if (!task || !task.dependencies || task.dependencies.length === 0) {
      return null
    }

    visited.add(startTaskId)
    path.push(startTaskId)

    for (const depId of task.dependencies) {
      if (!allTaskIds.has(depId)) continue // Skip invalid deps (handled elsewhere)

      const cycle = this.detectCircularDependency(depId, allTaskIds, new Set(visited), [...path])
      if (cycle) return cycle
    }

    return null
  }

  /**
   * Generate human-readable validation summary
   */
  private generateValidationSummary(
    errors: StateValidationIssue[],
    warnings: StateValidationIssue[]
  ): string {
    if (errors.length === 0 && warnings.length === 0) {
      return 'Mission state is valid'
    }

    const parts: string[] = []

    if (errors.length > 0) {
      parts.push(`${errors.length} error(s)`)
    }

    if (warnings.length > 0) {
      parts.push(`${warnings.length} warning(s)`)
    }

    return `Validation found ${parts.join(' and ')}`
  }
}

// =============================================================================
// Validation Types
// =============================================================================

/**
 * Issue codes for state validation
 */
export type StateValidationCode =
  | 'NO_MISSION'
  | 'MISSING_ID'
  | 'DUPLICATE_ID'
  | 'INVALID_INDEX'
  | 'INVALID_VALUE'
  | 'INVALID_DEPENDENCY'
  | 'SELF_DEPENDENCY'
  | 'CIRCULAR_DEPENDENCY'
  | 'MISSING_CRITERIA'
  | 'EMPTY_OBJECTIVE'
  | 'BUDGET_MISMATCH'
  | 'OVER_BUDGET'
  | 'MISSING_TIMESTAMP'
  | 'TIMESTAMP_ORDER'

/**
 * Individual validation issue
 */
export interface StateValidationIssue {
  /** Issue code for programmatic handling */
  code: StateValidationCode
  /** Human-readable message */
  message: string
  /** JSON path to the problematic field */
  path: string
}

/**
 * Result of state validation
 */
export interface StateValidationResult {
  /** Whether the mission state is valid (no errors) */
  isValid: boolean
  /** Critical errors that must be fixed */
  errors: StateValidationIssue[]
  /** Non-critical warnings */
  warnings: StateValidationIssue[]
  /** Human-readable summary */
  summary: string
}
