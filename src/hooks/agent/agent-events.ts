/**
 * Agent Events
 * Processes AgentEvent stream and updates reactive signals
 */

import type { AgentEvent } from '@ava/core-v2/agent'
import type { Setter } from 'solid-js'
import { batch } from 'solid-js'
import { logError, logWarn } from '../../services/logger'
import { addToolActivity, updateToolActivity, updateToolActivityBatch } from './agent-tool-activity'
import type { ToolActivity } from './agent-types'

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
        const e = event as AgentEvent & { workerName: string; childAgentId: string; task: string }
        addToolActivity(signals.setToolActivity, {
          id: `delegate_${e.workerName}-${e.childAgentId}`,
          name: `delegate_${e.workerName}`,
          args: { task: e.task, worker: e.workerName },
          status: 'running',
          startedAt: Date.now(),
        })
        break
      }

      case 'delegation:complete': {
        const e = event as AgentEvent & {
          workerName: string
          success: boolean
          durationMs?: number
        }
        updateToolActivity(signals.setToolActivity, `delegate_${e.workerName}`, {
          status: e.success ? 'success' : 'error',
          completedAt: Date.now(),
          durationMs: e.durationMs,
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
