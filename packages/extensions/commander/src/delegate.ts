/**
 * Delegate tool factory — creates per-agent delegate tools.
 *
 * Tier-aware: Leads get delegate tools for their workers.
 * Workers never get delegate tools. Commander only has delegate tools.
 * Each agent can use its own model/provider.
 * Supports automatic retry with enhanced prompt on failure.
 */

import type { AgentEventCallback } from '@ava/core-v2/agent'
import { AgentExecutor } from '@ava/core-v2/agent'
import type { LLMProvider } from '@ava/core-v2/llm'
import type { AnyTool, ToolContext, ToolResult } from '@ava/core-v2/tools'
import type { AgentDefinition } from './agent-definition.js'
import { getAgent } from './registry.js'

export interface DelegationConfig {
  /** Maximum retries on delegation failure. Default: 1 */
  maxRetries: number
}

const DEFAULT_DELEGATION_CONFIG: DelegationConfig = {
  maxRetries: 1,
}

let delegationConfig: DelegationConfig = { ...DEFAULT_DELEGATION_CONFIG }

/** Update delegation configuration. */
export function configureDelegation(config: Partial<DelegationConfig>): void {
  delegationConfig = { ...delegationConfig, ...config }
}

/** Get current delegation configuration. */
export function getDelegationConfig(): DelegationConfig {
  return { ...delegationConfig }
}

/** Reset delegation configuration to defaults. */
export function resetDelegationConfig(): void {
  delegationConfig = { ...DEFAULT_DELEGATION_CONFIG }
}

interface DelegateParams {
  task: string
  context?: string
}

/**
 * Resolve the full tool list for an agent, including delegate tools for leads.
 * Workers: own tools minus any delegate_* (safety filter).
 * Leads: own tools + delegate_<worker> for each worker in delegates[].
 * Commander: delegate_* tools only (added externally).
 */
export function resolveTools(agent: AgentDefinition): string[] {
  if (agent.tier === 'worker') {
    return agent.tools.filter((t) => !t.startsWith('delegate_'))
  }
  if (agent.tier === 'lead') {
    const delegateTools = (agent.delegates ?? []).map((id) => `delegate_${id}`)
    return [...agent.tools, ...delegateTools]
  }
  // Commander: tools are set externally (delegate_<lead> + meta tools)
  return agent.tools
}

/**
 * Create a delegate tool for a target agent.
 * When invoked, spawns a child AgentExecutor running as that agent.
 * If the target is a lead, it gets delegate tools for its own workers.
 */
export function createDelegateTool(agent: AgentDefinition): AnyTool {
  return {
    definition: {
      name: `delegate_${agent.name}`,
      description: `Delegate a task to ${agent.displayName}. ${agent.description}. Available tools: ${resolveTools(agent).join(', ')}`,
      input_schema: {
        type: 'object' as const,
        properties: {
          task: { type: 'string', description: 'What the agent should accomplish' },
          context: {
            type: 'string',
            description: 'Relevant context (file paths, requirements, constraints)',
          },
        },
        required: ['task'],
      },
    },

    execute(params: DelegateParams, ctx: ToolContext): Promise<ToolResult> {
      return executeDelegation(agent, params, ctx)
    },
  }
}

/**
 * Create delegate tools for all agents in a delegates[] list.
 * Used by leads to get their worker delegation tools, and by commander for leads.
 */
export function createDelegateToolsForAgent(parent: AgentDefinition): AnyTool[] {
  if (!parent.delegates?.length) return []

  return parent.delegates
    .map((id) => {
      const target = getAgent(id)
      if (!target) return null
      return createDelegateTool(target)
    })
    .filter((t): t is AnyTool => t !== null)
}

async function executeDelegation(
  agent: AgentDefinition,
  params: DelegateParams,
  ctx: ToolContext
): Promise<ToolResult> {
  const baseGoal = params.context ? `${params.task}\n\nContext:\n${params.context}` : params.task
  const { maxRetries } = delegationConfig
  let attempt = 0
  let lastResult: ToolResult | undefined

  while (attempt <= maxRetries) {
    const childId = crypto.randomUUID()
    const goal =
      attempt === 0
        ? baseGoal
        : `${baseGoal}\n\n[RETRY - Previous attempt failed: ${lastResult?.output.slice(0, 500)}. Try a different approach.]`

    // Emit retry event for attempts after the first
    if (attempt > 0) {
      ctx.onEvent?.({
        type: 'delegation:retry',
        agentId: ctx.sessionId,
        childAgentId: childId,
        workerName: agent.name,
        attempt,
        maxRetries,
      })
    }

    // Notify parent stream: delegation starting
    ctx.onEvent?.({
      type: 'delegation:start',
      agentId: ctx.sessionId,
      childAgentId: childId,
      workerName: agent.name,
      task: params.task,
      tier: agent.tier,
    })

    const childTools = resolveTools(agent)

    const child = new AgentExecutor(
      {
        id: childId,
        name: `${agent.tier}:${agent.name}`,
        provider: (agent.provider ?? ctx.provider) as LLMProvider | undefined,
        model: agent.model ?? ctx.model,
        allowedTools: childTools,
        maxTurns: agent.maxTurns ?? 15,
        maxTimeMinutes: agent.maxTimeMinutes ?? 5,
        systemPrompt: agent.systemPrompt || undefined,
      },
      ctx.onEvent as AgentEventCallback | undefined
    )

    try {
      const result = await child.run({ goal, cwd: ctx.workingDirectory }, ctx.signal)

      if (result.success || attempt >= maxRetries) {
        ctx.onEvent?.({
          type: 'delegation:complete',
          agentId: ctx.sessionId,
          childAgentId: childId,
          success: result.success,
          output: result.output,
        })

        return {
          success: result.success,
          output: result.output || `${agent.displayName} finished (${result.terminateMode})`,
        }
      }

      // Failed but can retry
      lastResult = {
        success: false,
        output: result.output || `${agent.displayName} finished (${result.terminateMode})`,
      }
      attempt++
    } catch (err) {
      const errorMsg = err instanceof Error ? err.message : String(err)

      if (attempt >= maxRetries) {
        ctx.onEvent?.({
          type: 'delegation:complete',
          agentId: ctx.sessionId,
          childAgentId: childId,
          success: false,
          output: errorMsg,
        })

        return {
          success: false,
          output: `${agent.displayName} failed: ${errorMsg}`,
        }
      }

      // Failed with exception but can retry
      lastResult = { success: false, output: errorMsg }
      attempt++
    }
  }

  // Should not reach here, but safety fallback
  return (
    lastResult ?? {
      success: false,
      output: `${agent.displayName} failed after ${maxRetries} retries`,
    }
  )
}
