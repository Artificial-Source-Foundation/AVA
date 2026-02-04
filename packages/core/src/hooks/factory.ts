/**
 * Hook Factory
 * Helper functions for creating hook contexts and parsing hook output
 */

import type { ToolResult } from '../tools/types.js'
import type {
  HookResult,
  HookType,
  PostToolUseContext,
  PreToolUseContext,
  TaskCancelContext,
  TaskCompleteContext,
  TaskStartContext,
} from './types.js'

// ============================================================================
// Context Factory Functions
// ============================================================================

/**
 * Create a PreToolUse hook context
 */
export function createPreToolUseContext(params: {
  toolName: string
  parameters: Record<string, unknown>
  workingDirectory: string
  sessionId: string
}): PreToolUseContext {
  return {
    toolName: params.toolName,
    parameters: params.parameters,
    workingDirectory: params.workingDirectory,
    sessionId: params.sessionId,
  }
}

/**
 * Create a PostToolUse hook context
 */
export function createPostToolUseContext(params: {
  toolName: string
  parameters: Record<string, unknown>
  result: ToolResult
  workingDirectory: string
  sessionId: string
  durationMs: number
}): PostToolUseContext {
  return {
    toolName: params.toolName,
    parameters: params.parameters,
    result: params.result,
    success: params.result.success,
    workingDirectory: params.workingDirectory,
    sessionId: params.sessionId,
    durationMs: params.durationMs,
  }
}

/**
 * Create a TaskStart hook context
 */
export function createTaskStartContext(params: {
  goal: string
  sessionId: string
  workingDirectory: string
}): TaskStartContext {
  return {
    goal: params.goal,
    sessionId: params.sessionId,
    workingDirectory: params.workingDirectory,
  }
}

/**
 * Create a TaskComplete hook context
 */
export function createTaskCompleteContext(params: {
  success: boolean
  output: string
  command?: string
  sessionId: string
  workingDirectory: string
  durationMs: number
  toolCallCount: number
}): TaskCompleteContext {
  return {
    success: params.success,
    output: params.output,
    command: params.command,
    sessionId: params.sessionId,
    workingDirectory: params.workingDirectory,
    durationMs: params.durationMs,
    toolCallCount: params.toolCallCount,
  }
}

/**
 * Create a TaskCancel hook context
 */
export function createTaskCancelContext(params: {
  reason: string
  sessionId: string
  workingDirectory: string
  durationMs: number
}): TaskCancelContext {
  return {
    reason: params.reason,
    sessionId: params.sessionId,
    workingDirectory: params.workingDirectory,
    durationMs: params.durationMs,
  }
}

// ============================================================================
// Output Parsing
// ============================================================================

/**
 * Parse hook script output (JSON from stdout)
 *
 * @param output - Raw string output from hook script
 * @param hookType - The type of hook that ran (for validation)
 * @returns Parsed and validated HookResult
 * @throws Error if output is invalid
 */
export function parseHookOutput(output: string, hookType: HookType): HookResult {
  // Empty output is valid - means no modifications
  if (!output || output.trim() === '') {
    return {}
  }

  // Try to parse as JSON
  let parsed: unknown
  try {
    parsed = JSON.parse(output.trim())
  } catch {
    throw new Error(
      `Invalid hook output: expected JSON, got: ${output.slice(0, 100)}${output.length > 100 ? '...' : ''}`
    )
  }

  // Validate it's an object
  if (typeof parsed !== 'object' || parsed === null || Array.isArray(parsed)) {
    throw new Error('Invalid hook output: expected JSON object')
  }

  const result = parsed as Record<string, unknown>

  // Validate and extract fields
  const hookResult: HookResult = {}

  // cancel is only valid for PreToolUse
  if ('cancel' in result) {
    if (hookType !== 'PreToolUse') {
      console.warn(
        `Hook output 'cancel' field ignored for ${hookType} hook (only valid for PreToolUse)`
      )
    } else if (typeof result.cancel === 'boolean') {
      hookResult.cancel = result.cancel
    } else {
      throw new Error("Invalid hook output: 'cancel' must be a boolean")
    }
  }

  // contextModification is valid for all hooks
  if ('contextModification' in result) {
    if (typeof result.contextModification === 'string') {
      hookResult.contextModification = result.contextModification
    } else {
      throw new Error("Invalid hook output: 'contextModification' must be a string")
    }
  }

  // errorMessage is typically used with cancel
  if ('errorMessage' in result) {
    if (typeof result.errorMessage === 'string') {
      hookResult.errorMessage = result.errorMessage
    } else {
      throw new Error("Invalid hook output: 'errorMessage' must be a string")
    }
  }

  return hookResult
}

/**
 * Validate a HookResult object
 *
 * @param result - The result to validate
 * @param hookType - The hook type (for context-specific validation)
 * @returns True if valid
 * @throws Error if invalid
 */
export function validateHookResult(result: HookResult, hookType: HookType): boolean {
  // cancel only makes sense for PreToolUse
  if (result.cancel && hookType !== 'PreToolUse') {
    console.warn(`Hook result 'cancel' ignored for ${hookType} hook`)
    result.cancel = undefined
  }

  // If canceling, should have an error message
  if (result.cancel && !result.errorMessage) {
    result.errorMessage = 'Operation cancelled by hook'
  }

  return true
}

// ============================================================================
// Context Serialization
// ============================================================================

/**
 * Serialize hook context to JSON for stdin
 * Handles special cases like BigInt, circular references, etc.
 */
export function serializeContext(context: Record<string, unknown>): string {
  return JSON.stringify(context, (_key, value) => {
    // Handle BigInt
    if (typeof value === 'bigint') {
      return value.toString()
    }

    // Handle undefined (convert to null for JSON)
    if (value === undefined) {
      return null
    }

    // Handle Error objects
    if (value instanceof Error) {
      return {
        name: value.name,
        message: value.message,
        stack: value.stack,
      }
    }

    // Handle functions (skip them)
    if (typeof value === 'function') {
      return undefined
    }

    return value
  })
}

// ============================================================================
// Error Helpers
// ============================================================================

/**
 * Create a HookResult representing an error
 */
export function createErrorResult(error: Error | string): HookResult {
  const message = typeof error === 'string' ? error : error.message
  return {
    cancel: false,
    errorMessage: message,
  }
}

/**
 * Create a HookResult that cancels the operation
 */
export function createCancelResult(reason: string): HookResult {
  return {
    cancel: true,
    errorMessage: reason,
  }
}

/**
 * Merge multiple hook results (later results override earlier)
 */
export function mergeHookResults(results: HookResult[]): HookResult {
  const merged: HookResult = {}

  for (const result of results) {
    // cancel is OR'd - any cancellation cancels
    if (result.cancel) {
      merged.cancel = true
      merged.errorMessage = result.errorMessage || merged.errorMessage
    }

    // contextModification is concatenated
    if (result.contextModification) {
      merged.contextModification = merged.contextModification
        ? `${merged.contextModification}\n\n${result.contextModification}`
        : result.contextModification
    }

    // errorMessage takes the last one if not from cancel
    if (result.errorMessage && !result.cancel) {
      merged.errorMessage = result.errorMessage
    }
  }

  return merged
}
