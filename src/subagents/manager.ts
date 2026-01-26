/**
 * Delta9 Subagent Manager
 *
 * Thin layer over BackgroundManager that adds:
 * - Human-readable aliases for subagents
 * - State tracking
 * - Output piping back to parent sessions
 */

import type { MissionState } from '../mission/state.js'
import { getBackgroundManager, type OpenCodeClient } from '../lib/background-manager.js'
import type {
  Subagent,
  SubagentState,
  SpawnSubagentInput,
  SubagentOutput,
  SubagentQuery,
  SubagentStats,
} from './types.js'

// =============================================================================
// Constants
// =============================================================================

const POLL_INTERVAL_MS = 1000 // Check subagent status every second
const MAX_POLL_DURATION_MS = 30 * 60 * 1000 // 30 minutes max

// =============================================================================
// Subagent Manager
// =============================================================================

export class SubagentManager {
  private subagents = new Map<string, Subagent>()
  private aliasToId = new Map<string, string>()
  private pollingInterval?: ReturnType<typeof setInterval>
  private readonly missionState: MissionState
  private readonly cwd: string
  private readonly client: OpenCodeClient | undefined

  constructor(missionState: MissionState, cwd: string, client?: OpenCodeClient) {
    this.missionState = missionState
    this.cwd = cwd
    this.client = client
  }

  // ===========================================================================
  // Core Operations
  // ===========================================================================

  /**
   * Spawn a new subagent with a human-readable alias
   */
  async spawn(input: SpawnSubagentInput): Promise<Subagent> {
    // Validate alias uniqueness
    if (this.aliasToId.has(input.alias)) {
      throw new Error(`Subagent alias already in use: ${input.alias}`)
    }

    const manager = getBackgroundManager(this.missionState, this.cwd, this.client)

    // Build prompt with context if provided
    let prompt = input.prompt
    if (input.context) {
      prompt = `Context:\n${input.context}\n\nTask:\n${input.prompt}`
    }

    // Launch via BackgroundManager
    const taskId = await manager.launch({
      prompt,
      agent: input.agentType || 'operator',
      priority: 0, // Normal priority for subagents
    })

    // Get session ID if available
    const task = manager.getTask(taskId)
    const sessionId = task?.sessionId

    const subagent: Subagent = {
      alias: input.alias,
      taskId,
      sessionId,
      agentType: input.agentType || 'operator',
      prompt: input.prompt,
      state: 'spawning',
      parentSessionId: input.parentSessionId,
      spawnedAt: new Date().toISOString(),
      outputDelivered: false,
    }

    this.subagents.set(taskId, subagent)
    this.aliasToId.set(input.alias, taskId)

    // Emit event
    this.emitEvent('subagent.spawned', {
      alias: subagent.alias,
      taskId: subagent.taskId,
      agentType: subagent.agentType,
    })

    // Start polling for completion
    this.startPolling()

    return subagent
  }

  /**
   * Get subagent by alias
   */
  getByAlias(alias: string): Subagent | null {
    const taskId = this.aliasToId.get(alias)
    if (!taskId) return null
    return this.subagents.get(taskId) || null
  }

  /**
   * Get subagent by task ID
   */
  getByTaskId(taskId: string): Subagent | null {
    return this.subagents.get(taskId) || null
  }

  /**
   * List subagents with optional filters
   */
  list(query?: SubagentQuery): Subagent[] {
    let results = Array.from(this.subagents.values())

    if (query?.state) {
      results = results.filter((s) => s.state === query.state)
    }

    if (query?.parentSessionId) {
      results = results.filter((s) => s.parentSessionId === query.parentSessionId)
    }

    if (query?.pendingDelivery) {
      results = results.filter((s) => s.state === 'completed' && !s.outputDelivered && s.output)
    }

    // Sort by spawn time (newest first)
    return results.sort((a, b) => new Date(b.spawnedAt).getTime() - new Date(a.spawnedAt).getTime())
  }

  /**
   * Get outputs pending delivery for a parent session
   */
  getPendingOutputs(parentSessionId: string): SubagentOutput[] {
    return this.list({
      parentSessionId,
      pendingDelivery: true,
    }).map((s) => ({
      alias: s.alias,
      taskId: s.taskId,
      state: s.state,
      output: s.output,
      duration: s.completedAt
        ? new Date(s.completedAt).getTime() - new Date(s.spawnedAt).getTime()
        : undefined,
    }))
  }

  /**
   * Mark outputs as delivered
   */
  markDelivered(aliases: string[]): void {
    for (const alias of aliases) {
      const subagent = this.getByAlias(alias)
      if (subagent) {
        subagent.outputDelivered = true
      }
    }
  }

  /**
   * Get statistics
   */
  getStats(): SubagentStats {
    const byState: Record<SubagentState, number> = {
      spawning: 0,
      active: 0,
      idle: 0,
      completed: 0,
      failed: 0,
    }

    let pendingDelivery = 0

    for (const subagent of this.subagents.values()) {
      byState[subagent.state]++
      if (subagent.state === 'completed' && !subagent.outputDelivered && subagent.output) {
        pendingDelivery++
      }
    }

    return {
      total: this.subagents.size,
      byState,
      pendingDelivery,
    }
  }

