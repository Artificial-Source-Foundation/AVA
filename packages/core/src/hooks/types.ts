/**
 * Hook System Types
 * Type definitions for tool lifecycle hooks
 *
 * Hooks allow operators to customize tool behavior:
 * - PreToolUse: Run before tool execution (can cancel or modify)
 * - PostToolUse: Run after tool execution (can add context)
 * - TaskStart: Run when agent begins a task
 * - TaskComplete: Run when agent signals completion
 * - TaskCancel: Run when task is cancelled
 */

import type { ToolResult } from '../tools/types.js'

// ============================================================================
// Hook Types
// ============================================================================

/**
 * Available hook types
 */
export type HookType = 'PreToolUse' | 'PostToolUse' | 'TaskStart' | 'TaskComplete' | 'TaskCancel'

/**
 * Hook execution result
 */
export interface HookResult {
  /**
   * If true, cancel the tool execution (PreToolUse only)
   * The tool will not run and an error will be returned
   */
  cancel?: boolean

  /**
   * Message to inject into the conversation context
   * Useful for adding reminders, lint results, etc.
   */
  contextModification?: string

  /**
   * Error message to display if cancel is true
   */
  errorMessage?: string
}

// ============================================================================
// Hook Context Types - Input to hooks
// ============================================================================

/**
 * Context provided to PreToolUse hooks
 */
export interface PreToolUseContext {
  /** Name of the tool being called */
  toolName: string

  /** Parameters passed to the tool */
  parameters: Record<string, unknown>

  /** Working directory for the operation */
  workingDirectory: string

  /** Session identifier */
  sessionId: string
}

/**
 * Context provided to PostToolUse hooks
 */
export interface PostToolUseContext {
  /** Name of the tool that was called */
  toolName: string

  /** Parameters that were passed to the tool */
  parameters: Record<string, unknown>

  /** Result from the tool execution */
  result: ToolResult

  /** Whether the tool succeeded */
  success: boolean

  /** Working directory for the operation */
  workingDirectory: string

  /** Session identifier */
  sessionId: string

  /** Execution time in milliseconds */
  durationMs: number
}

/**
 * Context provided to TaskStart hooks
 */
export interface TaskStartContext {
  /** The task goal/description */
  goal: string

  /** Session identifier */
  sessionId: string

  /** Working directory */
  workingDirectory: string
}

/**
 * Context provided to TaskComplete hooks
 */
export interface TaskCompleteContext {
  /** Whether the task completed successfully */
  success: boolean

  /** Result summary from attempt_completion */
  output: string

  /** Optional demo command provided */
  command?: string

  /** Session identifier */
  sessionId: string

  /** Working directory */
  workingDirectory: string

  /** Total task duration in milliseconds */
  durationMs: number

  /** Number of tool calls made */
  toolCallCount: number
}

/**
 * Context provided to TaskCancel hooks
 */
export interface TaskCancelContext {
  /** Reason for cancellation */
  reason: string

  /** Session identifier */
  sessionId: string

  /** Working directory */
  workingDirectory: string

  /** Duration before cancellation in milliseconds */
  durationMs: number
}

/**
 * Union of all hook context types
 */
export type HookContext =
  | PreToolUseContext
  | PostToolUseContext
  | TaskStartContext
  | TaskCompleteContext
  | TaskCancelContext

/**
 * Map from hook type to its context type
 */
export interface HookContextMap {
  PreToolUse: PreToolUseContext
  PostToolUse: PostToolUseContext
  TaskStart: TaskStartContext
  TaskComplete: TaskCompleteContext
  TaskCancel: TaskCancelContext
}

// ============================================================================
// Hook Configuration
// ============================================================================

/**
 * Hook discovery location
 */
export interface HookLocation {
  /** Absolute path to the hook script */
  path: string

  /** Source of the hook (global or project) */
  source: 'global' | 'project'

  /** Hook type this script handles */
  type: HookType
}

/**
 * Configuration for hook execution
 */
export interface HookConfig {
  /** Timeout in milliseconds (default: 30000) */
  timeout?: number

  /** Whether to continue if hook fails (default: true for non-canceling hooks) */
  continueOnError?: boolean

  /** Working directory for hook execution */
  workingDirectory?: string
}

/**
 * Default hook configuration
 */
export const DEFAULT_HOOK_CONFIG: Required<HookConfig> = {
  timeout: 30_000,
  continueOnError: true,
  workingDirectory: process.cwd(),
}

// ============================================================================
// Hook Events
// ============================================================================

/**
 * Event types for hook system
 */
export type HookEventType =
  | 'hook:discovered'
  | 'hook:executing'
  | 'hook:completed'
  | 'hook:failed'
  | 'hook:timeout'
  | 'hook:cancelled'

/**
 * Hook event payload
 */
export interface HookEvent {
  type: HookEventType
  hookType: HookType
  hookPath?: string
  result?: HookResult
  error?: Error
  durationMs?: number
}

/**
 * Hook event listener
 */
export type HookEventListener = (event: HookEvent) => void
