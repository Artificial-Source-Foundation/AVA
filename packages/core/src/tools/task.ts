/**
 * Task Tool
 * Spawn subagents for complex multi-step tasks
 *
 * Based on OpenCode's task tool pattern
 */

import {
  createSubagentManager,
  generateSubagentSessionId,
  type SubagentConfig,
  type SubagentResult,
  type SubagentTask,
  type SubagentType,
} from '../agent/subagent.js'
import { ToolError, ToolErrorType } from './errors.js'
import type { Tool, ToolContext, ToolResult } from './types.js'

// ============================================================================
// Types
// ============================================================================

interface TaskParams {
  /** Short description of the task (3-5 words) */
  description: string
  /** Full task prompt with detailed instructions */
  prompt: string
  /** Subagent type to use */
  agentType: SubagentType
  /** Session ID to resume (optional) */
  sessionId?: string
  /** Maximum turns (optional, uses preset default) */
  maxTurns?: number
  /** Custom tools to allow (for 'custom' type) */
  allowedTools?: string[]
}

// ============================================================================
// Constants
// ============================================================================

/** Maximum turns allowed for any subagent */
const MAX_TURNS_LIMIT = 100

/** Valid agent types */
const VALID_AGENT_TYPES: SubagentType[] = ['explore', 'plan', 'execute', 'custom']

// ============================================================================
// Helper Functions
// ============================================================================

/**
 * Format subagent result for LLM consumption
 */
