/**
 * Delta9 Background Manager
 *
 * Manages background task execution with:
 * - Concurrency control (default: 2 concurrent tasks)
 * - Task queue with priority
 * - Session creation and tracking
 * - Stability detection via polling
 * - OpenCode SDK integration for real agent execution
 */

import { nanoid } from 'nanoid'
import type { MissionState } from '../mission/state.js'
import { appendHistory } from '../mission/history.js'
import { getConfig } from './config.js'
import { taskNotifications } from './notifications.js'

// =============================================================================
// SDK Types (from OpenCode plugin system)
// =============================================================================

/**
 * OpenCode client type - passed from plugin context
 * We use a minimal interface to avoid tight coupling
 * The actual SDK types are more complex, but we only need these methods
 */
export interface OpenCodeClient {
  /** Session management (for background tasks) */
  session: {
    create: (opts: {
      body: { parentID?: string; title?: string }
      query?: { directory?: string }
    }) => Promise<{ data?: { id: string }; error?: unknown }>
    prompt: (opts: {
      path: { id: string }
      body: {
        agent?: string
        model?: { providerID: string; modelID: string }
        system?: string
        tools?: Record<string, boolean>
        parts: Array<{ type: string; text: string }>
        noReply?: boolean
      }
    }) => Promise<unknown> // Returns complex response, but we fire-and-forget
    messages: (opts: { path: { id: string } }) => Promise<{
      data?: Array<{
        info?: { role?: string; time?: { created?: number } }
        parts?: Array<{ type?: string; text?: string }>
      }>
      error?: unknown
    }>
    status: () => Promise<{
      data?: Record<string, { type: string }>
    }>
    get: (opts: { path: { id: string } }) => Promise<{
      data?: { directory?: string }
    }>
    abort: (opts: { path: { id: string } }) => Promise<unknown>
  }
  /** Application utilities (for logging) */
  app?: {
    log: (message: string) => void
  }
}

// Stability detection constants (from oh-my-opencode patterns)
const MIN_STABILITY_TIME_MS = 10_000 // Must run at least 10s before stability detection
const STABILITY_POLLS_REQUIRED = 3 // Need 3 consecutive polls with same message count
const TASK_TTL_MS = 30 * 60 * 1000 // 30 minutes max task lifetime
const STALE_TIMEOUT_MS = 3 * 60 * 1000 // 3 minutes of no activity = stale

type ProcessCleanupEvent = NodeJS.Signals | 'beforeExit' | 'exit'

// =============================================================================
// Types
// =============================================================================

export type BackgroundTaskStatus = 'pending' | 'running' | 'completed' | 'failed' | 'cancelled'

export interface BackgroundTask {
  /** Unique task ID */
  id: string
  /** Task prompt */
  prompt: string
  /** Agent type to use */
  agent: string
  /** Current status */
  status: BackgroundTaskStatus
  /** Related mission task ID (if any) */
  missionTaskId?: string
  /** Session ID for this task */
  sessionId?: string
  /** When task was queued */
  queuedAt: string
  /** When task started running */
  startedAt?: string
  /** When task completed/failed */
  completedAt?: string
  /** Task output */
  output?: string
  /** Error message if failed */
  error?: string
  /** Priority (higher = more important) */
  priority: number
  /** Progress tracking for stale detection */
  progress?: {
    lastUpdate: number // timestamp of last activity
    messageCount: number
  }
}

export interface LaunchInput {
  /** Task prompt */
  prompt: string
  /** Agent type */
  agent: string
  /** Mission context */
  missionContext?: {
    id: string
    description: string
    status: string
  }
  /** Related mission task ID */
  missionTaskId?: string
  /** Priority (default: 0) */
  priority?: number
}

export interface ExecuteSyncInput {
  /** Task prompt */
  prompt: string
  /** Agent type */
  agent: string
  /** Optional model override */
  model?: string
  /** Related mission task ID */
  missionTaskId?: string
}

