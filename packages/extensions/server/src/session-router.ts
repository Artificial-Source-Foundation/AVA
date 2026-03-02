/**
 * Session router — maps run IDs to AgentExecutor instances.
 *
 * Manages lifecycle of agent runs: create, stream events, steer, abort.
 */

import type { ExtensionAPI } from '@ava/core-v2/extensions'
import type { RunRequest, RunStatus, ServerEvent, SteerResponse } from './types.js'

interface ActiveRun {
  runId: string
  goal: string
  status: 'running' | 'completed' | 'error' | 'aborted'
  startedAt: number
  completedAt?: number
  events: ServerEvent[]
  listeners: Set<(event: ServerEvent) => void>
  abort?: AbortController
  result?: string
  error?: string
}

export class SessionRouter {
  private runs = new Map<string, ActiveRun>()

  constructor(private api: ExtensionAPI) {}

  /** Start a new agent run and return the run ID. */
  async startRun(request: RunRequest): Promise<string> {
    const runId = crypto.randomUUID()
    const abortController = new AbortController()

    const run: ActiveRun = {
      runId,
      goal: request.goal,
      status: 'running',
      startedAt: Date.now(),
      events: [],
      listeners: new Set(),
      abort: abortController,
    }

    this.runs.set(runId, run)

    // Start agent execution asynchronously
    this.executeRun(run, request).catch((err) => {
      run.status = 'error'
      run.error = String(err)
      run.completedAt = Date.now()
      this.pushEvent(run, { type: 'error', data: { message: String(err) }, timestamp: Date.now() })
      this.pushEvent(run, { type: 'done', data: {}, timestamp: Date.now() })
    })

    return runId
  }

  /** Get current status of a run. */
  getStatus(runId: string): RunStatus | null {
    const run = this.runs.get(runId)
    if (!run) return null
    return {
      runId: run.runId,
      status: run.status,
      startedAt: run.startedAt,
      completedAt: run.completedAt,
      result: run.result,
      error: run.error,
    }
  }

  /** Subscribe to events from a run. Returns unsubscribe function. */
  subscribe(runId: string, listener: (event: ServerEvent) => void): (() => void) | null {
    const run = this.runs.get(runId)
    if (!run) return null

    // Send backlog first
    for (const event of run.events) {
      listener(event)
    }

    run.listeners.add(listener)
    return () => run.listeners.delete(listener)
  }

  /** Send a steering message to a running agent. */
  steer(runId: string, message: string): SteerResponse {
    const run = this.runs.get(runId)
    if (!run || run.status !== 'running') {
      return { accepted: false }
    }

    this.api.emit('agent:steer', { runId, message })
    this.pushEvent(run, {
      type: 'status',
      data: { message: `Steering: ${message}` },
      timestamp: Date.now(),
    })

    return { accepted: true }
  }

  /** Abort a running agent. */
  abort(runId: string): boolean {
    const run = this.runs.get(runId)
    if (!run || run.status !== 'running') return false

    run.abort?.abort()
    run.status = 'aborted'
    run.completedAt = Date.now()
    this.pushEvent(run, { type: 'done', data: { aborted: true }, timestamp: Date.now() })
    return true
  }

  /** Clean up completed runs older than maxAge ms. */
  cleanup(maxAge: number = 3600_000): void {
    const cutoff = Date.now() - maxAge
    for (const [id, run] of this.runs) {
      if (run.status !== 'running' && (run.completedAt ?? 0) < cutoff) {
        this.runs.delete(id)
      }
    }
  }

  get activeCount(): number {
    let count = 0
    for (const run of this.runs.values()) {
      if (run.status === 'running') count++
    }
    return count
  }

  private async executeRun(run: ActiveRun, request: RunRequest): Promise<void> {
    // Emit events as the agent runs
    const eventHandler = (data: unknown) => {
      const event = data as Record<string, unknown>
      const serverEvent: ServerEvent = {
        type: (event.type as ServerEvent['type']) ?? 'text',
        data: event,
        timestamp: Date.now(),
      }
      this.pushEvent(run, serverEvent)
    }

    const unsub = this.api.on('agent:event', eventHandler)

    try {
      // Emit the goal as an agent:run event for the agent loop to pick up
      this.api.emit('server:run', {
        runId: run.runId,
        goal: request.goal,
        context: request.context,
        tools: request.tools,
        provider: request.provider,
        model: request.model,
      })

      // Wait for completion event
      await new Promise<void>((resolve) => {
        const completionHandler = (data: unknown) => {
          const ev = data as Record<string, unknown>
          if (ev.runId === run.runId) {
            run.status = 'completed'
            run.result = ev.result as string | undefined
            run.completedAt = Date.now()
            this.pushEvent(run, { type: 'done', data: {}, timestamp: Date.now() })
            completionUnsub.dispose()
            resolve()
          }
        }
        const completionUnsub = this.api.on('server:run-complete', completionHandler)

        // Also resolve on abort
        run.abort?.signal.addEventListener('abort', () => resolve(), { once: true })
      })
    } finally {
      unsub.dispose()
    }
  }

  private pushEvent(run: ActiveRun, event: ServerEvent): void {
    run.events.push(event)
    for (const listener of run.listeners) {
      listener(event)
    }
  }

  dispose(): void {
    for (const run of this.runs.values()) {
      run.abort?.abort()
    }
    this.runs.clear()
  }
}
