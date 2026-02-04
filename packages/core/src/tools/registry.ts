/**
 * Tool Registry
 * Manages tool registration and execution with hook support
 */

import { checkPlanModeAccess } from '../agent/modes/index.js'
import { createPostToolUseContext, createPreToolUseContext, getHookRunner } from '../hooks/index.js'
import { ToolError } from './errors.js'
import type { Tool, ToolContext, ToolDefinition, ToolResult } from './types.js'

// Use a more flexible type for the registry - tools have different param types
// biome-ignore lint/suspicious/noExplicitAny: Registry must store tools with varying param types
type AnyTool = Tool<any> // eslint-disable-line @typescript-eslint/no-explicit-any

// ============================================================================
// Registry
// ============================================================================

/** Internal tool registry */
const tools = new Map<string, AnyTool>()

/**
 * Register a tool
 */
export function registerTool(tool: AnyTool): void {
  const name = tool.definition.name
  if (tools.has(name)) {
    console.warn(`Tool "${name}" already registered, overwriting`)
  }
  tools.set(name, tool)
}

/**
 * Get a tool by name
 */
export function getTool(name: string): Tool | undefined {
  return tools.get(name)
}

/**
 * Get all registered tools
 */
export function getAllTools(): Tool[] {
  return Array.from(tools.values())
}

/**
 * Get all tool definitions (for sending to LLM)
 */
export function getToolDefinitions(): ToolDefinition[] {
  return getAllTools().map((t) => t.definition)
}

// ============================================================================
// Execution
// ============================================================================

/** Maximum tool calls per turn to prevent infinite loops */
const MAX_TOOL_CALLS = 10

/** Track tool calls in current turn */
let toolCallCount = 0

/**
 * Reset tool call counter (call at start of each message turn)
 */
export function resetToolCallCount(): void {
  toolCallCount = 0
}

/**
 * Get current tool call count
 */
export function getToolCallCount(): number {
  return toolCallCount
}

/**
 * Execute a tool by name with hook support
 *
 * Hook execution flow:
 * 1. PreToolUse hook runs (can cancel tool execution)
 * 2. Tool executes
 * 3. PostToolUse hook runs (can add context modification)
 *
 * @param name - Tool name
 * @param params - Tool parameters
 * @param ctx - Tool context
 * @returns Tool result, potentially with hook context modifications
 */
