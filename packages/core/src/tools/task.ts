/**
 * Task Tool
 * Spawn subagents for complex multi-step tasks
 *
 * Supports both single and parallel execution modes.
 * Based on OpenCode's task tool pattern.
 */

import { AgentExecutor } from '../agent/loop.js'
import {
  createSubagentManager,
  generateSubagentSessionId,
  type SubagentConfig,
  type SubagentResult,
  type SubagentTask,
  type SubagentType,
} from '../agent/subagent.js'
import type { AgentEvent } from '../agent/types.js'
import { AgentTerminateMode } from '../agent/types.js'
import { getEditorModelConfig } from '../llm/client.js'
import { ToolError, ToolErrorType } from './errors.js'
import { getToolDefinitions } from './registry.js'
import {
  executeParallel,
  MAX_CONCURRENCY,
  MAX_PARALLEL_TASKS,
  type ParallelTask,
} from './task-parallel.js'
import type { Tool, ToolContext, ToolResult } from './types.js'

// Re-export for consumers
export type { ParallelTask } from './task-parallel.js'

// ============================================================================
// Types
// ============================================================================

interface TaskParams {
  description: string
  prompt: string
  agentType: SubagentType
  sessionId?: string
  maxTurns?: number
  allowedTools?: string[]
  tasks?: ParallelTask[]
  maxConcurrent?: number
}

// ============================================================================
// Constants
// ============================================================================

const MAX_TURNS_LIMIT = 100
const VALID_AGENT_TYPES: SubagentType[] = ['explore', 'plan', 'execute', 'custom']

// ============================================================================
// Helper Functions
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

function formatResult(result: SubagentResult, config: SubagentConfig): string {
  const lines: string[] = [
    `## Subagent Result`,
    '',
    `**Agent:** ${config.name} (${config.type})`,
    `**Status:** ${result.success ? 'Completed' : 'Failed'}`,
    `**Turns Used:** ${result.turns}${config.maxTurns ? `/${config.maxTurns}` : ''}`,
    `**Termination:** ${result.terminationReason}`,
  ]

  if (result.error) {
    lines.push(`**Error:** ${result.error}`)
  }

  lines.push('')
  lines.push('### Output')
  lines.push('')
  lines.push(result.output || '(No output)')

  return lines.join('\n')
}

// ============================================================================
// Tool Implementation
// ============================================================================

