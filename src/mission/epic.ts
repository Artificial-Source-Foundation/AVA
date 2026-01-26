/**
 * Delta9 Epic System
 *
 * Epics are high-level work items that span multiple objectives.
 * They provide:
 * - Cross-objective task grouping
 * - Git branch management
 * - Progress tracking across objectives
 * - Acceptance criteria at the epic level
 *
 * @example
 * ```typescript
 * import { getEpicManager } from './mission/epic'
 *
 * const manager = getEpicManager({ baseDir: '.delta9' })
 *
 * // Create an epic
 * const epic = manager.create({
 *   title: 'User Authentication System',
 *   description: 'Complete auth with OAuth, MFA, and session management',
 *   priority: 'high',
 *   acceptanceCriteria: ['All auth tests pass', 'Security review completed'],
 * })
 *
 * // Link tasks
 * manager.linkTasks(epic.id, ['task-1', 'task-2', 'task-3'])
 *
 * // Check status
 * const status = manager.getStatus(epic.id)
 * ```
 */

import { z } from 'zod'
import { nanoid } from 'nanoid'
import { existsSync, mkdirSync, readFileSync, writeFileSync } from 'node:fs'
import { join, dirname } from 'node:path'

// =============================================================================
// Types
// =============================================================================

export const EpicPrioritySchema = z.enum(['low', 'normal', 'high', 'critical'])
export type EpicPriority = z.infer<typeof EpicPrioritySchema>

export const EpicStatusSchema = z.enum([
  'planning',
  'in_progress',
  'completed',
  'blocked',
  'archived',
])
export type EpicStatus = z.infer<typeof EpicStatusSchema>

export const EpicSchema = z.object({
  /** Unique epic ID */
  id: z.string(),
  /** Epic title */
  title: z.string(),
  /** Detailed description */
  description: z.string(),
  /** Current status */
  status: EpicStatusSchema,
  /** Priority level */
  priority: EpicPrioritySchema,
  /** Linked objective IDs */
  objectives: z.array(z.string()),
  /** Linked task IDs (across all objectives) */
  tasks: z.array(z.string()),
  /** Acceptance criteria for the epic */
  acceptanceCriteria: z.array(z.string()),
  /** Git branch name if created */
  gitBranch: z.string().optional(),
  /** Related mission ID */
  missionId: z.string().optional(),
  /** Labels/tags for categorization */
  labels: z.array(z.string()).optional(),
  /** When epic was created */
  createdAt: z.string(),
  /** When epic was last updated */
  updatedAt: z.string(),
  /** When epic was completed */
  completedAt: z.string().optional(),
})

export type Epic = z.infer<typeof EpicSchema>

export interface EpicProgress {
  /** Total tasks in epic */
  totalTasks: number
  /** Completed tasks */
  completedTasks: number
  /** Failed tasks */
  failedTasks: number
  /** In-progress tasks */
  inProgressTasks: number
  /** Blocked tasks */
  blockedTasks: number
  /** Pending tasks */
  pendingTasks: number
  /** Progress percentage (0-100) */
  percentage: number
}

export interface EpicBreakdown {
  /** Epic details */
  epic: Epic
  /** Progress info */
  progress: EpicProgress
  /** Breakdown by objective */
  byObjective: {
    objectiveId: string
    taskCount: number
    completedCount: number
  }[]
}

export interface EpicManagerConfig {
  /** Base directory for storage */
  baseDir?: string
  /** Storage filename */
  storageFile?: string
}

// =============================================================================
// Default Configuration
// =============================================================================

const DEFAULT_EPIC_CONFIG: Required<EpicManagerConfig> = {
  baseDir: '.delta9',
  storageFile: 'epics.json',
}

// =============================================================================
// Epic Manager
// =============================================================================

export class EpicManager {
  private config: Required<EpicManagerConfig>
  private epics: Map<string, Epic> = new Map()
  private storagePath: string
  private taskStatuses: Map<
    string,
    'pending' | 'in_progress' | 'completed' | 'failed' | 'blocked'
  > = new Map()
  private taskObjectives: Map<string, string> = new Map()

  constructor(config?: EpicManagerConfig) {
    this.config = { ...DEFAULT_EPIC_CONFIG, ...config }
    this.storagePath = join(this.config.baseDir, this.config.storageFile)
    this.load()
  }

  // ===========================================================================
  // Epic CRUD
  // ===========================================================================

  /**
   * Create a new epic
   */
  create(input: {
    title: string
    description: string
    priority?: EpicPriority
    acceptanceCriteria?: string[]
    missionId?: string
    labels?: string[]
  }): Epic {
    const now = new Date().toISOString()
    const epic: Epic = {
      id: `epic-${nanoid(8)}`,
      title: input.title,
      description: input.description,
      status: 'planning',
      priority: input.priority ?? 'normal',
      objectives: [],
      tasks: [],
      acceptanceCriteria: input.acceptanceCriteria ?? [],
      missionId: input.missionId,
      labels: input.labels,
      createdAt: now,
      updatedAt: now,
    }

    this.epics.set(epic.id, epic)
    this.save()
    return epic
  }