export interface BackgroundManagerConfig {
  /** Maximum concurrent tasks */
  maxConcurrent: number
  /** Polling interval for stability detection (ms) */
  pollInterval: number
  /** Max time to wait for task completion (ms) */
  maxWaitTime: number
}

// =============================================================================
// Concurrency Manager (with settled flag pattern from oh-my-opencode)
// =============================================================================

interface QueueEntry {
  resolve: () => void
  reject: (error: Error) => void
  settled: boolean
}

class ConcurrencyManager {
  private running = 0
  private readonly max: number
  private readonly waitQueue: QueueEntry[] = []

  constructor(maxConcurrent: number) {
    this.max = maxConcurrent
  }

  async acquire(): Promise<void> {
    if (this.running < this.max) {
      this.running++
      return
    }

    await new Promise<void>((resolve, reject) => {
      this.waitQueue.push({ resolve, reject, settled: false })
    })
    this.running++
  }

  release(): void {
    this.running--
    const next = this.waitQueue.shift()
    if (next && !next.settled) {
      next.settled = true
      next.resolve()
    }
  }

  /**
   * Cancel all waiting entries (for shutdown)
   */
  cancelAll(): void {
    for (const entry of this.waitQueue) {
      if (!entry.settled) {
        entry.settled = true
        entry.reject(new Error('Manager shutdown'))
      }
    }
    this.waitQueue.length = 0
  }

  get activeCount(): number {
    return this.running
  }

  get pendingCount(): number {
    return this.waitQueue.length
  }
}

// =============================================================================
// Background Manager
// =============================================================================

export class BackgroundManager {
  // Static cleanup tracking (shared across instances)
  private static cleanupManagers = new Set<BackgroundManager>()
  private static cleanupRegistered = false
  private static cleanupHandlers = new Map<ProcessCleanupEvent, () => void>()

  private tasks = new Map<string, BackgroundTask>()
  private concurrency: ConcurrencyManager
  private readonly config: BackgroundManagerConfig
  private readonly missionState: MissionState
  private readonly cwd: string
  private readonly client: OpenCodeClient | null
  private shutdownTriggered = false
  private pollingInterval?: ReturnType<typeof setInterval>

  constructor(
    missionState: MissionState,
    cwd: string,
    config?: Partial<BackgroundManagerConfig>,
    client?: OpenCodeClient
  ) {
    this.missionState = missionState
    this.cwd = cwd
    this.client = client ?? null
    this.config = {
      maxConcurrent: config?.maxConcurrent ?? 2,
      pollInterval: config?.pollInterval ?? 500, // 500ms for faster stability detection
      maxWaitTime: config?.maxWaitTime ?? 600000, // 10 minutes (increased for real execution)
    }
    this.concurrency = new ConcurrencyManager(this.config.maxConcurrent)

    // Register for process cleanup
    this.registerProcessCleanup()
  }

  // ===========================================================================
  // Process Cleanup (CRITICAL for data integrity)
  // ===========================================================================

  /**
   * Register handlers for process signals to ensure graceful shutdown
   */
  private registerProcessCleanup(): void {
    BackgroundManager.cleanupManagers.add(this)

    if (BackgroundManager.cleanupRegistered) return
    BackgroundManager.cleanupRegistered = true

    const cleanupAll = () => {
      for (const manager of BackgroundManager.cleanupManagers) {
        try {
          manager.shutdown()
        } catch (error) {
          console.error('[delta9] [background] Error during shutdown cleanup:', error)
        }
      }
    }

    const registerSignal = (signal: ProcessCleanupEvent, exitAfter: boolean): void => {
      const listener = () => {
        cleanupAll()
        if (exitAfter) {
          process.exit(0)
        }
      }
      process.on(signal, listener)
      BackgroundManager.cleanupHandlers.set(signal, listener)
    }

    // Register signal handlers
    registerSignal('SIGINT', true)
    registerSignal('SIGTERM', true)
    if (process.platform === 'win32') {
      registerSignal('SIGBREAK' as NodeJS.Signals, true)
    }
    registerSignal('beforeExit', false)
    registerSignal('exit', false)

    console.log('[delta9] [background] Process cleanup handlers registered')
  }