  /**
   * Wait for a subagent to complete
   */
  async waitFor(alias: string, timeoutMs?: number): Promise<SubagentOutput> {
    const timeout = timeoutMs || MAX_POLL_DURATION_MS
    const startTime = Date.now()

    while (Date.now() - startTime < timeout) {
      const subagent = this.getByAlias(alias)
      if (!subagent) {
        throw new Error(`Subagent not found: ${alias}`)
      }

      if (subagent.state === 'completed' || subagent.state === 'failed') {
        return {
          alias: subagent.alias,
          taskId: subagent.taskId,
          state: subagent.state,
          output: subagent.output,
          error: subagent.error,
          duration: subagent.completedAt
            ? new Date(subagent.completedAt).getTime() - new Date(subagent.spawnedAt).getTime()
            : undefined,
        }
      }

      await new Promise((resolve) => setTimeout(resolve, POLL_INTERVAL_MS))
    }

    throw new Error(`Subagent timed out: ${alias}`)
  }

  /**
   * Clear completed subagents
   */
  cleanup(): number {
    let cleaned = 0

    for (const [taskId, subagent] of this.subagents) {
      if (
        (subagent.state === 'completed' || subagent.state === 'failed') &&
        subagent.outputDelivered
      ) {
        this.subagents.delete(taskId)
        this.aliasToId.delete(subagent.alias)
        cleaned++
      }
    }

    return cleaned
  }

  // ===========================================================================
  // Internal Methods
  // ===========================================================================

  /**
   * Start polling for subagent completion
   */
  private startPolling(): void {
    if (this.pollingInterval) return

    this.pollingInterval = setInterval(() => {
      this.updateSubagentStates()
    }, POLL_INTERVAL_MS)

    // Don't keep process alive just for polling
    this.pollingInterval.unref()
  }

  /**
   * Stop polling
   */
  private stopPolling(): void {
    if (this.pollingInterval) {
      clearInterval(this.pollingInterval)
      this.pollingInterval = undefined
    }
  }

  /**
   * Update subagent states from background tasks
   */
  private updateSubagentStates(): void {
    const manager = getBackgroundManager(this.missionState, this.cwd, this.client)
    let hasActive = false

    for (const [taskId, subagent] of this.subagents) {
      if (subagent.state === 'completed' || subagent.state === 'failed') {
        continue
      }

      const task = manager.getTask(taskId)
      if (!task) continue

      const previousState = subagent.state

      // Map BackgroundTaskStatus to SubagentState
      switch (task.status) {
        case 'pending':
          subagent.state = 'spawning'
          hasActive = true
          break
        case 'running':
          subagent.state = 'active'
          subagent.sessionId = task.sessionId
          hasActive = true
          break
        case 'completed':
          subagent.state = 'completed'
          subagent.completedAt = task.completedAt || new Date().toISOString()
          subagent.output = task.output
          break
        case 'failed':
          subagent.state = 'failed'
          subagent.completedAt = task.completedAt || new Date().toISOString()
          subagent.error = task.error
          break
        case 'cancelled':
          subagent.state = 'failed'
          subagent.completedAt = task.completedAt || new Date().toISOString()
          subagent.error = 'Cancelled'
          break
      }

      // Emit state change event
      if (subagent.state !== previousState) {
        this.emitEvent('subagent.state_changed', {
          alias: subagent.alias,
          taskId: subagent.taskId,
          previousState,
          newState: subagent.state,
        })

        // Emit completion event
        if (subagent.state === 'completed') {
          this.emitEvent('subagent.completed', {
            alias: subagent.alias,
            taskId: subagent.taskId,
            parentSessionId: subagent.parentSessionId,
            hasOutput: !!subagent.output,
          })
        } else if (subagent.state === 'failed') {
          this.emitEvent('subagent.failed', {
            alias: subagent.alias,
            taskId: subagent.taskId,
            error: subagent.error,
          })
        }
      }
    }

    // Stop polling if no active subagents
    if (!hasActive) {
      this.stopPolling()
    }
  }

  /**
   * Emit event to event store
   * Note: Subagent events are logged via background manager events
   */
  private emitEvent(_type: string, _data: Record<string, unknown>): void {
    // Subagent events are not in the core event schema
    // Background task events (background_task_started, etc.) cover this functionality
    // This is kept as a no-op hook for future extension
  }

  /**
   * Shutdown manager
   */
  shutdown(): void {
    this.stopPolling()
    this.subagents.clear()
    this.aliasToId.clear()
  }
}

// =============================================================================
// Singleton
// =============================================================================

let globalSubagentManager: SubagentManager | null = null

export function getSubagentManager(
  missionState: MissionState,
  cwd: string,
  client?: OpenCodeClient
): SubagentManager {
  if (!globalSubagentManager) {
    globalSubagentManager = new SubagentManager(missionState, cwd, client)
  }
  return globalSubagentManager
}

export function resetSubagentManager(): void {
  if (globalSubagentManager) {
    globalSubagentManager.shutdown()
    globalSubagentManager = null
  }
}