export async function executeTool(
  name: string,
  params: Record<string, unknown>,
  ctx: ToolContext
): Promise<ToolResult> {
  // Check tool call limit
  if (toolCallCount >= MAX_TOOL_CALLS) {
    return {
      success: false,
      output: `Tool call limit (${MAX_TOOL_CALLS}) reached. Please complete your response.`,
      error: 'TOOL_CALL_LIMIT_REACHED',
    }
  }
  toolCallCount++

  // Check plan mode restrictions
  const planModeCheck = checkPlanModeAccess(name, ctx.sessionId)
  if (!planModeCheck.allowed) {
    return planModeCheck.error!
  }

  // Get tool
  const tool = getTool(name)
  if (!tool) {
    return {
      success: false,
      output: `Unknown tool: ${name}`,
      error: `Tool "${name}" not found in registry`,
    }
  }

  // Check abort signal
  if (ctx.signal.aborted) {
    return {
      success: false,
      output: 'Operation was cancelled',
      error: 'ABORTED',
    }
  }

  const startTime = Date.now()
  let hookContextModification: string | undefined

  try {
    // ========================================================================
    // PreToolUse Hook
    // ========================================================================
    const hookRunner = getHookRunner(ctx.workingDirectory)
    const preHookResult = await hookRunner.run(
      'PreToolUse',
      createPreToolUseContext({
        toolName: name,
        parameters: params,
        workingDirectory: ctx.workingDirectory,
        sessionId: ctx.sessionId,
      })
    )

    // Handle hook cancellation
    if (preHookResult.cancel) {
      return {
        success: false,
        output: preHookResult.errorMessage || 'Operation cancelled by hook',
        error: 'HOOK_CANCELLED',
      }
    }

    // Collect context modification from PreToolUse
    if (preHookResult.contextModification) {
      hookContextModification = preHookResult.contextModification
    }

    // ========================================================================
    // Tool Execution
    // ========================================================================
    // Validate params if validator provided
    const validatedParams = tool.validate ? tool.validate(params) : params

    // Execute tool
    const result = await tool.execute(validatedParams, ctx)
    const durationMs = Date.now() - startTime

    // ========================================================================
    // PostToolUse Hook
    // ========================================================================
    const postHookResult = await hookRunner.run(
      'PostToolUse',
      createPostToolUseContext({
        toolName: name,
        parameters: params,
        result,
        workingDirectory: ctx.workingDirectory,
        sessionId: ctx.sessionId,
        durationMs,
      })
    )

    // Collect context modification from PostToolUse
    if (postHookResult.contextModification) {
      hookContextModification = hookContextModification
        ? `${hookContextModification}\n\n${postHookResult.contextModification}`
        : postHookResult.contextModification
    }

    // Return result with hook context modifications
    return addHookContext(result, hookContextModification)
  } catch (err) {
    const toolError = ToolError.from(err, name)
    const durationMs = Date.now() - startTime

    // Run PostToolUse hook even on error (for logging, etc.)
    try {
      const hookRunner = getHookRunner(ctx.workingDirectory)
      const postHookResult = await hookRunner.run(
        'PostToolUse',
        createPostToolUseContext({
          toolName: name,
          parameters: params,
          result: {
            success: false,
            output: toolError.message,
            error: toolError.type,
          },
          workingDirectory: ctx.workingDirectory,
          sessionId: ctx.sessionId,
          durationMs,
        })
      )

      if (postHookResult.contextModification) {
        hookContextModification = hookContextModification
          ? `${hookContextModification}\n\n${postHookResult.contextModification}`
          : postHookResult.contextModification
      }
    } catch {
      // Ignore hook errors on error path
    }

    return addHookContext(
      {
        success: false,
        output: toolError.message,
        error: toolError.type,
      },
      hookContextModification
    )
  }
}

/**
 * Add hook context modification to tool result
 */
function addHookContext(result: ToolResult, hookContext: string | undefined): ToolResult {
  if (!hookContext) {
    return result
  }

  // Add hook context to output
  const output = result.output
    ? `${result.output}\n\n---\n**Hook Context:**\n${hookContext}`
    : hookContext

  return {
    ...result,
    output,
    metadata: {
      ...result.metadata,
      hookContext,
    },
  }
}

// ============================================================================
// Hook-Aware Tool Execution (Advanced)
// ============================================================================

/**
 * Options for executing a tool with custom hook behavior
 */
export interface ExecuteToolOptions {
  /** Skip PreToolUse hook */
  skipPreHook?: boolean
  /** Skip PostToolUse hook */
  skipPostHook?: boolean
  /** Custom hook runner (for testing) */
  hookRunner?: ReturnType<typeof getHookRunner>
}

/**
 * Execute a tool with custom hook options
 *
 * @param name - Tool name
 * @param params - Tool parameters
 * @param ctx - Tool context
 * @param options - Hook options
 * @returns Tool result
 */
export async function executeToolWithOptions(
  name: string,
  params: Record<string, unknown>,
  ctx: ToolContext,
  options: ExecuteToolOptions = {}
): Promise<ToolResult> {
  // If skipping both hooks, use fast path
  if (options.skipPreHook && options.skipPostHook) {
    return executeToolDirect(name, params, ctx)
  }

  // Otherwise use standard execution with hooks
  return executeTool(name, params, ctx)
}

/**
 * Execute a tool directly without hooks (internal use)
 */
async function executeToolDirect(
  name: string,
  params: Record<string, unknown>,
  ctx: ToolContext
): Promise<ToolResult> {
  const tool = getTool(name)
  if (!tool) {
    return {
      success: false,
      output: `Unknown tool: ${name}`,
      error: `Tool "${name}" not found in registry`,
    }
  }

  try {
    const validatedParams = tool.validate ? tool.validate(params) : params
    return await tool.execute(validatedParams, ctx)
  } catch (err) {
    const toolError = ToolError.from(err, name)
    return {
      success: false,
      output: toolError.message,
      error: toolError.type,
    }
  }
}