  /**
   * Unregister process cleanup handlers
   */
  private unregisterProcessCleanup(): void {
    BackgroundManager.cleanupManagers.delete(this)

    if (BackgroundManager.cleanupManagers.size > 0) return

    for (const [signal, listener] of BackgroundManager.cleanupHandlers.entries()) {
      process.off(signal, listener)
    }
    BackgroundManager.cleanupHandlers.clear()
    BackgroundManager.cleanupRegistered = false
  }

  /**
   * Graceful shutdown - release resources and cancel pending work
   */
  shutdown(): void {
    if (this.shutdownTriggered) return
    this.shutdownTriggered = true

    console.log('[delta9] [background] Shutting down BackgroundManager')

    // Stop polling immediately
    if (this.pollingInterval) {
      clearInterval(this.pollingInterval)
      this.pollingInterval = undefined
    }

    // Abort running sessions
    for (const task of this.tasks.values()) {
      if (task.status === 'running' && task.sessionId && this.client) {
        this.client.session.abort({ path: { id: task.sessionId } }).catch(() => {
          // Swallow errors - session may already be gone
        })
      }
    }

    // Cancel all concurrency waiters
    this.concurrency.cancelAll()

    // Clear state
    this.tasks.clear()

    // Unregister handlers
    this.unregisterProcessCleanup()

    console.log('[delta9] [background] Shutdown complete')
  }

  // ===========================================================================
  // Stale Detection & TTL Pruning
  // ===========================================================================

  /**
   * Prune stale and expired tasks
   * Called periodically during polling
   */
  private pruneStaleAndExpiredTasks(): void {
    const now = Date.now()

    for (const [taskId, task] of this.tasks) {
      // Check task age (TTL)
      const taskStart = task.startedAt
        ? new Date(task.startedAt).getTime()
        : new Date(task.queuedAt).getTime()
      const taskAge = now - taskStart

      if (taskAge > TASK_TTL_MS) {
        console.log(
          `[delta9] [background] Pruning expired task ${taskId} (age: ${Math.round(taskAge / 1000)}s)`
        )

        if (task.status === 'running' && task.sessionId && this.client) {
          // Abort the session
          this.client.session.abort({ path: { id: task.sessionId } }).catch(() => {})
        }

        task.status = 'failed'
        task.error = 'Task exceeded TTL (30 minutes)'
        task.completedAt = new Date().toISOString()
        continue
      }

      // Check for stale activity (only for running tasks)
      if (task.status === 'running' && task.progress) {
        const lastActivity = task.progress.lastUpdate
        const staleTime = now - lastActivity

        if (staleTime > STALE_TIMEOUT_MS) {
          console.log(
            `[delta9] [background] Task ${taskId} stale (no activity for ${Math.round(staleTime / 1000)}s)`
          )

          if (task.sessionId && this.client) {
            // Abort the session
            this.client.session.abort({ path: { id: task.sessionId } }).catch(() => {})
          }

          task.status = 'failed'
          task.error = `Task stale (no activity for ${Math.round(staleTime / 60000)} minutes)`
          task.completedAt = new Date().toISOString()
        }
      }
    }
  }

  /**
   * Start periodic polling for all running tasks
   */
  private startPolling(): void {
    if (this.pollingInterval) return

    this.pollingInterval = setInterval(() => {
      // Prune stale tasks first
      this.pruneStaleAndExpiredTasks()

      // Check if any running tasks remain
      const hasRunning = Array.from(this.tasks.values()).some((t) => t.status === 'running')
      if (!hasRunning) {
        this.stopPolling()
      }
    }, 5000) // Check every 5 seconds

    // Don't keep the process alive just for polling
    this.pollingInterval.unref()
  }

