/**
 * Parallel Task Execution
 * Execute multiple subagents concurrently with semaphore-based concurrency control
 *
 * - Read-only (explore) agents: up to 5 concurrent
 * - Write (execute) agents: limited to 1 for file safety
 * - Uses Semaphore from commander/parallel/batch for fair scheduling
 */

import { AgentExecutor } from '../agent/loop.js'
import {
  createSubagentManager,
  generateSubagentSessionId,
  type SubagentResult,
  type SubagentTask,
  type SubagentType,
} from '../agent/subagent.js'
import type { AgentEvent } from '../agent/types.js'
import { AgentTerminateMode } from '../agent/types.js'
import { Semaphore } from '../commander/parallel/batch.js'
import { getEditorModelConfig } from '../llm/client.js'
import { getToolDefinitions } from './registry.js'
import type { ToolContext, ToolResult } from './types.js'

// ============================================================================
// Types
// ============================================================================

/** A single task within a parallel batch */
export interface ParallelTask {
  /** Short description of the task (3-5 words) */
  description: string
  /** Full task prompt with detailed instructions */
  prompt: string
}

/** Params needed for parallel execution (subset of TaskParams) */
export interface ParallelExecutionParams {
  agentType: SubagentType
  maxTurns?: number
  allowedTools?: string[]
  tasks: ParallelTask[]
  maxConcurrent?: number
}

// ============================================================================
// Constants
// ============================================================================

/** Default concurrency by agent type */
export const DEFAULT_CONCURRENCY: Record<SubagentType, number> = {
  explore: 5,
  plan: 3,
  execute: 1,
  custom: 3,
}

/** Maximum concurrency by agent type (execute limited to 1 for file safety) */
export const MAX_CONCURRENCY: Record<SubagentType, number> = {
  explore: 5,
  plan: 3,
  execute: 1,
  custom: 5,
}

/** Maximum parallel tasks allowed */
export const MAX_PARALLEL_TASKS = 10

// ============================================================================
// Helpers
// ============================================================================

function mapTerminateMode(mode: AgentTerminateMode): SubagentResult['terminationReason'] {
  switch (mode) {
    case AgentTerminateMode.GOAL:
      return 'completed'
    case AgentTerminateMode.MAX_TURNS:
      return 'max_turns'
    case AgentTerminateMode.ABORTED:
      return 'cancelled'
    default:
      return 'error'
  }
}

// ============================================================================
// Parallel Execution
// ============================================================================

/**
 * Execute multiple subagents in parallel with concurrency control
 */
export async function executeParallel(
  params: ParallelExecutionParams,
  ctx: ToolContext
): Promise<ToolResult> {
  const { tasks } = params
  const concurrency = Math.min(
    params.maxConcurrent ?? DEFAULT_CONCURRENCY[params.agentType],
    MAX_CONCURRENCY[params.agentType]
  )

  const semaphore = new Semaphore(concurrency)
  const manager = createSubagentManager((event) => {
    if (ctx.metadata && event.type === 'subagent_progress') {
      ctx.metadata({
        title: `[${event.subagentId}] Turn ${event.turn}`,
        metadata: {
          subagentId: event.subagentId,
          turn: event.turn,
          eventType: event.event.type,
        },
      })
    }
  })

  // Stream batch start metadata
  if (ctx.metadata) {
    ctx.metadata({
      title: `Starting batch: ${tasks.length} tasks (concurrency: ${concurrency})`,
      metadata: {
        agentType: params.agentType,
        taskCount: tasks.length,
        concurrency,
      },
    })
  }

  // Create execution promises for each task
  const promises = tasks.map((task, index) =>
    executeSingleInBatch(task, index, params, ctx, manager, semaphore)
  )

  // Wait for all to settle (failure isolation)
  const settlements = await Promise.allSettled(promises)

  // Collect results
  const results: Array<{ description: string; result: SubagentResult | null; error?: string }> = []
  let allSuccess = true

  for (let i = 0; i < settlements.length; i++) {
    const settlement = settlements[i]
    if (settlement.status === 'fulfilled') {
      results.push({
        description: tasks[i].description,
        result: settlement.value,
      })
      if (!settlement.value.success) allSuccess = false
    } else {
      allSuccess = false
      results.push({
        description: tasks[i].description,
        result: null,
        error: String(settlement.reason),
      })
    }
  }

  // Format combined output
  const output = formatParallelResults(results, params.agentType, concurrency)

  // Stream batch completion
  if (ctx.metadata) {
    ctx.metadata({
      title: `Batch completed: ${results.filter((r) => r.result?.success).length}/${tasks.length} succeeded`,
      metadata: {
        totalTasks: tasks.length,
        succeeded: results.filter((r) => r.result?.success).length,
        failed: results.filter((r) => !r.result?.success).length,
      },
    })
  }

  return {
    success: allSuccess,
    output,
    metadata: {
      parallel: true,
      taskCount: tasks.length,
      concurrency,
      succeeded: results.filter((r) => r.result?.success).length,
      failed: results.filter((r) => !r.result?.success).length,
    },
  }
}

