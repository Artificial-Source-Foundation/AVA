/**
 * Agent Events
 * Processes AgentEvent stream and updates reactive signals
 */

import type { AgentEvent } from '@ava/core'
import type { Setter } from 'solid-js'
import { batch } from 'solid-js'
import { logError, logInfo, logWarn } from '../../services/logger'
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

      case 'turn:finish':
        if (event.toolCalls) {
          updateToolActivityBatch(signals.setToolActivity, event.toolCalls)
        }
        break

      case 'thought':
        signals.setCurrentThought((prev) => prev + event.text)
        break

      case 'tool:start':
        addToolActivity(signals.setToolActivity, {
          id: `${event.toolName}-${Date.now()}`,
          name: event.toolName,
          args: event.args ?? {},
          status: 'running',
          startedAt: event.timestamp,
        })
        break

      case 'tool:finish':
        updateToolActivity(signals.setToolActivity, event.toolName, {
          status: 'success',
          output: event.output,
          completedAt: event.timestamp,
          durationMs: event.durationMs,
        })
        break

      case 'tool:error':
        updateToolActivity(signals.setToolActivity, event.toolName, {
          status: 'error',
          error: event.error,
          completedAt: event.timestamp,
        })
        break

      case 'tool:metadata':
        break

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

      case 'recovery:start':
        logInfo('Agent', 'Recovery started')
        break

      case 'recovery:finish':
        logInfo('Agent', 'Recovery finished')
        break
    }
  }
}