  /**
   * Stop periodic polling
   */
  private stopPolling(): void {
    if (this.pollingInterval) {
      clearInterval(this.pollingInterval)
      this.pollingInterval = undefined
    }
  }

  // ===========================================================================
  // Task Management
  // ===========================================================================

  /**
   * Launch a background task
   *
   * Returns immediately with task ID. Use getOutput to check progress.
   */
  async launch(input: LaunchInput): Promise<string> {
    const taskId = `bg_${nanoid(8)}`
    const now = new Date().toISOString()

    const task: BackgroundTask = {
      id: taskId,
      prompt: input.prompt,
      agent: input.agent,
      status: 'pending',
      missionTaskId: input.missionTaskId,
      queuedAt: now,
      priority: input.priority ?? 0,
    }

    this.tasks.set(taskId, task)

    // Log to history
    const mission = this.missionState.getMission()
    if (mission) {
      appendHistory(this.cwd, {
        type: 'background_task_started',
        timestamp: now,
        missionId: mission.id,
        taskId: input.missionTaskId,
        data: { backgroundTaskId: taskId, agent: input.agent },
      })
    }

    // Queue for execution
    this.processQueue()

    return taskId
  }

  /**
   * Execute a task synchronously
   *
   * Waits for completion before returning.
   */
  async executeSync(input: ExecuteSyncInput): Promise<string> {
    const taskId = await this.launch({
      prompt: input.prompt,
      agent: input.agent,
      missionTaskId: input.missionTaskId,
      priority: 10, // High priority for sync tasks
    })

    // Wait for completion
    const result = await this.waitForCompletion(taskId)
    return result
  }

  /**
   * Get task by ID
   */
  getTask(taskId: string): BackgroundTask | undefined {
    return this.tasks.get(taskId)
  }

  /**
   * Get output from a task
   *
   * Returns null if task doesn't exist or isn't complete.
   */
  async getOutput(taskId: string): Promise<string | null> {
    const task = this.tasks.get(taskId)
    if (!task) return null

    if (task.status === 'running') {
      // Poll for stability
      await this.pollForStability(task)
    }

    if (task.status === 'completed') {
      return task.output ?? ''
    }

    if (task.status === 'failed') {
      throw new Error(task.error ?? 'Task failed')
    }

    return null
  }

  /**
   * Cancel a task
   */
  cancel(taskId: string): boolean {
    const task = this.tasks.get(taskId)
    if (!task) return false

    if (task.status === 'pending' || task.status === 'running') {
      // Abort the session if running
      if (task.status === 'running' && task.sessionId && this.client) {
        this.client.session.abort({ path: { id: task.sessionId } }).catch(() => {
          // Swallow errors - session may already be gone
        })
      }

      task.status = 'cancelled'
      task.completedAt = new Date().toISOString()

      // Notify task cancelled
      taskNotifications.cancelled(task.id, task.prompt.substring(0, 50))

      return true
    }

    return false
  }

  /**
   * List all tasks
   */
  listTasks(filter?: { status?: BackgroundTaskStatus }): BackgroundTask[] {
    let tasks = Array.from(this.tasks.values())

    if (filter?.status) {
      tasks = tasks.filter((t) => t.status === filter.status)
    }

    return tasks.sort((a, b) => b.priority - a.priority)
  }

  /**
   * Get active task count
   */
  getActiveCount(): number {
    return this.concurrency.activeCount
  }

  /**
   * Get pending task count
   */
  getPendingCount(): number {
    return this.concurrency.pendingCount
  }

