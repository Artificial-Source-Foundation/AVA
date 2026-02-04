/**
 * Plan Mode
 * Research-only mode that restricts tool usage to read-only operations
 *
 * When enabled, the agent can only:
 * - Read files (read, glob, grep, ls)
 * - Search web (websearch, webfetch)
 * - Complete tasks (attempt_completion)
 *
 * Cannot:
 * - Write, edit, or create files
 * - Execute bash commands
 * - Interact with browser
 */

import { z } from 'zod'
import { defineTool } from '../../tools/define.js'
import type { ToolResult } from '../../tools/types.js'

// ============================================================================
// Types
// ============================================================================

export interface PlanModeState {
  /** Whether plan mode is currently enabled */
  enabled: boolean
  /** When plan mode was entered */
  enteredAt?: Date
  /** Reason for entering plan mode */
  reason?: string
  /** Session ID that entered plan mode */
  sessionId?: string
}

export interface PlanModeConfig {
  /** Custom list of allowed tools (overrides default) */
  allowedTools?: string[]
  /** Whether to allow the agent to exit plan mode itself */
  allowSelfExit?: boolean
}

// ============================================================================
// Constants
// ============================================================================

/**
 * Tools allowed in plan mode (read-only operations)
 */
export const PLAN_MODE_ALLOWED_TOOLS: readonly string[] = [
  // File reading
  'read',
  'glob',
  'grep',
  'ls',
  // Web research
  'websearch',
  'webfetch',
  // Task management (read-only)
  'todo_read',
  // Mode switching
  'plan_exit',
  // Completion
  'attempt_completion',
] as const

/**
 * Tools explicitly blocked in plan mode (writes, executions)
 */
export const PLAN_MODE_BLOCKED_TOOLS: readonly string[] = [
  'write',
  'create',
  'edit',
  'delete',
  'bash',
  'browser',
  'task',
  'todo_write',
  'question', // Prefer focused research over questions
] as const

// ============================================================================
// State Management
// ============================================================================

/** Global plan mode state (per-session) */
const planModeStates = new Map<string, PlanModeState>()

/** Default session ID for single-session scenarios */
const DEFAULT_SESSION = 'default'

/**
 * Get plan mode state for a session
 */
export function getPlanModeState(sessionId: string = DEFAULT_SESSION): PlanModeState {
  return planModeStates.get(sessionId) ?? { enabled: false }
}

/**
 * Set plan mode state for a session
 */
export function setPlanModeState(
  sessionId: string = DEFAULT_SESSION,
  state: Partial<PlanModeState>
): void {
  const current = getPlanModeState(sessionId)
  planModeStates.set(sessionId, { ...current, ...state })
}

/**
 * Check if plan mode is enabled for a session
 */
export function isPlanModeEnabled(sessionId: string = DEFAULT_SESSION): boolean {
  return getPlanModeState(sessionId).enabled
}

/**
 * Enter plan mode
 */
export function enterPlanMode(sessionId: string = DEFAULT_SESSION, reason?: string): void {
  setPlanModeState(sessionId, {
    enabled: true,
    enteredAt: new Date(),
    reason,
    sessionId,
  })
}

/**
 * Exit plan mode
 */
export function exitPlanMode(sessionId: string = DEFAULT_SESSION): void {
  setPlanModeState(sessionId, {
    enabled: false,
    enteredAt: undefined,
    reason: undefined,
  })
}

/**
 * Clear all plan mode states (useful for testing)
 */
export function clearAllPlanModeStates(): void {
  planModeStates.clear()
}

// ============================================================================
// Tool Restriction Logic
// ============================================================================

/**
 * Check if a tool is restricted in plan mode
 */
export function isPlanModeRestricted(
  toolName: string,
  sessionId: string = DEFAULT_SESSION,
  config?: PlanModeConfig
): boolean {
  // If plan mode is not enabled, nothing is restricted
  if (!isPlanModeEnabled(sessionId)) {
    return false
  }

  // Get allowed tools (custom or default)
  const allowedTools = config?.allowedTools ?? PLAN_MODE_ALLOWED_TOOLS

  // Tool is restricted if not in the allowed list
  return !allowedTools.includes(toolName)
}

/**
 * Get the reason why a tool is restricted
 */
export function getRestrictionReason(toolName: string): string {
  if (PLAN_MODE_BLOCKED_TOOLS.includes(toolName)) {
    return `Tool '${toolName}' is blocked in plan mode. Plan mode only allows read-only operations like reading files, searching, and web fetching. Use 'plan_exit' to leave plan mode first.`
  }
  return `Tool '${toolName}' is not in the allowed list for plan mode. Allowed tools: ${PLAN_MODE_ALLOWED_TOOLS.join(', ')}`
}

/**
 * Get human-readable plan mode status
 */
