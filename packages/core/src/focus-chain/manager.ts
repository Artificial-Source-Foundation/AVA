/**
 * Focus Chain Manager
 *
 * Manages task progress tracking via markdown checklist
 */

import { getPlatform } from '../platform.js'
import {
  addTaskToMarkdown,
  calculateProgress,
  getNextTask,
  parseMarkdown,
  removeTaskFromMarkdown,
  serializeToMarkdown,
  updateTaskInMarkdown,
} from './parser.js'
import {
  DEFAULT_FOCUS_CHAIN_OPTIONS,
  type FocusChain,
  type FocusChainEvent,
  type FocusChainEventListener,
  type FocusChainOptions,
  type FocusTask,
  type TaskStatus,
} from './types.js'

// ============================================================================
// Manager Class
// ============================================================================

/**
 * Focus Chain Manager
 *
 * Provides task progress tracking through a markdown file.
 * Supports:
 * - Load/save from .ava/tasks.md
 * - Add, update, remove tasks
 * - Progress calculation
 * - Event notifications
 */
export class FocusChainManager {
  private chain: FocusChain | null = null
  private options: Required<FocusChainOptions>
  private listeners: Set<FocusChainEventListener> = new Set()
  private watchCleanup: (() => void) | null = null
  private saveDebounceTimer: ReturnType<typeof setTimeout> | null = null

  constructor(options: FocusChainOptions = {}) {
    this.options = { ...DEFAULT_FOCUS_CHAIN_OPTIONS, ...options }
  }

  // ==========================================================================
  // Lifecycle
  // ==========================================================================

  /**
   * Initialize the focus chain
   * Creates or loads the tasks.md file
   */
  async init(workspaceRoot: string): Promise<FocusChain> {
    const platform = getPlatform()
    const filePath = `${workspaceRoot}/${this.options.directory}/${this.options.fileName}`

    // Ensure directory exists
    const dirPath = `${workspaceRoot}/${this.options.directory}`
    const dirExists = await platform.fs.exists(dirPath)
    if (!dirExists) {
      await platform.fs.mkdir(dirPath)
    }

    // Try to load existing file
    let content: string
    const fileExists = await platform.fs.exists(filePath)

    if (fileExists) {
      content = await platform.fs.readFile(filePath)
    } else {
      // Create default template
      content = this.createDefaultTemplate()
      await platform.fs.writeFile(filePath, content)
    }

    // Parse and create chain
    const tasks = parseMarkdown(content)
    const progress = calculateProgress(tasks)

    this.chain = {
      tasks,
      filePath,
      lastModified: Date.now(),
      metadata: {
        startTime: Date.now(),
        totalTasks: progress.total,
        completedTasks: progress.completed,
        blockedTasks: progress.blocked,
      },
    }

    this.emit({ type: 'chain:loaded', chain: this.chain })

    // Set up file watching if enabled
    if (this.options.watchFile) {
      this.setupFileWatch(filePath)
    }

    return this.chain
  }

  /**
   * Cleanup resources
   */
  async dispose(): Promise<void> {
    if (this.watchCleanup) {
      this.watchCleanup()
      this.watchCleanup = null
    }
    if (this.saveDebounceTimer) {
      clearTimeout(this.saveDebounceTimer)
      this.saveDebounceTimer = null
    }
    this.listeners.clear()
  }

  // ==========================================================================
  // Task Operations
  // ==========================================================================

  /**
   * Add a new task
   */
  async addTask(
    text: string,
    options: { level?: number; afterTaskId?: string; status?: TaskStatus } = {}
  ): Promise<FocusTask> {
    if (!this.chain) {
      throw new Error('Focus chain not initialized')
    }

    const { level = 0, afterTaskId, status = 'pending' } = options

    // Find line to insert after
    let afterLine: number | undefined
    if (afterTaskId) {
      const afterTask = this.chain.tasks.find((t) => t.id === afterTaskId)
      if (afterTask) {
        afterLine = afterTask.line
      }
    }

    // Update markdown content
    const platform = getPlatform()
    const currentContent = await platform.fs.readFile(this.chain.filePath)
    const newContent = addTaskToMarkdown(currentContent, text, { level, afterLine, status })
    await platform.fs.writeFile(this.chain.filePath, newContent)

    // Re-parse to get updated tasks
    const tasks = parseMarkdown(newContent)
    const newTask = tasks.find((t) => t.text === text && t.status === status)

    if (!newTask) {
      throw new Error('Failed to add task')
    }

    this.chain.tasks = tasks
    this.updateMetadata()

    this.emit({ type: 'task:added', task: newTask })

    return newTask
  }

  /**
   * Update a task's status
   */
  async updateTaskStatus(taskId: string, status: TaskStatus): Promise<FocusTask> {
    if (!this.chain) {
      throw new Error('Focus chain not initialized')
    }

    const task = this.chain.tasks.find((t) => t.id === taskId)
    if (!task) {
      throw new Error(`Task not found: ${taskId}`)
    }

    const previousStatus = task.status

    // Update markdown content
    const platform = getPlatform()
    const currentContent = await platform.fs.readFile(this.chain.filePath)
    const newContent = updateTaskInMarkdown(currentContent, taskId, this.chain.tasks, status)
    await platform.fs.writeFile(this.chain.filePath, newContent)

    // Update in-memory state
    task.status = status
    this.updateMetadata()

    this.emit({ type: 'task:updated', task, previousStatus })

    return task
  }