  /**
   * Clean up completed/failed tasks older than maxAge (ms)
   */
  cleanup(maxAge: number = 3600000): number {
    const now = Date.now()
    let cleaned = 0

    for (const [id, task] of this.tasks) {
      if (
        (task.status === 'completed' || task.status === 'failed' || task.status === 'cancelled') &&
        task.completedAt
      ) {
        const age = now - new Date(task.completedAt).getTime()
        if (age > maxAge) {
          this.tasks.delete(id)
          cleaned++
        }
      }
    }

    return cleaned
  }

  // ===========================================================================
  // Internal Methods
  // ===========================================================================

  /**
   * Process the task queue
   */
  private async processQueue(): Promise<void> {
    // Get pending tasks sorted by priority
    const pending = this.listTasks({ status: 'pending' })

    for (const task of pending) {
      // Try to acquire a slot
      if (this.concurrency.activeCount >= this.config.maxConcurrent) {
        break
      }

      // Execute the task
      this.executeTask(task)
    }
  }

  /**
   * Execute a single task
   */
  private async executeTask(task: BackgroundTask): Promise<void> {
    await this.concurrency.acquire()

    try {
      task.status = 'running'
      task.startedAt = new Date().toISOString()

      // Notify task started
      taskNotifications.started(task.id, task.agent, task.prompt.substring(0, 50))

      // Use SDK execution if client is available, otherwise fall back to simulation
      if (this.client) {
        console.log(`[delta9] [background] Executing task ${task.id} with SDK`)
        await this.executeWithSDK(task)
      } else {
        console.log(
          `[delta9] [background] Executing task ${task.id} with simulation (no SDK client)`
        )
        await this.simulateExecution(task)
      }
    } catch (error) {
      task.status = 'failed'
      task.error = error instanceof Error ? error.message : String(error)
      task.completedAt = new Date().toISOString()

      console.error(`[delta9] [background] Task ${task.id} failed:`, task.error)

      // Notify task failed
      taskNotifications.failed(task.id, task.agent, task.prompt.substring(0, 50), task.error)

      // Log failure
      const mission = this.missionState.getMission()
      if (mission) {
        appendHistory(this.cwd, {
          type: 'background_task_failed',
          timestamp: task.completedAt,
          missionId: mission.id,
          taskId: task.missionTaskId,
          data: { backgroundTaskId: task.id, error: task.error },
        })
      }
    } finally {
      this.concurrency.release()
    }
  }

  /**
   * Execute task using OpenCode SDK
   *
   * 1. Create a sub-session via client.session.create()
   * 2. Invoke the agent via client.session.prompt() (fire-and-forget)
   * 3. Poll for completion using stability detection
   * 4. Return the result
   */
  private async executeWithSDK(task: BackgroundTask): Promise<void> {
    if (!this.client) {
      throw new Error('SDK client not available')
    }

    // Create a sub-session
    const createResult = await this.client.session.create({
      body: {
        title: `Delta9 Task: ${task.id}`,
      },
      query: {
        directory: this.cwd,
      },
    })

    if (createResult.error || !createResult.data?.id) {
      throw new Error(`Failed to create session: ${createResult.error ?? 'No session ID returned'}`)
    }

    const sessionId = createResult.data.id
    task.sessionId = sessionId

    console.log(`[delta9] [background] Task ${task.id} session created: ${sessionId}`)

    // Initialize progress tracking
    task.progress = {
      lastUpdate: Date.now(),
      messageCount: 0,
    }

    // Start global polling for stale detection
    this.startPolling()

    // Fire-and-forget prompt to agent
    // We don't await completion - we poll for stability instead
    this.client.session
      .prompt({
        path: { id: sessionId },
        body: {
          agent: task.agent,
          tools: {
            // Prevent recursive delegation
            delegate_task: false,
          },
          parts: [{ type: 'text', text: task.prompt }],
        },
      })
      .catch((error) => {
        console.error(`[delta9] [background] Prompt error for task ${task.id}:`, error)
        task.status = 'failed'
        task.error = error instanceof Error ? error.message : String(error)
        task.completedAt = new Date().toISOString()
      })

    // Poll for stability (oh-my-opencode pattern)
    await this.pollForSDKStability(task, sessionId)
  }

