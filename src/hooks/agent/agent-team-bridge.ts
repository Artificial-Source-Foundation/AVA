/**
 * Agent Team Bridge
 * Maps agent events to the team hierarchy store for visualization
 */

import type { AgentEvent } from '@ava/core-v2/agent'
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
  // Track tool call IDs for finish events (core-v2 events don't carry timestamps)
  const runningTools: Record<string, string> = {} // agentId:toolName → toolId

  function bridgeToTeam(event: AgentEvent): void {
    switch (event.type) {
      case 'agent:start': {
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
        const e = event as {
          agentId: string
          childAgentId: string
          workerName: string
          task: string
          tier?: string
        }
        const domain = teamStore.inferDomain(e.task)
        const workerLabel = e.workerName.charAt(0).toUpperCase() + e.workerName.slice(1)

        // Map Praxis tier to team role
        const role =
          e.tier === 'lead' ? 'senior-lead' : e.tier === 'worker' ? 'junior-dev' : 'senior-lead'

        teamStore.addMember({
          id: e.childAgentId,
          name: workerLabel,
          role,
          status: 'working',
          parentId: e.agentId,
          domain,
          model: 'unknown',
          task: e.task,
          toolCalls: [],
          messages: [],
          createdAt: Date.now(),
          delegatedAt: Date.now(),
          delegationContext: lastThought[e.agentId],
        })
        break
      }

      case 'delegation:complete': {
        const e = event as { childAgentId: string; success: boolean; output: string }
        teamStore.updateMemberStatus(e.childAgentId, e.success ? 'done' : 'error')
        teamStore.updateMember(e.childAgentId, {
          result: e.output,
          completedAt: Date.now(),
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

      case 'thought': {
        const now = Date.now()
        lastThought[event.agentId] = event.content
        teamStore.addMessage(event.agentId, {
          id: `thought-${now}`,
          role: 'assistant',
          content: event.content,
          timestamp: now,
        })
        break
      }

      case 'tool:start': {
        const ts = Date.now()
        const toolId = `${event.toolName}-${ts}`
        runningTools[`${event.agentId}:${event.toolName}`] = toolId
        teamStore.addToolCall(event.agentId, {
          id: toolId,
          name: event.toolName,
          status: 'running',
          timestamp: ts,
        })
        break
      }

      case 'tool:finish': {
        const key = `${event.agentId}:${event.toolName}`
        const toolId = runningTools[key]
        if (toolId) {
          teamStore.updateToolCall(event.agentId, toolId, {
            status: event.success ? 'success' : 'error',
            durationMs: event.durationMs,
          })
          delete runningTools[key]
        }
        break
      }
    }
  }

  return { bridgeToTeam }
}