  /**
   * Remove a task
   */
  async removeTask(taskId: string): Promise<void> {
    if (!this.chain) {
      throw new Error('Focus chain not initialized')
    }

    const task = this.chain.tasks.find((t) => t.id === taskId)
    if (!task) {
      throw new Error(`Task not found: ${taskId}`)
    }

    // Update markdown content
    const platform = getPlatform()
    const currentContent = await platform.fs.readFile(this.chain.filePath)
    const newContent = removeTaskFromMarkdown(currentContent, taskId, this.chain.tasks)
    await platform.fs.writeFile(this.chain.filePath, newContent)

    // Re-parse to get updated tasks
    const tasks = parseMarkdown(newContent)
    this.chain.tasks = tasks
    this.updateMetadata()

    this.emit({ type: 'task:removed', taskId })
  }

  /**
   * Mark a task as completed
   */
  async completeTask(taskId: string): Promise<FocusTask> {
    return this.updateTaskStatus(taskId, 'completed')
  }

  /**
   * Mark a task as in progress
   */
  async startTask(taskId: string): Promise<FocusTask> {
    // Clear any other in_progress tasks
    if (this.chain) {
      const inProgressTasks = this.chain.tasks.filter((t) => t.status === 'in_progress')
      for (const task of inProgressTasks) {
        await this.updateTaskStatus(task.id, 'pending')
      }
    }

    const task = await this.updateTaskStatus(taskId, 'in_progress')
    if (this.chain) {
      this.chain.activeTaskId = taskId
    }
    return task
  }

  /**
   * Block a task
   */
  async blockTask(taskId: string, reason?: string): Promise<FocusTask> {
    const task = await this.updateTaskStatus(taskId, 'blocked')
    if (reason) {
      task.notes = reason
    }
    return task
  }

  // ==========================================================================
  // Query Methods
  // ==========================================================================

  /**
   * Get current focus chain
   */
  getChain(): FocusChain | null {
    return this.chain
  }

  /**
   * Get all tasks
   */
  getTasks(): FocusTask[] {
    return this.chain?.tasks || []
  }

  /**
   * Get a specific task
   */
  getTask(taskId: string): FocusTask | undefined {
    return this.chain?.tasks.find((t) => t.id === taskId)
  }

  /**
   * Get the currently active task
   */
  getActiveTask(): FocusTask | undefined {
    if (!this.chain?.activeTaskId) return undefined
    return this.chain.tasks.find((t) => t.id === this.chain!.activeTaskId)
  }

  /**
   * Get the next actionable task
   */
  getNextTask(): FocusTask | null {
    if (!this.chain) return null
    return getNextTask(this.chain.tasks)
  }

  /**
   * Get progress statistics
   */
  getProgress(): ReturnType<typeof calculateProgress> {
    if (!this.chain) {
      return { total: 0, completed: 0, inProgress: 0, blocked: 0, pending: 0, percentComplete: 0 }
    }
    return calculateProgress(this.chain.tasks)
  }

  // ==========================================================================
  // Event Handling
  // ==========================================================================

  /**
   * Subscribe to focus chain events
   */
  on(listener: FocusChainEventListener): () => void {
    this.listeners.add(listener)
    return () => this.listeners.delete(listener)
  }

  /**
   * Emit an event to all listeners
   */
  private emit(event: FocusChainEvent): void {
    for (const listener of this.listeners) {
      try {
        listener(event)
      } catch (error) {
        console.error('Focus chain event listener error:', error)
      }
    }
  }

  // ==========================================================================
  // Private Methods
  // ==========================================================================

  /**
   * Create default template content
   */
  private createDefaultTemplate(): string {
    return serializeToMarkdown([], 'Task Progress')
  }

  /**
   * Update metadata after task changes
   */
  private updateMetadata(): void {
    if (!this.chain) return

    const progress = calculateProgress(this.chain.tasks)
    this.chain.metadata = {
      ...this.chain.metadata,
      totalTasks: progress.total,
      completedTasks: progress.completed,
      blockedTasks: progress.blocked,
    }
    this.chain.lastModified = Date.now()
  }

  /**
   * Set up file watching for external changes
   */
  private setupFileWatch(_filePath: string): void {
    // File watching would be platform-specific
    // For now, we'll implement manual refresh
    // In Tauri, we could use tauri-plugin-fs-watch
    // In Node, we could use chokidar
    // TODO: Implement platform-specific file watching
    // this.watchCleanup = platform.fs.watch(filePath, this.handleExternalChange)
  }

  // Reserved for future file watching implementation
  // private async handleExternalChange(): Promise<void> {
  //   if (!this.chain) return
  //   const platform = getPlatform()
  //   const content = await platform.fs.readFile(this.chain.filePath)
  //   const tasks = parseMarkdown(content)
  //   this.chain.tasks = tasks
  //   this.updateMetadata()
  //   this.emit({ type: 'chain:external_change', chain: this.chain })
  // }
}

// ============================================================================
// Singleton Instance
// ============================================================================

let instance: FocusChainManager | null = null

/**
 * Get the focus chain manager singleton
 */
export function getFocusChainManager(): FocusChainManager {
  if (!instance) {
    instance = new FocusChainManager()
  }
  return instance
}

/**
 * Create a new focus chain manager with custom options
 */
export function createFocusChainManager(options?: FocusChainOptions): FocusChainManager {
  return new FocusChainManager(options)
}