export function getPlanModeStatus(sessionId: string = DEFAULT_SESSION): string {
  const state = getPlanModeState(sessionId)

  if (!state.enabled) {
    return 'Plan mode is not active.'
  }

  const parts = ['Plan mode is ACTIVE.']

  if (state.enteredAt) {
    const duration = Date.now() - state.enteredAt.getTime()
    const minutes = Math.floor(duration / 60000)
    parts.push(`Active for ${minutes} minute(s).`)
  }

  if (state.reason) {
    parts.push(`Reason: ${state.reason}`)
  }

  parts.push(`\nAllowed tools: ${PLAN_MODE_ALLOWED_TOOLS.join(', ')}`)

  return parts.join(' ')
}

// ============================================================================
// Plan Mode Tools
// ============================================================================

/**
 * Tool to enter plan mode
 */
export const planEnterTool = defineTool({
  name: 'plan_enter',
  description: `Enter plan mode for focused research and analysis.

In plan mode, you can only use read-only tools:
- File reading: read, glob, grep, ls
- Web research: websearch, webfetch
- Task viewing: todo_read

This prevents accidental modifications while exploring the codebase.
Use 'plan_exit' when ready to make changes.

## When to use
- Investigating bugs before fixing
- Understanding codebase structure
- Researching solutions before implementing
- Reviewing code without modifying`,

  schema: z.object({
    reason: z
      .string()
      .optional()
      .describe('Why you are entering plan mode (e.g., "investigating auth bug")'),
  }),

  permissions: [],

  execute: async (params: { reason?: string }, ctx): Promise<ToolResult> => {
    const sessionId = ctx.sessionId ?? DEFAULT_SESSION

    // Check if already in plan mode
    if (isPlanModeEnabled(sessionId)) {
      return {
        success: false,
        output: 'Already in plan mode. Use plan_exit to leave first.',
        error: 'ALREADY_IN_PLAN_MODE',
      }
    }

    // Enter plan mode
    enterPlanMode(sessionId, params.reason)

    const parts = ['Entered plan mode.']
    if (params.reason) {
      parts.push(`\nReason: ${params.reason}`)
    }
    parts.push(`\n\nYou can now only use these tools:\n- ${PLAN_MODE_ALLOWED_TOOLS.join('\n- ')}`)
    parts.push('\n\nUse plan_exit when ready to make changes.')

    return {
      success: true,
      output: parts.join(''),
      metadata: {
        planModeEnabled: true,
        reason: params.reason,
        allowedTools: [...PLAN_MODE_ALLOWED_TOOLS],
      },
    }
  },
})

/**
 * Tool to exit plan mode
 */
export const planExitTool = defineTool({
  name: 'plan_exit',
  description: `Exit plan mode to resume normal operations.

After exiting, all tools become available again including:
- File modifications: write, edit, create, delete
- Command execution: bash
- Browser interaction: browser

## When to use
- Research is complete
- Ready to implement changes
- Need to execute a command`,

  schema: z.object({
    summary: z.string().optional().describe('Summary of what you learned/planned during plan mode'),
  }),

  permissions: [],

  execute: async (params: { summary?: string }, ctx): Promise<ToolResult> => {
    const sessionId = ctx.sessionId ?? DEFAULT_SESSION

    // Check if not in plan mode
    if (!isPlanModeEnabled(sessionId)) {
      return {
        success: false,
        output: 'Not currently in plan mode.',
        error: 'NOT_IN_PLAN_MODE',
      }
    }

    // Get state before exiting for duration info
    const state = getPlanModeState(sessionId)
    const duration = state.enteredAt ? Date.now() - state.enteredAt.getTime() : 0
    const minutes = Math.floor(duration / 60000)

    // Exit plan mode
    exitPlanMode(sessionId)

    const parts = ['Exited plan mode.']
    parts.push(`\nDuration: ${minutes} minute(s)`)

    if (params.summary) {
      parts.push(`\nSummary: ${params.summary}`)
    }

    parts.push('\n\nAll tools are now available.')

    return {
      success: true,
      output: parts.join(''),
      metadata: {
        planModeEnabled: false,
        duration,
        summary: params.summary,
      },
    }
  },
})

// ============================================================================
// Registry Integration Helper
// ============================================================================

/**
 * Check tool access and return error if restricted
 * Call this from registry.executeTool() before executing
 */
export function checkPlanModeAccess(
  toolName: string,
  sessionId?: string
): { allowed: boolean; error?: ToolResult } {
  if (!isPlanModeRestricted(toolName, sessionId)) {
    return { allowed: true }
  }

  return {
    allowed: false,
    error: {
      success: false,
      output: getRestrictionReason(toolName),
      error: 'PLAN_MODE_RESTRICTED',
    },
  }
}