  /**
   * Get an epic by ID
   */
  get(epicId: string): Epic | undefined {
    return this.epics.get(epicId)
  }

  /**
   * List all epics
   */
  list(filters?: { status?: EpicStatus; priority?: EpicPriority; missionId?: string }): Epic[] {
    let epics = Array.from(this.epics.values())

    if (filters?.status) {
      epics = epics.filter((e) => e.status === filters.status)
    }
    if (filters?.priority) {
      epics = epics.filter((e) => e.priority === filters.priority)
    }
    if (filters?.missionId) {
      epics = epics.filter((e) => e.missionId === filters.missionId)
    }

    return epics
  }

  /**
   * Update an epic
   */
  update(epicId: string, updates: Partial<Omit<Epic, 'id' | 'createdAt'>>): Epic | undefined {
    const epic = this.epics.get(epicId)
    if (!epic) return undefined

    const updated: Epic = {
      ...epic,
      ...updates,
      updatedAt: new Date().toISOString(),
    }

    this.epics.set(epicId, updated)
    this.save()
    return updated
  }

  /**
   * Delete an epic
   */
  delete(epicId: string): boolean {
    const deleted = this.epics.delete(epicId)
    if (deleted) {
      this.save()
    }
    return deleted
  }

  // ===========================================================================
  // Task Linking
  // ===========================================================================

  /**
   * Link tasks to an epic
   */
  linkTasks(epicId: string, taskIds: string[]): Epic | undefined {
    const epic = this.epics.get(epicId)
    if (!epic) return undefined

    const newTasks = new Set([...epic.tasks, ...taskIds])
    return this.update(epicId, { tasks: Array.from(newTasks) })
  }

  /**
   * Unlink tasks from an epic
   */
  unlinkTasks(epicId: string, taskIds: string[]): Epic | undefined {
    const epic = this.epics.get(epicId)
    if (!epic) return undefined

    const taskSet = new Set(taskIds)
    return this.update(epicId, {
      tasks: epic.tasks.filter((t) => !taskSet.has(t)),
    })
  }

  /**
   * Link objectives to an epic
   */
  linkObjectives(epicId: string, objectiveIds: string[]): Epic | undefined {
    const epic = this.epics.get(epicId)
    if (!epic) return undefined

    const newObjectives = new Set([...epic.objectives, ...objectiveIds])
    return this.update(epicId, { objectives: Array.from(newObjectives) })
  }

  // ===========================================================================
  // Status Management
  // ===========================================================================

  /**
   * Update task status (called by mission state when tasks change)
   */
  updateTaskStatus(
    taskId: string,
    status: 'pending' | 'in_progress' | 'completed' | 'failed' | 'blocked',
    objectiveId?: string
  ): void {
    this.taskStatuses.set(taskId, status)
    if (objectiveId) {
      this.taskObjectives.set(taskId, objectiveId)
    }
    // Auto-update epic status based on task statuses
    this.autoUpdateEpicStatuses()
  }

  /**
   * Get epic status with progress
   */
  getStatus(epicId: string): EpicProgress | undefined {
    const epic = this.epics.get(epicId)
    if (!epic) return undefined

    let completed = 0
    let failed = 0
    let inProgress = 0
    let blocked = 0
    let pending = 0

    for (const taskId of epic.tasks) {
      const status = this.taskStatuses.get(taskId) ?? 'pending'
      switch (status) {
        case 'completed':
          completed++
          break
        case 'failed':
          failed++
          break
        case 'in_progress':
          inProgress++
          break
        case 'blocked':
          blocked++
          break
        default:
          pending++
      }
    }

    const total = epic.tasks.length
    const percentage = total > 0 ? Math.round((completed / total) * 100) : 0

    return {
      totalTasks: total,
      completedTasks: completed,
      failedTasks: failed,
      inProgressTasks: inProgress,
      blockedTasks: blocked,
      pendingTasks: pending,
      percentage,
    }
  }

  /**
   * Get detailed breakdown by objective
   */
  getBreakdown(epicId: string): EpicBreakdown | undefined {
    const epic = this.epics.get(epicId)
    if (!epic) return undefined

    const progress = this.getStatus(epicId)
    if (!progress) return undefined

    // Group tasks by objective
    const byObjective = new Map<string, { total: number; completed: number }>()

    for (const taskId of epic.tasks) {
      const objectiveId = this.taskObjectives.get(taskId) ?? 'unknown'
      const current = byObjective.get(objectiveId) ?? { total: 0, completed: 0 }
      current.total++
      if (this.taskStatuses.get(taskId) === 'completed') {
        current.completed++
      }
      byObjective.set(objectiveId, current)
    }

    return {
      epic,
      progress,
      byObjective: Array.from(byObjective.entries()).map(([objectiveId, data]) => ({
        objectiveId,
        taskCount: data.total,
        completedCount: data.completed,
      })),
    }
  }

