/**
 * A2A Task Manager
 *
 * Manages A2A task lifecycle: create, execute, cancel.
 * Bridges between A2A protocol and AVA's AgentExecutor.
 */

import { randomUUID } from 'node:crypto'
import type {
  A2AEvent,
  A2AMessage,
  A2ATask,
  A2ATaskState,
  Artifact,
  Part,
  TaskArtifactUpdateEvent,
  TaskStatus,
  TaskStatusUpdateEvent,
} from './types.js'

// ============================================================================
// Types
// ============================================================================

export type TaskEventListener = (event: A2AEvent) => void

export interface TaskExecutor {
  /** Execute a task with the given goal and return events */
  execute(goal: string, cwd: string, signal: AbortSignal, onEvent: TaskEventListener): Promise<void>
}

// ============================================================================
// Task Manager
// ============================================================================

/**
 * Manages A2A tasks: creation, lookup, execution, cancellation.
 */
export class TaskManager {
  private tasks = new Map<string, A2ATask>()
  private abortControllers = new Map<string, AbortController>()
  private executor: TaskExecutor | null
  private workingDirectory: string

  constructor(executor?: TaskExecutor, workingDirectory = process.cwd()) {
    this.executor = executor ?? null
    this.workingDirectory = workingDirectory
  }

  // ==========================================================================
  // Task Lifecycle
  // ==========================================================================

  /**
   * Create a new task from an incoming message.
   */
  createTask(message: A2AMessage, contextId?: string): A2ATask {
    const id = randomUUID()
    const ctx = contextId ?? randomUUID()
    const now = new Date().toISOString()

    const status: TaskStatus = {
      state: 'submitted',
      timestamp: now,
    }

    const task: A2ATask = {
      id,
      contextId: ctx,
      status,
      messages: [message],
      artifacts: [],
      history: [status],
    }

    this.tasks.set(id, task)
    return task
  }

  /**
   * Get a task by ID.
   */
  getTask(taskId: string): A2ATask | undefined {
    return this.tasks.get(taskId)
  }

  /**
   * Get all tasks.
   */
  getAllTasks(): A2ATask[] {
    return Array.from(this.tasks.values())
  }

  /**
   * Get or create a task for a message.
   * If taskId is provided, appends to existing task.
   * Otherwise creates a new task.
   */
  getOrCreateTask(message: A2AMessage, taskId?: string, contextId?: string): A2ATask {
    if (taskId) {
      const existing = this.tasks.get(taskId)
      if (existing) {
        existing.messages.push(message)
        return existing
      }
    }

    return this.createTask(message, contextId)
  }

  // ==========================================================================
  // Task Execution
  // ==========================================================================

  /**
   * Execute a task asynchronously.
   * Returns an async iterable of A2A events for SSE streaming.
   */
  async *executeTask(taskId: string): AsyncGenerator<A2AEvent> {
    const task = this.tasks.get(taskId)
    if (!task) {
      throw new Error(`Task not found: ${taskId}`)
    }

    if (!this.executor) {
      throw new Error('No task executor configured')
    }

    // Create abort controller for cancellation
    const abortController = new AbortController()
    this.abortControllers.set(taskId, abortController)

    // Transition to working
    this.setState(task, 'working')
    yield createStatusEvent(task, false)

    // Collect events from executor
    const eventQueue: A2AEvent[] = []
    let resolveWaiting: (() => void) | null = null
    let executionDone = false

    const onEvent: TaskEventListener = (event) => {
      eventQueue.push(event)
      resolveWaiting?.()
    }

    // Extract goal from user messages
    const goal = extractGoal(task.messages)

    // Start execution in background
    const executionPromise = this.executor
      .execute(goal, this.workingDirectory, abortController.signal, onEvent)
      .then(() => {
        executionDone = true
        resolveWaiting?.()
      })
      .catch((error: Error) => {
        executionDone = true
        if (error.name !== 'AbortError') {
          this.setState(task, 'failed', {
            role: 'agent',
            parts: [{ type: 'text', text: `Error: ${error.message}` }],
          })
          eventQueue.push(createStatusEvent(task, true))
        }
        resolveWaiting?.()
      })

    // Yield events as they come in
    while (!executionDone || eventQueue.length > 0) {
      if (eventQueue.length > 0) {
        yield eventQueue.shift()!
      } else {
        // Wait for next event
        await new Promise<void>((resolve) => {
          resolveWaiting = resolve
        })
        resolveWaiting = null
      }
    }

    await executionPromise

    // Clean up
    this.abortControllers.delete(taskId)

    // If still working, mark as completed
    if (task.status.state === 'working') {
      this.setState(task, 'completed')
      yield createStatusEvent(task, true)
    }
  }

