/**
 * Delegate tool factory — creates per-agent delegate tools.
 * Supports file cache, budget awareness, retries, and worktree isolation.
 */

import type { AgentEventCallback } from '@ava/core-v2/agent'
import { AgentExecutor, registerExecutor, unregisterExecutor } from '@ava/core-v2/agent'
import type { LLMProvider } from '@ava/core-v2/llm'
import type { AnyTool, ToolContext, ToolResult } from '@ava/core-v2/tools'
import { createWorktree, removeWorktree } from '../../git/src/worktree.js'
import type { AgentDefinition } from './agent-definition.js'
import { getAgent } from './registry.js'

export const REMOVED_DELEGATE_TOOLS = [
  'delegate_coder',
  'delegate_reviewer',
  'delegate_researcher',
  'delegate_explorer',
] as const

export interface DelegationConfig {
  /** Maximum retries on delegation failure. Default: 1 */
  maxRetries: number
  /** Run workers in isolated git worktrees. Default: false */
  isolation: boolean
  /** Maximum delegation nesting depth. Default: 3 */
  maxDelegationDepth: number
}

const DEFAULT_DELEGATION_CONFIG: DelegationConfig = {
  maxRetries: 1,
  isolation: false,
  maxDelegationDepth: 3,
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
  files?: Record<string, string>
}

/** Resolve full tool list for an agent, adding delegate tools when within depth limit. */
export function resolveTools(agent: AgentDefinition, depth = 0): string[] {
  const denied = agent.deniedTools ? new Set(agent.deniedTools) : null
  const maxDepth = delegationConfig.maxDelegationDepth
  let tools: string[]

  if (depth >= maxDepth) {
    // At max depth: strip all delegate tools regardless of tier
    tools = agent.tools.filter((t) => !t.startsWith('delegate_'))
  } else if (agent.tier === 'director' || agent.tier === 'commander') {
    // Director/Commander: tools are provided externally (invoke_team + meta tools)
    tools = agent.tools
  } else {
    // Other tiers may expose explicit delegates where needed
    const delegateTools = (agent.delegates ?? []).map((id) => `delegate_${id}`)
    tools = [...agent.tools, ...delegateTools]
  }

  // Apply deniedTools filter if present
  if (denied) {
    tools = tools.filter((t) => !denied.has(t))
  }

  return tools
}

/** Create a delegate tool that spawns a child AgentExecutor for the target agent. */
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
          files: {
            type: 'object',
            description: 'Pre-read file contents to share (path -> content map)',
            additionalProperties: { type: 'string' },
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

/** Create delegate tools for all agents in a delegates[] list. */
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

/** Build file cache prefix from shared files map. */
function buildFilePrefix(files: Record<string, string>): string {
  let prefix = ''
  for (const [path, content] of Object.entries(files)) {
    prefix += `<file path="${path}">\n${content}\n</file>\n\n`
  }
  return prefix
}

async function executeDelegation(
  agent: AgentDefinition,
  params: DelegateParams,
  ctx: ToolContext
): Promise<ToolResult> {
  // Build goal with optional file cache prefix
  let goalPrefix = ''
  if (params.files) {
    goalPrefix = buildFilePrefix(params.files)
  }
  const baseGoal =
    goalPrefix + (params.context ? `${params.task}\n\nContext:\n${params.context}` : params.task)

  const { maxRetries, isolation } = delegationConfig
  const attempt = 0
  let lastResult: ToolResult | undefined

  // Worktree isolation: create a worktree if configured
  let worktreePath: string | undefined
  let effectiveCwd = ctx.workingDirectory
  if (isolation) {
    try {
      const wt = await createWorktree(ctx.workingDirectory, crypto.randomUUID())
      worktreePath = wt.path
      effectiveCwd = wt.path
    } catch {
      // Worktree creation failed (not a git repo, etc.) — continue without isolation
    }
  }

  try {
    return await runDelegationLoop(
      agent,
      baseGoal,
      params,
      ctx,
      effectiveCwd,
      maxRetries,
      attempt,
      lastResult
    )
  } finally {
    // Clean up worktree if we created one
    if (worktreePath) {
      try {
        await removeWorktree(ctx.workingDirectory, worktreePath)
      } catch {
        // Best-effort cleanup
      }
    }
  }
}

async function runDelegationLoop(
  agent: AgentDefinition,
  baseGoal: string,
  params: DelegateParams,
  ctx: ToolContext,
  effectiveCwd: string,
  maxRetries: number,
  attempt: number,
  lastResult: ToolResult | undefined
): Promise<ToolResult> {
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

    const currentDepth = ctx.delegationDepth ?? 0
    const childTools = resolveTools(agent, currentDepth + 1)
    const maxTurns = agent.maxTurns ?? 15
    const budgetInstruction = `You have ${maxTurns} turns maximum. If you've used more than 50% without meaningful progress, call attempt_completion with a partial result rather than continuing to spin.`
    const systemPrompt =
      [agent.systemPrompt, budgetInstruction].filter(Boolean).join('\n\n') || undefined

    const child = new AgentExecutor(
      {
        id: childId,
        name: `${agent.tier}:${agent.name}`,
        provider: (agent.provider ?? ctx.provider) as LLMProvider | undefined,
        model: agent.model ?? ctx.model,
        allowedTools: childTools,
        maxTurns,
        maxTimeMinutes: agent.maxTimeMinutes ?? 5,
        systemPrompt,
        delegationDepth: currentDepth + 1,
      },
      ctx.onEvent as AgentEventCallback | undefined
    )

    // Notify extensions that a child agent session is starting
    ctx.onEvent?.({
      type: 'session:child-opened',
      sessionId: childId,
      parentSessionId: ctx.sessionId,
      workingDirectory: effectiveCwd,
    })

    // Register child executor for UI stop/message operations
    const childAbort = new AbortController()
    registerExecutor(childId, child, childAbort, ctx.sessionId, agent.name)

    try {
      // Use combined signal: abort if parent OR child-specific abort fires
      const combinedSignal = ctx.signal
        ? AbortSignal.any([ctx.signal, childAbort.signal])
        : childAbort.signal
      const result = await child.run({ goal, cwd: effectiveCwd }, combinedSignal)

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
    } finally {
      unregisterExecutor(childId)
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
