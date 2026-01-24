/**
 * Delta9 Mission State Manager
 *
 * Manages the mission.json state file with CRUD operations.
 * This is the source of truth for mission state.
 */

import { readFileSync, writeFileSync } from 'node:fs'
import { nanoid } from 'nanoid'
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
import {
  getMissionPath,
  getMissionMdPath,
  ensureDelta9Dir,
  missionExists,
} from '../lib/paths.js'
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
      $schema: 'https://delta9.dev/mission.schema.json',
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
      console.error('Failed to load mission:', error)
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
    writeFileSync(
      getMissionPath(this.cwd),
      JSON.stringify(this.mission, null, 2),
      'utf-8'
    )

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
    return this.mission?.objectives.find(o => o.id === id) ?? null
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
      const task = objective.tasks.find(t => t.id === taskId)
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
  private areTaskDependenciesMet(task: Task): boolean {
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

    appendHistory(this.cwd, {
      type: 'task_started',
      timestamp: task.startedAt,
      missionId: this.mission!.id,
      taskId,
      data: { assignee, attempt: task.attempts },
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
        data: { attempts: task.attempts },
      })

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
      data: { reason },
    })
  }

  /**
   * Check if current objective is complete
   */
  private checkObjectiveCompletion(): void {
    if (!this.mission) return

    const objective = this.getCurrentObjective()
    if (!objective) return

    const allComplete = objective.tasks.every(t => t.status === 'completed')

    if (allComplete) {
      objective.status = 'completed'
      objective.completedAt = new Date().toISOString()

      appendHistory(this.cwd, {
        type: 'objective_completed',
        timestamp: objective.completedAt,
        missionId: this.mission.id,
        objectiveId: objective.id,
      })

      // Check if mission is complete
      this.checkMissionCompletion()
    }
  }

  /**
   * Check if mission is complete
   */
  private checkMissionCompletion(): void {
    if (!this.mission) return

    const allComplete = this.mission.objectives.every(o => o.status === 'completed')

    if (allComplete) {
      this.mission.status = 'completed'
      this.mission.completedAt = new Date().toISOString()

      appendHistory(this.cwd, {
        type: 'mission_completed',
        timestamp: this.mission.completedAt,
        missionId: this.mission.id,
      })
    }
  }

  // ===========================================================================
  // Budget
  // ===========================================================================

  /**
   * Add cost to budget
   */
  addCost(
    amount: number,
    category: keyof BudgetTracking['breakdown']
  ): void {
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
}
