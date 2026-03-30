/**
 * Agent Team Bridge
 * Maps agent events to the team hierarchy store for visualization.
 * Also handles team:stop and team:message events from the UI.
 */

import { invoke } from '@tauri-apps/api/core'
import { logDebug, logInfo } from '../../services/logger'
import type { useTeam } from '../../stores/team'
import type { AgentEvent } from './agent-events'

type TeamStore = ReturnType<typeof useTeam>

const TAG = 'team-bridge'

/**
 * Create a bridge that maps agent events into team store mutations.
 * Returns the bridgeToTeam function, stopAgent, and sendMessage helpers.
 *
 * @param isTeamMode - Reactive getter that returns true only when team/hq mode is active.
 *   When false, events are silently ignored (no team members created in solo mode).
 */
export function createTeamBridge(
  teamStore: TeamStore,
  isTeamMode: () => boolean = () => true
): {
  bridgeToTeam: (event: AgentEvent) => void
  stopAgent: (memberId: string) => boolean
  sendMessage: (memberId: string, message: string) => void
} {
  // Track last thought per agent for delegation context
  const lastThought: Record<string, string> = {}
  // Track tool call IDs for finish events (core-v2 events don't carry timestamps)
  const runningTools: Record<string, string> = {} // agentId:toolName → toolId
  // Track current thought message ID per agent for accumulation
  const currentThoughtId: Record<string, string> = {}
  // Track accumulated thought content per agent
  const accumulatedThought: Record<string, string> = {}

  function bridgeToTeam(event: AgentEvent): void {
    // Only process events when team/hq mode is active
    if (!isTeamMode()) return

    switch (event.type) {
      case 'agent:start': {
        logInfo(TAG, `agent:start ${event.agentId} goal=${event.goal?.slice(0, 80)}`)

        // Skip if this agent was already created by delegation:start
        if (teamStore.teamMembers().has(event.agentId)) break

        const role = teamStore.agentTypeToRole('commander')
        const domain = teamStore.inferDomain(event.goal)
        const name = teamStore.generateName(role, domain)

        // Determine parent based on role
        let parentId: string | null = null
        if (role === 'senior-lead') {
          parentId = teamStore.teamLead()?.id ?? null
        } else if (role === 'junior-dev') {
          const activeLead =
            teamStore
              .seniorLeads()
              .find((lead) => lead.domain === domain && lead.status === 'working') ??
            teamStore.seniorLeads().find((lead) => lead.status === 'working')
          parentId = activeLead?.id ?? teamStore.teamLead()?.id ?? null
        }

        const delegationContext = parentId ? lastThought[parentId] : undefined

        teamStore.addMember({
          id: event.agentId,
          name,
          role,
          status: 'working',
          parentId,
          domain,
          model: 'unknown',
          task: event.goal,
          toolCalls: [],
          messages: [],
          createdAt: Date.now(),
          delegatedAt: Date.now(),
          delegationContext,
        })
        break
      }

      case 'delegation:start': {
        logInfo(
          TAG,
          `delegation:start parent=${event.agentId} child=${event.childAgentId} worker=${event.workerName} task=${event.task.slice(0, 80)}`
        )

        const domain = teamStore.inferDomain(event.task)
        const workerLabel = event.workerName.charAt(0).toUpperCase() + event.workerName.slice(1)

        // Map HQ tier to team role
        const role =
          event.tier === 'lead'
            ? 'senior-lead'
            : event.tier === 'worker'
              ? 'junior-dev'
              : 'senior-lead'

        teamStore.addMember({
          id: event.childAgentId,
          name: workerLabel,
          role,
          status: 'working',
          parentId: event.agentId,
          domain,
          model: 'unknown',
          task: event.task,
          toolCalls: [],
          messages: [],
          createdAt: Date.now(),
          delegatedAt: Date.now(),
          delegationContext: lastThought[event.agentId],
        })
        break
      }

      case 'delegation:complete': {
        logInfo(
          TAG,
          `delegation:complete child=${event.childAgentId} success=${event.success} output=${event.output?.slice(0, 100)}`
        )
        teamStore.updateMemberStatus(event.childAgentId, event.success ? 'done' : 'error')
        teamStore.updateMember(event.childAgentId, {
          result: event.output,
          completedAt: Date.now(),
        })
        break
      }

      case 'agent:finish':
        logInfo(
          TAG,
          `agent:finish ${event.agentId} success=${event.result.success} output=${event.result.output?.slice(0, 100)}`
        )
        teamStore.updateMemberStatus(event.agentId, event.result.success ? 'done' : 'error')
        if (!event.result.success) {
          teamStore.updateMember(event.agentId, {
            error: event.result.error,
          })
        }
        if (event.result.output) {
          teamStore.updateMember(event.agentId, {
            result: event.result.output,
          })
        }
        if (event.result.tokensUsed) {
          teamStore.updateMember(event.agentId, {
            tokenUsage: {
              input: event.result.tokensUsed.input,
              output: event.result.tokensUsed.output,
            },
          })
        }
        delete lastThought[event.agentId]
        delete currentThoughtId[event.agentId]
        delete accumulatedThought[event.agentId]
        break

      case 'thought': {
        const existingId = currentThoughtId[event.agentId]
        lastThought[event.agentId] = event.content

        if (existingId) {
          // Accumulate into existing thought message
          accumulatedThought[event.agentId] =
            (accumulatedThought[event.agentId] ?? '') + event.content
          teamStore.updateMessage(event.agentId, existingId, accumulatedThought[event.agentId]!)
          logDebug(
            TAG,
            `thought:update ${event.agentId} len=${accumulatedThought[event.agentId]!.length}`
          )
        } else {
          // Create new thought message
          const id = `thought-${Date.now()}`
          currentThoughtId[event.agentId] = id
          accumulatedThought[event.agentId] = event.content
          teamStore.addMessage(event.agentId, {
            id,
            role: 'assistant',
            content: event.content,
            timestamp: Date.now(),
          })
          logDebug(TAG, `thought:new ${event.agentId} id=${id}`)
        }
        break
      }

      case 'tool:start': {
        const ts = Date.now()
        const toolId = `${event.toolName}-${ts}`
        runningTools[`${event.agentId}:${event.toolName}`] = toolId
        logDebug(TAG, `tool:start ${event.agentId} ${event.toolName}`, event.args)

        // Reset thought accumulation — new thinking block starts after tool calls
        delete currentThoughtId[event.agentId]
        delete accumulatedThought[event.agentId]

        teamStore.addToolCall(event.agentId, {
          id: toolId,
          name: event.toolName,
          status: 'running',
          args: event.args,
          startedAt: ts,
        })
        break
      }

      case 'tool:finish': {
        const key = `${event.agentId}:${event.toolName}`
        const toolId = runningTools[key]
        if (toolId) {
          const output = event.output
          const error = event.success ? undefined : String(output ?? '')
          logDebug(
            TAG,
            `tool:finish ${event.agentId} ${event.toolName} success=${event.success} ${event.durationMs}ms`,
            { output: output?.slice(0, 100) }
          )
          teamStore.updateToolCall(event.agentId, toolId, {
            status: event.success ? 'success' : 'error',
            durationMs: event.durationMs,
            output,
            error,
            completedAt: Date.now(),
          })
          delete runningTools[key]
        }
        break
      }
    }
  }

  /** Stop a running agent by its member ID via Rust backend. */
  function stopAgent(memberId: string): boolean {
    logInfo(TAG, `stopAgent ${memberId}`)
    try {
      void invoke('cancel_agent')
      teamStore.updateMemberStatus(memberId, 'error')
      teamStore.updateMember(memberId, { error: 'Stopped by user' })
      return true
    } catch {
      return false
    }
  }

  /** Send a follow-up message to a running agent (stored in team messages). */
  function sendMessage(memberId: string, message: string): void {
    logInfo(TAG, `sendMessage ${memberId} content=${message.slice(0, 80)}`)
    teamStore.addMessage(memberId, {
      id: `user-${Date.now()}`,
      role: 'user',
      content: message,
      timestamp: Date.now(),
    })
  }

  return { bridgeToTeam, stopAgent, sendMessage }
}
