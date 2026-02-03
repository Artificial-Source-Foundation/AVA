/**
 * Tool Registry
 * Manages tool registration and execution
 */

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
function getTool(name: string): Tool | undefined {
  return tools.get(name)
}

/**
 * Get all registered tools
 */
function getAllTools(): Tool[] {
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
 * Execute a tool by name
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

  try {
    // Validate params if validator provided
    const validatedParams = tool.validate ? tool.validate(params) : params

    // Execute tool
    const result = await tool.execute(validatedParams, ctx)

    return result
  } catch (err) {
    const toolError = ToolError.from(err, name)

    return {
      success: false,
      output: toolError.message,
      error: toolError.type,
    }
  }
}