export const taskTool: Tool<TaskParams> = {
  definition: {
    name: 'task',
    description: `Spawn subagents to handle complex, multi-step tasks autonomously.

Use this tool when:
- A task requires multiple steps or extensive exploration
- You need to delegate work while continuing other tasks
- Multiple independent tasks can run in parallel

Agent Types:
- **explore**: Read-only codebase exploration (glob, grep, read, ls)
- **plan**: Planning without execution (exploration + write plan files)
- **execute**: Full execution capabilities (all tools)
- **custom**: Custom tool set (specify with allowedTools)

Parallel mode: Provide a "tasks" array to run multiple subagents concurrently.`,
    input_schema: {
      type: 'object',
      properties: {
        description: { type: 'string', description: 'Short description (3-5 words)' },
        prompt: { type: 'string', description: 'Full task instructions' },
        agentType: {
          type: 'string',
          enum: ['explore', 'plan', 'execute', 'custom'],
          description: 'Type of subagent to spawn',
        },
        sessionId: { type: 'string', description: 'Session ID to resume (optional)' },
        maxTurns: { type: 'number', description: 'Maximum turns (default varies by type)' },
        allowedTools: {
          type: 'array',
          items: { type: 'string' },
          description: 'Tools to allow (only for custom type)',
        },
        tasks: {
          type: 'array',
          items: {
            type: 'object',
            properties: {
              description: { type: 'string' },
              prompt: { type: 'string' },
            },
            required: ['description', 'prompt'],
          },
          description: 'Multiple tasks to run in parallel',
        },
        maxConcurrent: {
          type: 'number',
          description: 'Max concurrent subagents (explore=5, plan=3, execute=1)',
        },
      },
      required: ['description', 'prompt', 'agentType'],
    },
  },

  validate(params: unknown): TaskParams {
    if (typeof params !== 'object' || params === null) {
      throw new ToolError('Invalid params: expected object', ToolErrorType.INVALID_PARAMS, 'task')
    }

    const p = params as Record<string, unknown>
    const {
      description,
      prompt,
      agentType,
      sessionId,
      maxTurns,
      allowedTools,
      tasks,
      maxConcurrent,
    } = p

    if (typeof description !== 'string' || !description.trim())
      throw new ToolError(
        'Invalid description: must be non-empty string',
        ToolErrorType.INVALID_PARAMS,
        'task'
      )

    if (typeof prompt !== 'string' || !prompt.trim())
      throw new ToolError(
        'Invalid prompt: must be non-empty string',
        ToolErrorType.INVALID_PARAMS,
        'task'
      )

    if (typeof agentType !== 'string' || !VALID_AGENT_TYPES.includes(agentType as SubagentType))
      throw new ToolError(
        `Invalid agentType: must be one of ${VALID_AGENT_TYPES.join(', ')}`,
        ToolErrorType.INVALID_PARAMS,
        'task'
      )

    if (sessionId !== undefined && typeof sessionId !== 'string')
      throw new ToolError('Invalid sessionId: must be string', ToolErrorType.INVALID_PARAMS, 'task')

    if (
      maxTurns !== undefined &&
      (typeof maxTurns !== 'number' || maxTurns < 1 || maxTurns > MAX_TURNS_LIMIT)
    )
      throw new ToolError(
        `Invalid maxTurns: must be 1-${MAX_TURNS_LIMIT}`,
        ToolErrorType.INVALID_PARAMS,
        'task'
      )

    if (allowedTools !== undefined) {
      if (!Array.isArray(allowedTools) || !allowedTools.every((t) => typeof t === 'string'))
        throw new ToolError(
          'Invalid allowedTools: must be string array',
          ToolErrorType.INVALID_PARAMS,
          'task'
        )
      if (agentType === 'custom' && allowedTools.length === 0)
        throw new ToolError(
          'Custom agent type requires at least one allowedTool',
          ToolErrorType.INVALID_PARAMS,
          'task'
        )
    }

    if (tasks !== undefined) {
      validateParallelTasks(tasks)
    }

    const agentTypeStr = agentType as SubagentType
    if (maxConcurrent !== undefined) {
      if (typeof maxConcurrent !== 'number' || maxConcurrent < 1)
        throw new ToolError(
          'Invalid maxConcurrent: must be positive',
          ToolErrorType.INVALID_PARAMS,
          'task'
        )
      if (maxConcurrent > MAX_CONCURRENCY[agentTypeStr])
        throw new ToolError(
          `Invalid maxConcurrent: ${agentTypeStr} limited to ${MAX_CONCURRENCY[agentTypeStr]}`,
          ToolErrorType.INVALID_PARAMS,
          'task'
        )
    }

    return {
      description: description.trim(),
      prompt: prompt.trim(),
      agentType: agentTypeStr,
      sessionId: typeof sessionId === 'string' ? sessionId.trim() : undefined,
      maxTurns: maxTurns as number | undefined,
      allowedTools: allowedTools as string[] | undefined,
      tasks: tasks as ParallelTask[] | undefined,
      maxConcurrent: maxConcurrent as number | undefined,
    }
  },

  async execute(params: TaskParams, ctx: ToolContext): Promise<ToolResult> {
    if (ctx.signal.aborted) {
      return {
        success: false,
        output: 'Operation was cancelled',
        error: ToolErrorType.EXECUTION_ABORTED,
      }
    }

    if (params.tasks && params.tasks.length > 0) {
      return executeParallel(
        {
          agentType: params.agentType,
          maxTurns: params.maxTurns,
          allowedTools: params.allowedTools,
          tasks: params.tasks,
          maxConcurrent: params.maxConcurrent,
        },
        ctx
      )
    }

    return executeSingle(params, ctx)
  },
}

// ============================================================================
// Validation Helpers
// ============================================================================