function formatResult(result: SubagentResult, config: SubagentConfig): string {
  const lines: string[] = [
    `## Subagent Result`,
    '',
    `**Agent:** ${config.name} (${config.type})`,
    `**Status:** ${result.success ? '✓ Completed' : '✗ Failed'}`,
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
    description: `Spawn a subagent to handle complex, multi-step tasks autonomously.

Use this tool when:
- A task requires multiple steps or extensive exploration
- You need to delegate work while continuing other tasks
- The task would benefit from focused, isolated execution

Agent Types:
- **explore**: Read-only codebase exploration (glob, grep, read, ls)
- **plan**: Planning without execution (exploration + write plan files)
- **execute**: Full execution capabilities (all tools)
- **custom**: Custom tool set (specify with allowedTools)

The subagent runs independently and returns results when done.`,
    input_schema: {
      type: 'object',
      properties: {
        description: {
          type: 'string',
          description: 'Short description (3-5 words) for status display',
        },
        prompt: {
          type: 'string',
          description: 'Full task instructions with all necessary context',
        },
        agentType: {
          type: 'string',
          enum: ['explore', 'plan', 'execute', 'custom'],
          description: 'Type of subagent to spawn',
        },
        sessionId: {
          type: 'string',
          description: 'Session ID to resume (optional)',
        },
        maxTurns: {
          type: 'number',
          description: 'Maximum turns before termination (default varies by type)',
        },
        allowedTools: {
          type: 'array',
          items: { type: 'string' },
          description: 'Tools to allow (only for custom type)',
        },
      },
      required: ['description', 'prompt', 'agentType'],
    },
  },

  validate(params: unknown): TaskParams {
    if (typeof params !== 'object' || params === null) {
      throw new ToolError('Invalid params: expected object', ToolErrorType.INVALID_PARAMS, 'task')
    }

    const { description, prompt, agentType, sessionId, maxTurns, allowedTools } = params as Record<
      string,
      unknown
    >

    // Validate description
    if (typeof description !== 'string' || !description.trim()) {
      throw new ToolError(
        'Invalid description: must be non-empty string',
        ToolErrorType.INVALID_PARAMS,
        'task'
      )
    }

    // Validate prompt
    if (typeof prompt !== 'string' || !prompt.trim()) {
      throw new ToolError(
        'Invalid prompt: must be non-empty string',
        ToolErrorType.INVALID_PARAMS,
        'task'
      )
    }

    // Validate agentType
    if (typeof agentType !== 'string' || !VALID_AGENT_TYPES.includes(agentType as SubagentType)) {
      throw new ToolError(
        `Invalid agentType: must be one of ${VALID_AGENT_TYPES.join(', ')}`,
        ToolErrorType.INVALID_PARAMS,
        'task'
      )
    }

    // Validate sessionId
    if (sessionId !== undefined && typeof sessionId !== 'string') {
      throw new ToolError('Invalid sessionId: must be string', ToolErrorType.INVALID_PARAMS, 'task')
    }

    // Validate maxTurns
    if (maxTurns !== undefined) {
      if (typeof maxTurns !== 'number' || maxTurns < 1 || maxTurns > MAX_TURNS_LIMIT) {
        throw new ToolError(
          `Invalid maxTurns: must be number between 1 and ${MAX_TURNS_LIMIT}`,
          ToolErrorType.INVALID_PARAMS,
          'task'
        )
      }
    }

    // Validate allowedTools
    if (allowedTools !== undefined) {
      if (!Array.isArray(allowedTools)) {
        throw new ToolError(
          'Invalid allowedTools: must be array',
          ToolErrorType.INVALID_PARAMS,
          'task'
        )
      }
      if (!allowedTools.every((t) => typeof t === 'string')) {
        throw new ToolError(
          'Invalid allowedTools: all items must be strings',
          ToolErrorType.INVALID_PARAMS,
          'task'
        )
      }

      // Custom type requires allowedTools
      if (agentType === 'custom' && allowedTools.length === 0) {
        throw new ToolError(
          'Custom agent type requires at least one allowedTool',
          ToolErrorType.INVALID_PARAMS,
          'task'
        )
      }
    }

    return {
      description: description.trim(),
      prompt: prompt.trim(),
      agentType: agentType as SubagentType,
      sessionId: typeof sessionId === 'string' ? sessionId.trim() : undefined,
      maxTurns: maxTurns as number | undefined,
      allowedTools: allowedTools as string[] | undefined,
    }
  },

  async execute(params: TaskParams, ctx: ToolContext): Promise<ToolResult> {
    // Check abort signal
    if (ctx.signal.aborted) {
      return {
        success: false,
        output: 'Operation was cancelled',
        error: ToolErrorType.EXECUTION_ABORTED,
      }
    }

    // Create subagent manager
    const manager = createSubagentManager((event) => {
      // Forward events via metadata
      if (ctx.metadata && event.type === 'subagent_progress') {
        ctx.metadata({
          title: `[${params.description}] Turn ${event.turn}`,
          metadata: {
            subagentId: event.subagentId,
            turn: event.turn,
            eventType: event.event.type,
          },
        })
      }
    })

    // Create subagent configuration
    const config = manager.createConfig(params.agentType, {
      maxTurns: params.maxTurns,
      allowedTools: params.allowedTools,
      parentSessionId: ctx.sessionId,
    })

    // Register the subagent
    manager.register(config)

    // Create task
    const task: SubagentTask = {
      description: params.description,
      prompt: params.prompt,
      workingDirectory: ctx.workingDirectory,
    }

    // Stream start metadata
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
      // Emit start event
      manager.emit({
        type: 'subagent_started',
        subagentId: config.id,
        task,
      })

      // NOTE: This is a placeholder implementation
      // In a real implementation, this would:
      // 1. Create a new session (or resume existing)
      // 2. Run the agent loop with filtered tools
      // 3. Stream progress events
      // 4. Return final result

      // For now, we return a "not implemented" result that shows the infrastructure works
      const sessionId = params.sessionId ?? generateSubagentSessionId(ctx.sessionId, config.id)

      const result: SubagentResult = {
        subagentId: config.id,
        success: false,
        output: `Subagent infrastructure is ready, but execution requires agent loop integration.

To complete this implementation:
1. Import AgentExecutor from agent/loop.ts
2. Create a new session with filtered tools
3. Run the agent loop with the task prompt
4. Collect and return the result

Subagent Configuration:
- Type: ${params.agentType}
- Allowed Tools: ${config.allowedTools?.join(', ') ?? 'all'}
- Max Turns: ${config.maxTurns}

Task:
${params.prompt}`,
        turns: 0,
        terminationReason: 'error',
        error: 'Subagent execution not yet implemented',
        sessionId,
      }

      // Emit completion event
      manager.emit({
        type: 'subagent_completed',
        subagentId: config.id,
        result,
      })

      // Stream completion metadata
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

      // Format output for LLM
      const output = formatResult(result, config)

      return {
        success: result.success,
        output,
        metadata: {
          subagentId: config.id,
          sessionId: result.sessionId,
          turns: result.turns,
          terminationReason: result.terminationReason,
        },
      }
    } catch (err) {
      // Handle errors
      const errorMessage = err instanceof Error ? err.message : String(err)

      manager.emit({
        type: 'subagent_error',
        subagentId: config.id,
        error: errorMessage,
      })

      return {
        success: false,
        output: `Subagent error: ${errorMessage}`,
        error: ToolErrorType.UNKNOWN,
        metadata: {
          subagentId: config.id,
          error: errorMessage,
        },
      }
    } finally {
      // Unregister subagent
      manager.unregister(config.id)
    }
  },
}
