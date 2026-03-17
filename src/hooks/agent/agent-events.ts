/**
 * Agent Events
 * Processes agent event stream and updates reactive signals.
 *
 * The AgentEvent type is now defined locally — the Rust backend
 * emits events via Tauri IPC, not the TypeScript core-v2 agent.
 */

import type { Setter } from 'solid-js'
import { batch } from 'solid-js'
import { logError, logWarn } from '../../services/logger'
import { addToolActivity, updateToolActivity, updateToolActivityBatch } from './agent-tool-activity'
import type { ToolActivity } from './agent-types'

// ============================================================================
// Local AgentEvent type (replaces @ava/core-v2/agent import)
// ============================================================================

/** Agent event — union of all event types emitted during agent execution */
export type AgentEvent =
  | { type: 'agent:start'; agentId: string; goal?: string }
  | {
      type: 'agent:finish'
      agentId: string
      result: {
        success: boolean
        error?: string
        output?: string
        turns: number
        tokensUsed: { input: number; output: number }
        terminateMode?: string
      }
    }
  | { type: 'turn:start'; agentId: string; turn: number }
  | { type: 'turn:end'; agentId: string; turn: number; toolCalls?: ToolCallInfo[] }
  | { type: 'thought'; agentId: string; content: string }
  | { type: 'thinking'; agentId: string; content: string }
  | { type: 'tool:start'; agentId: string; toolName: string; args?: Record<string, unknown> }
  | {
      type: 'tool:finish'
      agentId: string
      toolName: string
      success: boolean
      output?: string
      durationMs?: number
    }
  | { type: 'tool:progress'; agentId: string; toolName: string; chunk: string }
  | {
      type: 'context:compacting'
      agentId: string
      estimatedTokens: number
      contextLimit: number
      messagesBefore: number
      messagesAfter: number
    }
  | {
      type: 'delegation:start'
      agentId: string
      childAgentId: string
      workerName: string
      task: string
      tier?: string
    }
  | {
      type: 'delegation:complete'
      agentId: string
      childAgentId: string
      workerName: string
      success: boolean
      output: string
      durationMs?: number
    }
  | { type: 'error'; agentId: string; error: string }

/** Tool call info from turn:end events */
export interface ToolCallInfo {
  name: string
  success: boolean
  result?: string
  durationMs?: number
}

// ============================================================================
// Event Handler
// ============================================================================

/** Signal setters needed by the event handler */
export interface AgentEventSignals {
  setCurrentAgentId: Setter<string | null>
  setCurrentTurn: Setter<number>
  setTokensUsed: Setter<number>
  setToolActivity: Setter<ToolActivity[]>
  setDoomLoopDetected: Setter<boolean>
  setLastError: Setter<string | null>
  setIsRunning: Setter<boolean>
  setCurrentThought: Setter<string>
}

/**
 * Create the agent event handler.
 * Takes signal setters + an optional team bridge callback.
 */
export function createAgentEventHandler(
  signals: AgentEventSignals,
  bridgeToTeam: (event: AgentEvent) => void
): (event: AgentEvent) => void {
  return function handleAgentEvent(event: AgentEvent): void {
    bridgeToTeam(event)

    switch (event.type) {
      case 'agent:start':
        batch(() => {
          signals.setCurrentAgentId(event.agentId)
          signals.setCurrentTurn(0)
          signals.setTokensUsed(0)
          signals.setToolActivity([])
          signals.setDoomLoopDetected(false)
          signals.setLastError(null)
        })
        break

      case 'agent:finish':
        batch(() => {
          signals.setIsRunning(false)
          if (!event.result.success) {
            signals.setLastError(event.result.error ?? 'Agent failed')
          }
        })
        break

      case 'turn:start':
        signals.setCurrentTurn(event.turn)
        break

      case 'turn:end':
        if (event.toolCalls) {
          updateToolActivityBatch(signals.setToolActivity, event.toolCalls)
        }
        break

      case 'thought':
        batch(() => {
          signals.setCurrentThought((prev) => prev + event.content)
        })
        break

      case 'tool:start':
        addToolActivity(signals.setToolActivity, {
          id: `${event.toolName}-${Date.now()}`,
          name: event.toolName,
          args: event.args ?? {},
          status: 'running',
          startedAt: Date.now(),
        })
        break

      case 'tool:finish':
        updateToolActivity(signals.setToolActivity, event.toolName, {
          status: 'success',
          completedAt: Date.now(),
          durationMs: event.durationMs,
        })
        break

      case 'delegation:start': {
        addToolActivity(signals.setToolActivity, {
          id: `delegate_${event.workerName}-${event.childAgentId}`,
          name: `delegate_${event.workerName}`,
          args: { task: event.task, worker: event.workerName },
          status: 'running',
          startedAt: Date.now(),
        })
        break
      }

      case 'delegation:complete': {
        updateToolActivity(signals.setToolActivity, `delegate_${event.workerName}`, {
          status: event.success ? 'success' : 'error',
          completedAt: Date.now(),
          durationMs: event.durationMs,
        })
        break
      }

      case 'error':
        logError('Agent', `Agent error: ${event.error}`)
        batch(() => {
          signals.setLastError(event.error)
          if (event.error.includes('Doom loop')) {
            signals.setDoomLoopDetected(true)
            logWarn('Agent', 'Doom loop detected')
          }
        })
        break
    }
  }
}