  /**
   * Poll for task completion using stability detection
   *
   * Pattern from oh-my-opencode:
   * - Minimum 10 seconds before accepting completion
   * - 3 consecutive polls with same message count = stable
   * - Also check session status for idle state
   */
  private async pollForSDKStability(task: BackgroundTask, sessionId: string): Promise<void> {
    if (!this.client) return

    const startTime = Date.now()
    let lastMsgCount = 0
    let stablePolls = 0

    while (Date.now() - startTime < this.config.maxWaitTime) {
      // Check if task was cancelled
      if (task.status === 'cancelled') {
        return
      }

      await new Promise((resolve) => setTimeout(resolve, this.config.pollInterval))

      try {
        // Check session status
        const statusResult = await this.client.session.status()
        const sessionStatus = statusResult.data?.[sessionId]

        // If session is actively running, reset stability
        if (sessionStatus && sessionStatus.type !== 'idle') {
          stablePolls = 0
          lastMsgCount = 0
          continue
        }

        // Minimum time check before accepting completion
        const elapsed = Date.now() - startTime
        if (elapsed < MIN_STABILITY_TIME_MS) {
          continue
        }

        // Get messages to check stability
        const messagesResult = await this.client.session.messages({
          path: { id: sessionId },
        })

        if (messagesResult.error) {
          console.error(
            `[delta9] [background] Messages error for task ${task.id}:`,
            messagesResult.error
          )
          continue
        }

        const messages = messagesResult.data ?? []
        const currentMsgCount = messages.length

        // Update progress tracking for stale detection
        if (task.progress && currentMsgCount !== task.progress.messageCount) {
          task.progress.lastUpdate = Date.now()
          task.progress.messageCount = currentMsgCount
        }

        if (currentMsgCount === lastMsgCount) {
          stablePolls++
          if (stablePolls >= STABILITY_POLLS_REQUIRED) {
            // Task is complete - extract result
            await this.extractTaskResult(task, sessionId, messages)
            return
          }
        } else {
          stablePolls = 0
          lastMsgCount = currentMsgCount
        }
      } catch (error) {
        console.error(`[delta9] [background] Poll error for task ${task.id}:`, error)
        // Continue polling despite errors
      }
    }

    // Timeout - mark as failed
    task.status = 'failed'
    task.error = 'Task timed out'
    task.completedAt = new Date().toISOString()

    // Log failure
    const mission = this.missionState.getMission()
    if (mission) {
      appendHistory(this.cwd, {
        type: 'background_task_failed',
        timestamp: task.completedAt,
        missionId: mission.id,
        taskId: task.missionTaskId,
        data: { backgroundTaskId: task.id, error: task.error },
      })
    }
  }

  /**
   * Extract result from completed session messages
   */
  private async extractTaskResult(
    task: BackgroundTask,
    sessionId: string,
    messages: Array<{
      info?: { role?: string; time?: { created?: number } }
      parts?: Array<{ type?: string; text?: string }>
    }>
  ): Promise<void> {
    // Find the last assistant message
    const assistantMessages = messages
      .filter((m) => m.info?.role === 'assistant')
      .sort((a, b) => (b.info?.time?.created ?? 0) - (a.info?.time?.created ?? 0))

    const lastMessage = assistantMessages[0]

    if (!lastMessage) {
      task.output = JSON.stringify({
        success: true,
        sessionId,
        message: 'Task completed but no assistant response found',
      })
    } else {
      // Extract text from text and reasoning parts
      const textParts =
        lastMessage.parts?.filter((p) => p.type === 'text' || p.type === 'reasoning') ?? []
      const textContent = textParts
        .map((p) => p.text ?? '')
        .filter(Boolean)
        .join('\n')

      task.output = JSON.stringify({
        success: true,
        sessionId,
        agent: task.agent,
        result: textContent || '(No text output)',
      })
    }

    task.status = 'completed'
    task.completedAt = new Date().toISOString()

    console.log(`[delta9] [background] Task ${task.id} completed via SDK`)

    // Notify task completed
    taskNotifications.completed(task.id, task.agent, task.prompt.substring(0, 50))

    // Log completion
    const mission = this.missionState.getMission()
    if (mission) {
      appendHistory(this.cwd, {
        type: 'background_task_completed',
        timestamp: task.completedAt,
        missionId: mission.id,
        taskId: task.missionTaskId,
        data: { backgroundTaskId: task.id, sessionId },
      })
    }
  }