  /**
   * Set epic status
   */
  setStatus(epicId: string, status: EpicStatus): Epic | undefined {
    const updates: Partial<Epic> = { status }
    if (status === 'completed') {
      updates.completedAt = new Date().toISOString()
    }
    return this.update(epicId, updates)
  }

  // ===========================================================================
  // Git Branch Management
  // ===========================================================================

  /**
   * Set git branch for epic
   */
  setGitBranch(epicId: string, branchName: string): Epic | undefined {
    return this.update(epicId, { gitBranch: branchName })
  }

  /**
   * Generate suggested branch name
   */
  suggestBranchName(epic: Epic): string {
    const slug = epic.title
      .toLowerCase()
      .replace(/[^a-z0-9]+/g, '-')
      .replace(/(^-|-$)/g, '')
      .substring(0, 50)
    return `epic/${slug}`
  }

  // ===========================================================================
  // Private Methods
  // ===========================================================================

  /**
   * Auto-update epic statuses based on task progress
   */
  private autoUpdateEpicStatuses(): void {
    for (const [epicId, epic] of this.epics) {
      if (epic.status === 'archived' || epic.status === 'completed') continue

      const progress = this.getStatus(epicId)
      if (!progress) continue

      let newStatus: EpicStatus = epic.status

      if (progress.totalTasks === 0) {
        newStatus = 'planning'
      } else if (progress.completedTasks === progress.totalTasks) {
        newStatus = 'completed'
      } else if (progress.blockedTasks > 0 && progress.inProgressTasks === 0) {
        newStatus = 'blocked'
      } else if (progress.inProgressTasks > 0 || progress.completedTasks > 0) {
        newStatus = 'in_progress'
      }

      if (newStatus !== epic.status) {
        this.update(epicId, { status: newStatus })
      }
    }
  }

  // ===========================================================================
  // Persistence
  // ===========================================================================

  private ensureDirectory(): void {
    const dir = dirname(this.storagePath)
    if (!existsSync(dir)) {
      mkdirSync(dir, { recursive: true })
    }
  }

  private load(): void {
    if (!existsSync(this.storagePath)) {
      return
    }

    try {
      const content = readFileSync(this.storagePath, 'utf-8')
      const data = JSON.parse(content)

      if (data.epics && Array.isArray(data.epics)) {
        for (const epicData of data.epics) {
          try {
            const epic = EpicSchema.parse(epicData)
            this.epics.set(epic.id, epic)
          } catch {
            // Skip invalid epics
          }
        }
      }

      if (data.taskStatuses) {
        for (const [taskId, status] of Object.entries(data.taskStatuses)) {
          this.taskStatuses.set(
            taskId,
            status as 'pending' | 'in_progress' | 'completed' | 'failed' | 'blocked'
          )
        }
      }

      if (data.taskObjectives) {
        for (const [taskId, objectiveId] of Object.entries(data.taskObjectives)) {
          this.taskObjectives.set(taskId, objectiveId as string)
        }
      }
    } catch {
      // File read error, start fresh
      this.epics = new Map()
    }
  }

  private save(): void {
    this.ensureDirectory()
    try {
      const data = {
        epics: Array.from(this.epics.values()),
        taskStatuses: Object.fromEntries(this.taskStatuses),
        taskObjectives: Object.fromEntries(this.taskObjectives),
      }
      writeFileSync(this.storagePath, JSON.stringify(data, null, 2))
    } catch {
      // Ignore save errors
    }
  }

  /**
   * Clear all data (for testing)
   */
  clear(): void {
    this.epics.clear()
    this.taskStatuses.clear()
    this.taskObjectives.clear()
    this.save()
  }

  /**
   * Destroy the manager
   */
  destroy(): void {
    this.epics.clear()
    this.taskStatuses.clear()
    this.taskObjectives.clear()
  }
}

// =============================================================================
// Singleton Instance
// =============================================================================

let globalManager: EpicManager | null = null

/**
 * Get the global epic manager instance
 */
export function getEpicManager(config?: EpicManagerConfig): EpicManager {
  if (!globalManager) {
    globalManager = new EpicManager(config)
  }
  return globalManager
}

/**
 * Reset the global manager (for testing)
 */
export function resetEpicManager(): void {
  if (globalManager) {
    globalManager.destroy()
    globalManager = null
  }
}