/**
 * Execute a single task within a parallel batch, with semaphore control
 */
async function executeSingleInBatch(
  task: ParallelTask,
  index: number,
  params: ParallelExecutionParams,
  ctx: ToolContext,
  manager: ReturnType<typeof createSubagentManager>,
  semaphore: Semaphore
): Promise<SubagentResult> {
  await semaphore.acquire()

  try {
    if (ctx.signal.aborted) {
      return {
        subagentId: `batch-${index}`,
        success: false,
        output: 'Aborted before execution',
        turns: 0,
        terminationReason: 'cancelled',
        sessionId: '',
      }
    }

    const config = manager.createConfig(params.agentType, {
      maxTurns: params.maxTurns,
      allowedTools: params.allowedTools,
      parentSessionId: ctx.sessionId,
    })

    manager.register(config)

    try {
      const subagentTask: SubagentTask = {
        description: task.description,
        prompt: task.prompt,
        workingDirectory: ctx.workingDirectory,
      }

      manager.emit({
        type: 'subagent_started',
        subagentId: config.id,
        task: subagentTask,
      })

      const sessionId = generateSubagentSessionId(ctx.sessionId, config.id)

      const allowedTools =
        config.allowedTools ??
        getToolDefinitions()
          .map((t) => t.name)
          .filter((t) => t !== 'task')

      const eventCallback = (event: AgentEvent): void => {
        if (event.type === 'turn:start' || event.type === 'turn:finish') {
          manager.emit({
            type: 'subagent_progress',
            subagentId: config.id,
            turn: event.turn,
            event,
          })
        }
      }

      const editorConfig = getEditorModelConfig()

      const executor = new AgentExecutor(
        {
          id: `subagent-batch-${config.type}-${index}-${Date.now()}`,
          name: `${config.name} #${index + 1}`,
          maxTurns: config.maxTurns ?? 30,
          maxTimeMinutes: 10,
          maxRetries: 2,
          gracePeriodMs: 30 * 1000,
          tools: allowedTools,
          model: editorConfig.model,
          provider: editorConfig.provider as 'anthropic' | 'openai' | 'openrouter',
        },
        eventCallback
      )

      const agentResult = await executor.run(
        {
          goal: task.prompt,
          context: config.systemPrompt,
          cwd: ctx.workingDirectory,
        },
        ctx.signal
      )

      const result: SubagentResult = {
        subagentId: config.id,
        success: agentResult.success,
        output: agentResult.output,
        turns: agentResult.turns,
        terminationReason: mapTerminateMode(agentResult.terminateMode),
        error: agentResult.error,
        sessionId,
      }

      manager.emit({
        type: 'subagent_completed',
        subagentId: config.id,
        result,
      })

      return result
    } finally {
      manager.unregister(config.id)
    }
  } finally {
    semaphore.release()
  }
}

/**
 * Format parallel results for LLM consumption
 */
function formatParallelResults(
  results: Array<{ description: string; result: SubagentResult | null; error?: string }>,
  agentType: SubagentType,
  concurrency: number
): string {
  const lines: string[] = [
    `## Parallel Task Results`,
    '',
    `**Agent Type:** ${agentType}`,
    `**Tasks:** ${results.length}`,
    `**Concurrency:** ${concurrency}`,
    `**Succeeded:** ${results.filter((r) => r.result?.success).length}/${results.length}`,
    '',
  ]

  for (let i = 0; i < results.length; i++) {
    const { description, result, error } = results[i]
    lines.push(`### Task ${i + 1}: ${description}`)
    lines.push('')

    if (result) {
      lines.push(`**Status:** ${result.success ? 'Completed' : 'Failed'}`)
      lines.push(`**Turns:** ${result.turns}`)
      lines.push(`**Termination:** ${result.terminationReason}`)
      if (result.error) lines.push(`**Error:** ${result.error}`)
      lines.push('')
      lines.push(result.output || '(No output)')
    } else {
      lines.push(`**Status:** Error`)
      lines.push(`**Error:** ${error || 'Unknown error'}`)
    }

    lines.push('')
  }

  return lines.join('\n')
}
