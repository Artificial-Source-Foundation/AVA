/**
 * Worker Tool Wrapper
 * Wraps worker definitions as callable tools for the commander
 *
 * Based on Gemini CLI's SubagentToolWrapper pattern
 */

import type { Tool, ToolContext, ToolDefinition, ToolResult } from '../tools/types.js'
import { DELEGATE_TOOL_PREFIX, executeWorker } from './executor.js'
import type { WorkerRegistry } from './registry.js'
import type { WorkerActivityCallback, WorkerDefinition, WorkerInputs } from './types.js'

// ============================================================================
// Tool Wrapper Factory
// ============================================================================

/**
 * Create a Tool that wraps a worker definition
 *
 * The resulting tool will be named `delegate_<worker_name>` (e.g., delegate_coder).
 * When invoked, it spawns an isolated AgentExecutor with the worker's configuration.
 *
 * Based on Gemini CLI's SubagentToolWrapper pattern.
 *
 * @param definition - Worker definition to wrap
 * @param onActivity - Optional callback for activity events
 * @returns Tool instance
 */
export function createWorkerTool(
  definition: WorkerDefinition,
  onActivity?: WorkerActivityCallback
): Tool<WorkerToolParams> {
  const toolName = `${DELEGATE_TOOL_PREFIX}${definition.name}`

  return {
    definition: createToolDefinition(definition, toolName),
    execute: createToolExecutor(definition, onActivity),
  }
}

/**
 * Create tools for all workers in a registry
 *
 * @param registry - Worker registry
 * @param onActivity - Optional callback for activity events (shared across all workers)
 * @returns Array of worker tools
 */
export function createAllWorkerTools(
  registry: WorkerRegistry,
  onActivity?: WorkerActivityCallback
): Tool<WorkerToolParams>[] {
  return registry.getAllWorkers().map((definition) => createWorkerTool(definition, onActivity))
}

// ============================================================================
// Tool Parameters
// ============================================================================

/**
 * Parameters for worker delegation tools
 */
export interface WorkerToolParams {
  /** The task to delegate to the worker */
  task: string
  /** Additional context to help the worker understand the task */
  context?: string
}

// ============================================================================
// Internal Helpers
// ============================================================================

/**
 * Create the tool definition for a worker
 */
function createToolDefinition(definition: WorkerDefinition, toolName: string): ToolDefinition {
  return {
    name: toolName,
    description: buildToolDescription(definition),
    input_schema: {
      type: 'object',
      properties: {
        task: {
          type: 'string',
          description: `The specific task to delegate to ${definition.displayName}. Be clear and specific about what you want done.`,
        },
        context: {
          type: 'string',
          description:
            'Optional additional context to help the worker understand the task. Include relevant file paths, requirements, or constraints.',
        },
      },
      required: ['task'],
    },
  }
}

/**
 * Build the tool description from worker definition
 */
function buildToolDescription(definition: WorkerDefinition): string {
  const lines: string[] = [
    `Delegate a task to the ${definition.displayName} worker.`,
    '',
    definition.description,
    '',
    `This worker has access to: ${definition.tools.join(', ')}`,
    '',
    'The worker will execute autonomously and return results when done.',
  ]

  return lines.join('\n')
}

/**
 * Create the tool executor function for a worker
 */
function createToolExecutor(
  definition: WorkerDefinition,
  onActivity?: WorkerActivityCallback
): (params: WorkerToolParams, ctx: ToolContext) => Promise<ToolResult> {
  return async (params: WorkerToolParams, ctx: ToolContext): Promise<ToolResult> => {
    // Build worker inputs
    const inputs: WorkerInputs = {
      task: params.task,
      context: params.context,
      cwd: ctx.workingDirectory,
      parentAgentId: ctx.sessionId,
    }

    // Execute the worker
    const result = await executeWorker(definition, inputs, ctx.signal, onActivity)

    // Format the result
    if (result.success) {
      return {
        success: true,
        output: formatSuccessOutput(definition, result.output, result.turns, result.tokensUsed),
      }
    }

    return {
      success: false,
      output: formatErrorOutput(definition, result.error ?? 'Unknown error'),
      error: result.error,
    }
  }
}

/**
 * Format successful worker output
 */
function formatSuccessOutput(
  definition: WorkerDefinition,
  output: string,
  turns: number,
  tokens: number
): string {
  const lines: string[] = [
    `## ${definition.displayName} Worker Result`,
    '',
    output,
    '',
    '---',
    `*Completed in ${turns} turn(s), ${tokens} tokens*`,
  ]

  return lines.join('\n')
}

/**
 * Format worker error output
 */
function formatErrorOutput(definition: WorkerDefinition, error: string): string {
  return [
    `## ${definition.displayName} Worker Failed`,
    '',
    `Error: ${error}`,
    '',
    'The worker was unable to complete the task. You may need to:',
    '- Try a different approach',
    '- Break the task into smaller pieces',
    '- Handle the task yourself with your available tools',
  ].join('\n')
}

// ============================================================================
// Tool Registration Helpers
// ============================================================================

/**
 * Get all delegate tool names from a registry
 * Useful for filtering these tools from worker access
 */
export function getDelegateToolNames(registry: WorkerRegistry): string[] {
  return registry.getWorkerNames().map((name) => `${DELEGATE_TOOL_PREFIX}${name}`)
}

/**
 * Check if a tool name is a delegate tool from a specific registry
 */
export function isDelegateToolFromRegistry(toolName: string, registry: WorkerRegistry): boolean {
  if (!toolName.startsWith(DELEGATE_TOOL_PREFIX)) {
    return false
  }
  const workerName = toolName.slice(DELEGATE_TOOL_PREFIX.length)
  return registry.has(workerName)
}