function validateParallelTasks(tasks: unknown): void {
  if (!Array.isArray(tasks))
    throw new ToolError('Invalid tasks: must be array', ToolErrorType.INVALID_PARAMS, 'task')
  if (tasks.length === 0)
    throw new ToolError(
      'Invalid tasks: must have at least one task',
      ToolErrorType.INVALID_PARAMS,
      'task'
    )
  if (tasks.length > MAX_PARALLEL_TASKS)
    throw new ToolError(
      `Invalid tasks: maximum ${MAX_PARALLEL_TASKS} allowed`,
      ToolErrorType.INVALID_PARAMS,
      'task'
    )

  for (const t of tasks) {
    if (typeof t !== 'object' || t === null)
      throw new ToolError(
        'Invalid tasks: each must be an object',
        ToolErrorType.INVALID_PARAMS,
        'task'
      )
    const obj = t as Record<string, unknown>
    if (typeof obj.description !== 'string' || !obj.description.trim())
      throw new ToolError(
        'Invalid tasks: each must have non-empty description',
        ToolErrorType.INVALID_PARAMS,
        'task'
      )
    if (typeof obj.prompt !== 'string' || !obj.prompt.trim())
      throw new ToolError(
        'Invalid tasks: each must have non-empty prompt',
        ToolErrorType.INVALID_PARAMS,
        'task'
      )
  }
}

// ============================================================================
// Single Task Execution
// ============================================================================

async function executeSingle(params: TaskParams, ctx: ToolContext): Promise<ToolResult> {
  const manager = createSubagentManager((event) => {
    if (ctx.metadata && event.type === 'subagent_progress') {
      ctx.metadata({
        title: `[${params.description}] Turn ${event.turn}`,
        metadata: { subagentId: event.subagentId, turn: event.turn, eventType: event.event.type },
      })
    }
  })

  const config = manager.createConfig(params.agentType, {
    maxTurns: params.maxTurns,
    allowedTools: params.allowedTools,
    parentSessionId: ctx.sessionId,
  })

  manager.register(config)

  const task: SubagentTask = {
    description: params.description,
    prompt: params.prompt,
    workingDirectory: ctx.workingDirectory,
  }

  if (ctx.metadata) {
    ctx.metadata({
      title: `Starting: ${params.description}`,
      metadata: {
        subagentId: config.id,
        agentType: params.agentType,
        maxTurns: config.maxTurns,
        task,
      },
    })
  }

  try {
    manager.emit({ type: 'subagent_started', subagentId: config.id, task })

    const sessionId = params.sessionId ?? generateSubagentSessionId(ctx.sessionId, config.id)
    const allowedTools =
      config.allowedTools ??
      getToolDefinitions()
        .map((t) => t.name)
        .filter((t) => t !== 'task')

    const eventCallback = (event: AgentEvent): void => {
      if (event.type === 'turn:start' || event.type === 'turn:finish') {
        manager.emit({ type: 'subagent_progress', subagentId: config.id, turn: event.turn, event })
      }
    }

    const editorConfig = getEditorModelConfig()
    const executor = new AgentExecutor(
      {
        id: `subagent-${config.type}-${Date.now()}`,
        name: config.name,
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
      { goal: params.prompt, context: config.systemPrompt, cwd: ctx.workingDirectory },
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

    manager.emit({ type: 'subagent_completed', subagentId: config.id, result })

    if (ctx.metadata) {
      ctx.metadata({
        title: `Completed: ${params.description}`,
        metadata: {
          subagentId: config.id,
          success: result.success,
          turns: result.turns,
          terminationReason: result.terminationReason,
        },
      })
    }

    return {
      success: result.success,
      output: formatResult(result, config),
      metadata: {
        subagentId: config.id,
        sessionId: result.sessionId,
        turns: result.turns,
        terminationReason: result.terminationReason,
      },
    }
  } catch (err) {
    const errorMessage = err instanceof Error ? err.message : String(err)
    manager.emit({ type: 'subagent_error', subagentId: config.id, error: errorMessage })

    return {
      success: false,
      output: `Subagent error: ${errorMessage}`,
      error: ToolErrorType.UNKNOWN,
      metadata: { subagentId: config.id, error: errorMessage },
    }
  } finally {
    manager.unregister(config.id)
  }
}
