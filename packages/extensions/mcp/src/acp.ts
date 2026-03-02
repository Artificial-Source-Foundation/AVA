/**
 * ACP — Agent Communication Protocol
 *
 * Delegates to the server extension's SessionRouter for actual execution.
 * Provides the same interface as the ACP spec but backed by real agent runs.
 *
 * Endpoints:
 *   POST /run          — Start a new agent run
 *   GET  /run/:id/stream — SSE stream of agent events
 *   POST /run/:id/steer  — Send a steering message to a running agent
 */

import type { ExtensionAPI } from '@ava/core-v2/extensions'

// ─── Types ──────────────────────────────────────────────────────────────────

export interface ACPRunRequest {
  goal: string
  context?: string
  tools?: string[]
}

export interface ACPRunResponse {
  runId: string
}

export interface ACPSteerRequest {
  message: string
}

export interface ACPSteerResponse {
  accepted: boolean
}

export interface ACPStreamEvent {
  type: 'text' | 'tool_use' | 'tool_result' | 'error' | 'done'
  data: Record<string, unknown>
}

// ─── Server ─────────────────────────────────────────────────────────────────

/**
 * ACP server that delegates to the server extension's session router.
 * If no server extension is active, methods throw descriptive errors.
 */
export class ACPServer {
  constructor(private api?: ExtensionAPI) {}

  /** POST /run — Start a new agent run via server extension. */
  async run(request: ACPRunRequest): Promise<ACPRunResponse> {
    if (!this.api) throw new Error('ACP: No ExtensionAPI provided')

    // Delegate to server extension via event
    const runId = crypto.randomUUID()
    this.api.emit('server:run', {
      runId,
      goal: request.goal,
      context: request.context,
      tools: request.tools,
    })

    return { runId }
  }

  /** GET /run/:id/stream — SSE stream of agent events. */
  async *stream(runId: string): AsyncGenerator<ACPStreamEvent, void, unknown> {
    if (!this.api) throw new Error('ACP: No ExtensionAPI provided')

    // Create a channel for events
    const events: ACPStreamEvent[] = []
    let resolve: (() => void) | null = null
    let done = false

    const unsub = this.api.on('agent:event', (data: unknown) => {
      const ev = data as Record<string, unknown>
      if (ev.runId !== runId) return

      const event: ACPStreamEvent = {
        type: (ev.type as ACPStreamEvent['type']) ?? 'text',
        data: ev,
      }
      events.push(event)
      if (event.type === 'done') done = true
      resolve?.()
    })

    try {
      while (!done) {
        if (events.length === 0) {
          await new Promise<void>((r) => {
            resolve = r
          })
        }
        while (events.length > 0) {
          yield events.shift()!
        }
      }
    } finally {
      unsub.dispose()
    }
  }

  /** POST /run/:id/steer — Steer a running agent. */
  async steer(runId: string, request: ACPSteerRequest): Promise<ACPSteerResponse> {
    if (!this.api) throw new Error('ACP: No ExtensionAPI provided')

    this.api.emit('agent:steer', { runId, message: request.message })
    return { accepted: true }
  }
}
