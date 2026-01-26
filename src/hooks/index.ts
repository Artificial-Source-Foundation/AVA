/**
 * Delta9 Hooks Module
 *
 * Exports all hook factories for plugin integration.
 *
 * Hook Categories:
 * - Session: Lifecycle events (start, end, connect, disconnect)
 * - Tool Output: Before/after tool execution
 * - Recovery: Edit error recovery with automatic retry
 * - Message: Before/after message processing with context injection
 * - Truncation: Smart output truncation with per-tool limits
 * - Compaction: Context compaction with state preservation
 */

export {
  createSessionEventHandler,
  getSessionState,
  trackDispatchedTask,
  trackBackgroundTask,
  getActiveSessions,
  clearAllSessionState,
  type SessionHooksInput,
  type SessionEventHandler,
  type EventInput,
} from './session.js'

export {
  createToolOutputHooks,
  getChangedFiles,
  clearChangedFiles,
  type ToolOutputHooks,
  type ToolOutputHooksInput,
  type ToolExecuteBeforeInput,
  type ToolExecuteBeforeOutput,
  type ToolExecuteAfterInput,
  type ToolExecuteAfterOutput,
} from './tool-output.js'

export {
  createRecoveryHooks,
  getRecoveryHooks,
  type RecoveryHooksInput,
  type RecoveryHooks,
} from './recovery.js'

export {
  createMessageHooks,
  getMessageStats,
  clearMessageStats,
  type MessageHooksInput,
  type MessageHooks,
  type MessagePart,
  type MessageBeforeInput,
  type MessageBeforeOutput,
  type MessageAfterInput,
  type MessageAfterOutput,
} from './message.js'

export {
  createTruncationHooks,
  truncateOutput,
  getTruncationStats,
  clearTruncationStats,
  type TruncationHooksInput,
  type TruncationConfig,
  type TruncationInput,
  type TruncationOutput,
} from './truncation.js'

export {
  createCompactionHooks,
  getCompactionHistory,
  clearCompactionHistory,
  type CompactionHooksInput,
  type CompactionHooks,
  type PreCompactInput,
  type PreCompactOutput,
  type PostCompactInput,
  type PostCompactOutput,
} from './compaction.js'

// =============================================================================
// Combined Hook Factory
// =============================================================================

import type { MissionState } from '../mission/state.js'
import { createSessionEventHandler, type SessionEventHandler } from './session.js'
import {
  createToolOutputHooks,
  type ToolOutputHooks,
  type ToolExecuteAfterInput,
  type ToolExecuteAfterOutput,
} from './tool-output.js'
import { createRecoveryHooks } from './recovery.js'
import {
  createMessageHooks,
  type MessageHooks,
  type MessageBeforeInput,
  type MessageBeforeOutput,
  type MessageAfterInput,
  type MessageAfterOutput,
} from './message.js'
import { createTruncationHooks } from './truncation.js'
import {
  createCompactionHooks,
  type CompactionHooks,
  type PreCompactInput,
  type PreCompactOutput,
  type PostCompactInput,
  type PostCompactOutput,
} from './compaction.js'

export interface CreateHooksInput {
  /** Mission state instance */
  state: MissionState
  /** Project root directory */
  cwd: string
  /** Logger function */
  log: (level: string, message: string, data?: Record<string, unknown>) => void
}

export interface Delta9Hooks extends ToolOutputHooks {
  /** Event handler for session lifecycle events */
  event: SessionEventHandler
  /** Message before hook */
  'message.before': MessageHooks['message.before']
  /** Message after hook */
  'message.after': MessageHooks['message.after']
  /** Compact before hook */
  'compact.before': CompactionHooks['compact.before']
  /** Compact after hook */
  'compact.after': CompactionHooks['compact.after']
}

/**
 * Create all Delta9 hooks
 *
 * Combines:
 * - Session lifecycle events (via event handler)
 * - Tool output hooks (before/after tool execution)
 * - Recovery hooks (edit error recovery)
 * - Message hooks (context injection)
 * - Truncation hooks (output truncation)
 * - Compaction hooks (state preservation)
 *
 * Hook composition merges handlers that target the same hook point.
 */
export function createDelta9Hooks(input: CreateHooksInput): Delta9Hooks {
  const { state, cwd, log } = input

  // Create individual hook sets
  const sessionEventHandler = createSessionEventHandler(input)
  const toolOutputHooks = createToolOutputHooks(input)
  const recoveryHooks = createRecoveryHooks({ state, cwd })
  const messageHooks = createMessageHooks({ state, cwd, log })
  const truncationHooks = createTruncationHooks({ state, log })
  const compactionHooks = createCompactionHooks({ state, cwd, log })

  // Merge tool.execute.after handlers from multiple sources
  const originalAfterHook = toolOutputHooks['tool.execute.after']
  const recoveryAfterHook = recoveryHooks['tool.execute.after']
  const truncationAfterHook = truncationHooks['tool.execute.after']

  const mergedToolAfterHook = async (
    hookInput: ToolExecuteAfterInput,
    hookOutput: ToolExecuteAfterOutput
  ): Promise<void> => {
    // Run truncation first (modifies output), then others in parallel
    await truncationAfterHook(hookInput, hookOutput)
    await Promise.all([
      originalAfterHook(hookInput, hookOutput),
      recoveryAfterHook(hookInput, hookOutput),
    ])
  }

  // Merge message.before handlers
  const messageBeforeHook = messageHooks['message.before']
  const mergedMessageBeforeHook = async (
    hookInput: MessageBeforeInput,
    hookOutput: MessageBeforeOutput
  ): Promise<void> => {
    await messageBeforeHook(hookInput, hookOutput)
  }

  // Merge message.after handlers
  const messageAfterHook = messageHooks['message.after']
  const mergedMessageAfterHook = async (
    hookInput: MessageAfterInput,
    hookOutput: MessageAfterOutput
  ): Promise<void> => {
    await messageAfterHook(hookInput, hookOutput)
  }

  // Merge compact.before handlers
  const compactBeforeHook = compactionHooks['compact.before']
  const mergedCompactBeforeHook = async (
    hookInput: PreCompactInput,
    hookOutput: PreCompactOutput
  ): Promise<void> => {
    await compactBeforeHook(hookInput, hookOutput)
  }

  // Merge compact.after handlers
  const compactAfterHook = compactionHooks['compact.after']
  const mergedCompactAfterHook = async (
    hookInput: PostCompactInput,
    hookOutput: PostCompactOutput
  ): Promise<void> => {
    await compactAfterHook(hookInput, hookOutput)
  }

  return {
    // Session lifecycle events come through the event handler
    event: sessionEventHandler,
    // Tool hooks with merged after handler
    'tool.execute.before': toolOutputHooks['tool.execute.before'],
    'tool.execute.after': mergedToolAfterHook,
    // Message hooks
    'message.before': mergedMessageBeforeHook,
    'message.after': mergedMessageAfterHook,
    // Compaction hooks
    'compact.before': mergedCompactBeforeHook,
    'compact.after': mergedCompactAfterHook,
  }
}
