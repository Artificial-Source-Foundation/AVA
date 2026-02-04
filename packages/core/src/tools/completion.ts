/**
 * Completion Tool
 * Signals task completion with a result summary
 *
 * Based on Cline's attempt_completion pattern:
 * - LLM calls this when task is complete
 * - Provides clear summary of what was accomplished
 * - Optional command to demonstrate the result
 * - Triggers TaskComplete hook for custom actions
 */

import { z } from 'zod'
import { createTaskCompleteContext, getHookRunner } from '../hooks/index.js'
import { defineTool } from './define.js'

// ============================================================================
// Schema
// ============================================================================

const CompletionSchema = z.object({
  result: z
    .string()
    .min(1)
    .describe(
      'Clear, concise summary of what was accomplished (1-2 paragraphs). Include: what was done, any important details, and any follow-up recommendations.'
    ),
  command: z
    .string()
    .optional()
    .describe(
      'Optional CLI command to demonstrate or test the result. Examples: "npm run dev", "open index.html", "python main.py"'
    ),
})

type CompletionParams = z.infer<typeof CompletionSchema>

// ============================================================================
// State Tracking
// ============================================================================

/**
 * Track completion attempts per session
 * Used by agent loop to recognize when LLM signals completion
 */
const completionState = new Map<
  string,
  {
    attempted: boolean
    result?: string
    command?: string
    timestamp: number
  }
>()

/**
 * Check if completion was attempted for a session
 */
export function wasCompletionAttempted(sessionId: string): boolean {
  return completionState.get(sessionId)?.attempted ?? false
}

/**
 * Get completion details for a session
 */
export function getCompletionDetails(
  sessionId: string
): { result: string; command?: string } | null {
  const state = completionState.get(sessionId)
  if (!state?.attempted || !state.result) return null
  return { result: state.result, command: state.command }
}

/**
 * Reset completion state for a session
 */
export function resetCompletionState(sessionId: string): void {
  completionState.delete(sessionId)
}

/**
 * Clean up old completion states (>1 hour old)
 */
export function cleanupCompletionStates(): void {
  const oneHourAgo = Date.now() - 60 * 60 * 1000
  for (const [sessionId, state] of Array.from(completionState.entries())) {
    if (state.timestamp < oneHourAgo) {
      completionState.delete(sessionId)
    }
  }
}

// ============================================================================
// Tool Definition
// ============================================================================

export const completionTool = defineTool({
  name: 'attempt_completion',
  description: `Signal that the task is complete and present your final result to the user.

IMPORTANT: Only use this tool when:
1. All requested changes have been made successfully
2. All tool operations completed without errors
3. No pending operations remain
4. You have verified the result (ran tests, checked output, etc.)

Do NOT use this tool if:
- Any tool call failed
- You need to make more changes
- You are uncertain about the result
- The user might need to provide feedback for improvements

The result should be a clear, concise summary (1-2 paragraphs) of what was accomplished.
Optionally include a command the user can run to see/test the result.`,

  schema: CompletionSchema,

  permissions: [], // No file permissions needed

  execute: async (params: CompletionParams, ctx) => {
    const { result, command } = params

    // Record completion attempt
    completionState.set(ctx.sessionId, {
      attempted: true,
      result,
      command,
      timestamp: Date.now(),
    })

    // Run TaskComplete hook
    // Note: The hook receives basic info here; the full context
    // (duration, tool count) is added by the agent loop
    try {
      const hookRunner = getHookRunner(ctx.workingDirectory)
      await hookRunner.run(
        'TaskComplete',
        createTaskCompleteContext({
          success: true,
          output: result,
          command,
          sessionId: ctx.sessionId,
          workingDirectory: ctx.workingDirectory,
          durationMs: 0, // Agent loop will provide actual duration
          toolCallCount: 0, // Agent loop will provide actual count
        })
      )
    } catch {
      // Don't fail if hook errors
    }

    // Build response
    let output = `Task completion signaled.\n\n**Result:**\n${result}`

    if (command) {
      output += `\n\n**Demo command:**\n\`\`\`\n${command}\n\`\`\``
    }

    output += '\n\nThe user may provide feedback for improvements, or accept the result.'

    return {
      success: true,
      output,
      metadata: {
        completionAttempted: true,
        hasCommand: !!command,
      },
    }
  },
})

// ============================================================================
// Helper Functions
// ============================================================================

/**
 * Format a completion result for display
 */
export function formatCompletionResult(result: string, command?: string): string {
  let formatted = `## Task Completed\n\n${result}`

  if (command) {
    formatted += `\n\n### Try it out\n\`\`\`bash\n${command}\n\`\`\``
  }

  return formatted
}

/**
 * Check if a tool call is an attempt_completion call
 */
export function isCompletionCall(toolName: string): boolean {
  return toolName === 'attempt_completion'
}
