/**
 * Agent Team Bridge
 * Maps agent events to the team hierarchy store for visualization
 */

import type { AgentEvent } from '@ava/core'
import type { useTeam } from '../../stores/team'

type TeamStore = ReturnType<typeof useTeam>

/**
 * Create a bridge that maps agent events into team store mutations.
 * Returns the bridgeToTeam function and its internal state.
 */
export function createTeamBridge(teamStore: TeamStore): {
  bridgeToTeam: (event: AgentEvent) => void
} {
  // Track last thought per agent for delegation context
  const lastThought: Record<string, string> = {}

  function bridgeToTeam(event: AgentEvent): void {
    switch (event.type) {
      case 'agent:start': {
        const role = teamStore.agentTypeToRole(event.config?.name ?? 'commander')
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
          model: event.config?.model ?? 'unknown',
          task: event.goal,
          toolCalls: [],
          messages: [],
          createdAt: event.timestamp,
          delegatedAt: event.timestamp,
          delegationContext,
        })
        break
      }

      case 'agent:finish':
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
        delete lastThought[event.agentId]
        break

      case 'thought':
        lastThought[event.agentId] = event.text
        teamStore.addMessage(event.agentId, {
          id: `thought-${event.timestamp}`,
          role: 'assistant',
          content: event.text,
          timestamp: event.timestamp,
        })
        break

      case 'tool:start':
        teamStore.addToolCall(event.agentId, {
          id: `${event.toolName}-${event.timestamp}`,
          name: event.toolName,
          status: 'running',
          timestamp: event.timestamp,
        })
        break

      case 'tool:finish':
        teamStore.updateToolCall(event.agentId, `${event.toolName}-${event.timestamp}`, {
          status: 'success',
          durationMs: event.durationMs,
        })
        break

      case 'tool:error':
        teamStore.updateToolCall(event.agentId, `${event.toolName}-${event.timestamp}`, {
          status: 'error',
        })
        break
    }
  }

  return { bridgeToTeam }
}