  /**
   * Fallback simulation for when SDK client is not available
   */
  private async simulateExecution(task: BackgroundTask): Promise<void> {
    // Placeholder: In real implementation, this would invoke the agent
    // For now, just mark as completed with a placeholder output

    task.output = JSON.stringify({
      success: true,
      agent: task.agent,
      prompt: task.prompt.substring(0, 100) + (task.prompt.length > 100 ? '...' : ''),
      message: `Task ${task.id} executed by ${task.agent} agent`,
      note: 'SDK client not available. Real execution requires OpenCode SDK integration.',
    })

    task.status = 'completed'
    task.completedAt = new Date().toISOString()

    // Notify task completed
    taskNotifications.completed(task.id, task.agent, task.prompt.substring(0, 50))

    // Log completion
    const mission = this.missionState.getMission()
    if (mission) {
      appendHistory(this.cwd, {
        type: 'background_task_completed',
        timestamp: task.completedAt,
        missionId: mission.id,
        taskId: task.missionTaskId,
        data: { backgroundTaskId: task.id },
      })
    }
  }

  /**
   * Wait for a task to complete
   */
  private async waitForCompletion(taskId: string): Promise<string> {
    const startTime = Date.now()

    while (Date.now() - startTime < this.config.maxWaitTime) {
      const task = this.tasks.get(taskId)
      if (!task) {
        throw new Error(`Task ${taskId} not found`)
      }

      if (task.status === 'completed') {
        return task.output ?? ''
      }

      if (task.status === 'failed') {
        throw new Error(task.error ?? 'Task failed')
      }

      if (task.status === 'cancelled') {
        throw new Error('Task was cancelled')
      }

      // Wait before polling again
      await new Promise((resolve) => setTimeout(resolve, this.config.pollInterval))
    }

    throw new Error(`Task ${taskId} timed out`)
  }

  /**
   * Poll for task stability (wait for running task to complete)
   */
  private async pollForStability(task: BackgroundTask): Promise<void> {
    const startTime = Date.now()

    while (Date.now() - startTime < this.config.maxWaitTime) {
      if (task.status !== 'running') {
        return
      }

      await new Promise((resolve) => setTimeout(resolve, this.config.pollInterval))
    }
  }
}

// =============================================================================
// Factory
// =============================================================================

let managerInstance: BackgroundManager | null = null

/**
 * Get or create the background manager instance
 */
export function getBackgroundManager(
  missionState: MissionState,
  cwd: string,
  client?: OpenCodeClient
): BackgroundManager {
  if (!managerInstance) {
    const config = getConfig()
    managerInstance = new BackgroundManager(
      missionState,
      cwd,
      {
        maxConcurrent: config.operators.maxParallel,
      },
      client
    )
    console.log(`[delta9] [background] Manager created with${client ? '' : 'out'} SDK client`)
  }
  return managerInstance
}

/**
 * Clear the manager instance (for testing or plugin unload)
 * Calls shutdown to properly release resources
 */
export function clearBackgroundManager(): void {
  if (managerInstance) {
    managerInstance.shutdown()
    managerInstance = null
  }
}