  // ==========================================================================
  // Task Cancellation
  // ==========================================================================

  /**
   * Cancel a running task.
   */
  cancelTask(taskId: string): A2ATask | undefined {
    const task = this.tasks.get(taskId)
    if (!task) return undefined

    const controller = this.abortControllers.get(taskId)
    if (controller) {
      controller.abort()
      this.abortControllers.delete(taskId)
    }

    if (task.status.state === 'working' || task.status.state === 'submitted') {
      this.setState(task, 'canceled')
    }

    return task
  }

  // ==========================================================================
  // State Management
  // ==========================================================================

  /**
   * Transition task to a new state.
   */
  setState(task: A2ATask, state: A2ATaskState, message?: A2AMessage): void {
    const status: TaskStatus = {
      state,
      message,
      timestamp: new Date().toISOString(),
    }

    task.status = status
    task.history.push(status)

    if (message) {
      task.messages.push(message)
    }
  }

  /**
   * Add an artifact to a task.
   */
  addArtifact(taskId: string, artifact: Artifact): A2ATask | undefined {
    const task = this.tasks.get(taskId)
    if (!task) return undefined

    task.artifacts.push(artifact)
    return task
  }

  // ==========================================================================
  // Lifecycle
  // ==========================================================================

  /**
   * Set the task executor.
   */
  setExecutor(executor: TaskExecutor): void {
    this.executor = executor
  }

  /**
   * Set working directory.
   */
  setWorkingDirectory(cwd: string): void {
    this.workingDirectory = cwd
  }

  /**
   * Check if a task is currently executing.
   */
  isExecuting(taskId: string): boolean {
    return this.abortControllers.has(taskId)
  }

  /**
   * Get the count of active tasks.
   */
  getActiveCount(): number {
    return this.abortControllers.size
  }

  /**
   * Remove a completed/failed/canceled task from memory.
   */
  removeTask(taskId: string): boolean {
    this.abortControllers.get(taskId)?.abort()
    this.abortControllers.delete(taskId)
    return this.tasks.delete(taskId)
  }

  /**
   * Clear all tasks and abort running ones.
   */
  reset(): void {
    for (const controller of this.abortControllers.values()) {
      controller.abort()
    }
    this.abortControllers.clear()
    this.tasks.clear()
  }
}

// ============================================================================
// Helpers
// ============================================================================

/**
 * Extract goal text from user messages.
 */
function extractGoal(messages: A2AMessage[]): string {
  const userMessages = messages.filter((m) => m.role === 'user')
  const parts: string[] = []

  for (const msg of userMessages) {
    for (const part of msg.parts) {
      if (part.type === 'text') {
        parts.push(part.text)
      }
    }
  }

  return parts.join('\n') || 'No goal specified'
}

/**
 * Create a status update event for SSE streaming.
 */
export function createStatusEvent(task: A2ATask, final: boolean): TaskStatusUpdateEvent {
  return {
    kind: 'status-update',
    taskId: task.id,
    contextId: task.contextId,
    final,
    status: task.status,
  }
}

/**
 * Create an artifact update event for SSE streaming.
 */
export function createArtifactEvent(
  task: A2ATask,
  artifact: Artifact,
  append: boolean,
  lastChunk: boolean
): TaskArtifactUpdateEvent {
  return {
    kind: 'artifact-update',
    taskId: task.id,
    contextId: task.contextId,
    artifact,
    append,
    lastChunk,
  }
}

// ============================================================================
// Part Utilities
// ============================================================================

/**
 * Create a text part.
 */
export function textPart(text: string): Part {
  return { type: 'text', text }
}

/**
 * Create a data part.
 */
export function dataPart(data: Record<string, unknown>): Part {
  return { type: 'data', data }
}

/**
 * Create a user message with text.
 */
export function userMessage(text: string): A2AMessage {
  return { role: 'user', parts: [textPart(text)] }
}

/**
 * Create an agent message with text.
 */
export function agentMessage(text: string): A2AMessage {
  return { role: 'agent', parts: [textPart(text)] }
}
