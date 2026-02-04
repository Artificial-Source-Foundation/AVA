/**
 * Batch Tool
 * Execute multiple tools in parallel for reduced API round trips
 *
 * Based on OpenCode's batch tool pattern
 */

import { z } from 'zod'
import { defineTool } from './define.js'
import { executeTool } from './registry.js'
import type { ToolContext, ToolResult } from './types.js'

// ============================================================================
// Constants
// ============================================================================

/** Maximum number of parallel tool calls allowed */
const MAX_PARALLEL_CALLS = 25

/** Tool name for self-reference check */
const BATCH_TOOL_NAME = 'batch'

// ============================================================================
// Schema
// ============================================================================

const ToolCallSchema = z.object({
  tool: z.string().describe('Name of the tool to execute'),
  parameters: z.record(z.string(), z.unknown()).describe('Parameters for the tool'),
})

const BatchSchema = z.object({
  tool_calls: z
    .array(ToolCallSchema)
    .min(1)
    .max(MAX_PARALLEL_CALLS)
    .describe(`Array of tool calls to execute in parallel (1-${MAX_PARALLEL_CALLS})`),
})

type BatchParams = z.infer<typeof BatchSchema>

// ============================================================================
// Types
// ============================================================================

interface IndividualResult {
  tool: string
  success: boolean
  output: string
  error?: string
}

// ============================================================================
// Tool Implementation
// ============================================================================

export const batchTool = defineTool({
  name: BATCH_TOOL_NAME,
  description: `Execute multiple tools in parallel for better efficiency.

Key features:
- Execute up to ${MAX_PARALLEL_CALLS} tool calls in a single request
- Reduces API round trips for multi-file operations
- Results are aggregated and returned together
- Each tool call has its own success/failure status

Usage:
- Use for reading multiple files at once
- Use for running multiple independent searches
- Use for any operations that don't depend on each other

Constraints:
- Maximum ${MAX_PARALLEL_CALLS} parallel calls
- Cannot call 'batch' recursively (no batch inside batch)
- Tool failures don't stop other tools from executing

Example:
\`\`\`json
{
  "tool_calls": [
    { "tool": "read", "parameters": { "path": "src/index.ts" } },
    { "tool": "read", "parameters": { "path": "src/utils.ts" } },
    { "tool": "glob", "parameters": { "pattern": "**/*.test.ts" } }
  ]
}
\`\`\``,

  schema: BatchSchema,

  permissions: ['read', 'write', 'execute'], // Inherits all since it can call any tool

  async execute(params: BatchParams, ctx: ToolContext): Promise<ToolResult> {
    const { tool_calls } = params

    // Validate no recursive batch calls
    const hasRecursiveBatch = tool_calls.some((call) => call.tool === BATCH_TOOL_NAME)

    if (hasRecursiveBatch) {
      return {
        success: false,
        output: `Recursive batch calls are not allowed. The batch tool cannot call itself.`,
        error: 'RECURSIVE_BATCH_BLOCKED',
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

    // Execute all tools in parallel
    const startTime = Date.now()
    const results: IndividualResult[] = []

    const promises = tool_calls.map(async (call): Promise<IndividualResult> => {
      try {
        // Check abort between each tool
        if (ctx.signal.aborted) {
          return {
            tool: call.tool,
            success: false,
            output: 'Operation was cancelled',
            error: 'ABORTED',
          }
        }

        // Execute the tool
        const result = await executeTool(call.tool, call.parameters as Record<string, unknown>, ctx)

        return {
          tool: call.tool,
          success: result.success,
          output: result.output,
          error: result.error,
        }
      } catch (err) {
        const message = err instanceof Error ? err.message : String(err)
        return {
          tool: call.tool,
          success: false,
          output: `Tool execution failed: ${message}`,
          error: 'EXECUTION_ERROR',
        }
      }
    })

    // Wait for all tools to complete
    const settledResults = await Promise.allSettled(promises)

    for (const settled of settledResults) {
      if (settled.status === 'fulfilled') {
        results.push(settled.value)
      } else {
        // This shouldn't happen since we catch errors above, but just in case
        results.push({
          tool: 'unknown',
          success: false,
          output: `Unexpected error: ${settled.reason}`,
          error: 'UNEXPECTED_ERROR',
        })
      }
    }

    const durationMs = Date.now() - startTime

    // Calculate statistics
    const successCount = results.filter((r) => r.success).length
    const failureCount = results.length - successCount
    const allSucceeded = failureCount === 0

    // Format output
    const output = formatBatchOutput(results, successCount, failureCount, durationMs)

    // Stream metadata if available
    if (ctx.metadata) {
      ctx.metadata({
        title: `Batch: ${successCount}/${results.length} succeeded`,
        metadata: {
          totalCalls: results.length,
          successCount,
          failureCount,
          durationMs,
          tools: results.map((r) => r.tool),
        },
      })
    }

    return {
      success: allSucceeded,
      output,
      metadata: {
        totalCalls: results.length,
        successCount,
        failureCount,
        durationMs,
        results: results.map((r) => ({
          tool: r.tool,
          success: r.success,
          error: r.error,
        })),
      },
    }
  },
})

// ============================================================================
// Output Formatting
// ============================================================================

/**
 * Format batch results into readable output
 */
function formatBatchOutput(
  results: IndividualResult[],
  successCount: number,
  failureCount: number,
  durationMs: number
): string {
  const lines: string[] = []

  // Header
  lines.push(`## Batch Execution Results`)
  lines.push(``)
  lines.push(`**Summary:** ${successCount}/${results.length} tools succeeded in ${durationMs}ms`)
  if (failureCount > 0) {
    lines.push(`**Failures:** ${failureCount}`)
  }
  lines.push(``)
  lines.push(`---`)
  lines.push(``)

  // Individual results
  for (let i = 0; i < results.length; i++) {
    const result = results[i]
    const status = result.success ? '✓' : '✗'
    const statusWord = result.success ? 'Success' : 'Failed'

    lines.push(`### [${i + 1}] ${result.tool} - ${status} ${statusWord}`)
    lines.push(``)

    if (result.error) {
      lines.push(`**Error:** ${result.error}`)
      lines.push(``)
    }

    // Truncate very long outputs
    const maxOutputLength = 5000
    let output = result.output
    if (output.length > maxOutputLength) {
      output =
        output.slice(0, maxOutputLength) +
        `\n\n[Output truncated - ${result.output.length} chars total]`
    }

    lines.push(`<output>`)
    lines.push(output)
    lines.push(`</output>`)
    lines.push(``)
  }

  return lines.join('\n')
}
