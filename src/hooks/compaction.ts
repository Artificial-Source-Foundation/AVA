/**
 * Delta9 Compaction Hooks
 *
 * Handles context compaction events for:
 * - State preservation across compaction
 * - Todo continuation (resume incomplete tasks)
 * - Mission recovery
 * - Critical context injection
 */

import type { MissionState } from '../mission/state.js'
import { appendHistory } from '../mission/history.js'
import { getEventStore } from '../events/store.js'

// =============================================================================
// Types
// =============================================================================

export interface CompactionHooksInput {
  /** Mission state instance */
  state: MissionState
  /** Project root directory */
  cwd: string
  /** Logger function */
  log: (level: string, message: string, data?: Record<string, unknown>) => void
}

/** Pre-compaction hook input */
export interface PreCompactInput {
  /** Session ID */
  sessionID: string
  /** Reason for compaction */
  reason: 'context_limit' | 'manual' | 'error'
  /** Current context size (tokens) */
  contextTokens: number
}

/** Pre-compaction hook output (mutable) */
export interface PreCompactOutput {
  /** State to preserve across compaction */
  preservedState: Record<string, unknown>
  /** Critical context to inject post-compaction */
  criticalContext: string
}

/** Post-compaction hook input */
export interface PostCompactInput {
  /** Session ID */
  sessionID: string
  /** Preserved state from pre-compact */
  preservedState: Record<string, unknown>
  /** Tokens before compaction */
  tokensBefore: number
  /** Tokens after compaction */
  tokensAfter: number
}

/** Post-compaction hook output */
export interface PostCompactOutput {
  /** Message to inject after compaction */
  injectedMessage?: string
  /** Instructions for continuation */
  continuationPrompt?: string
}

export interface CompactionHooks {
  /** Hook before compaction */
  'compact.before': (input: PreCompactInput, output: PreCompactOutput) => Promise<void>
  /** Hook after compaction */
  'compact.after': (input: PostCompactInput, output: PostCompactOutput) => Promise<void>
}

// =============================================================================
// State Tracking
// =============================================================================

/** Track compaction events per session */
const compactionHistory = new Map<
  string,
  Array<{
    timestamp: string
    tokensBefore: number
    tokensAfter: number
    reason: string
  }>
>()

/**
 * Get compaction history for a session
 */
export function getCompactionHistory(
  sessionId: string
): Array<{ timestamp: string; tokensBefore: number; tokensAfter: number; reason: string }> {
  return compactionHistory.get(sessionId) ?? []
}

/**
 * Clear compaction history (for testing)
 */
export function clearCompactionHistory(): void {
  compactionHistory.clear()
}

// =============================================================================
// Todo Continuation
// =============================================================================

interface TodoItem {
  id: string
  description: string
  status: 'pending' | 'in_progress' | 'completed'
}

/**
 * Extract todos that need continuation
 */
function extractIncompleteTodos(state: MissionState): TodoItem[] {
  const mission = state.getMission()
  if (!mission) return []

  const todos: TodoItem[] = []

  for (const objective of mission.objectives) {
    for (const task of objective.tasks) {
      if (task.status === 'pending' || task.status === 'in_progress') {
        todos.push({
          id: task.id,
          description: task.description,
          status: task.status,
        })
      }
    }
  }

  // Sort by status (in_progress before pending)
  return todos.sort((a, b) => {
    if (a.status === 'in_progress' && b.status !== 'in_progress') return -1
    if (b.status === 'in_progress' && a.status !== 'in_progress') return 1
    return 0
  })
}

/**
 * Format todos for continuation prompt
 */
function formatTodosForContinuation(todos: TodoItem[]): string {
  if (todos.length === 0) return ''

  const lines = ['## Incomplete Tasks (Continue from here)', '']

  for (const todo of todos.slice(0, 10)) {
    // Limit to top 10
    const statusIcon = todo.status === 'in_progress' ? '[IN PROGRESS]' : '[PENDING]'
    lines.push(`- ${statusIcon} ${todo.description}`)
  }

  if (todos.length > 10) {
    lines.push(``, `... and ${todos.length - 10} more tasks`)
  }

  return lines.join('\n')
}

// =============================================================================
// Critical Context
// =============================================================================

/**
 * Build critical context to preserve across compaction
 */
function buildCriticalContext(state: MissionState): string {
  const mission = state.getMission()
  if (!mission) return ''

  const sections: string[] = []

  // Mission overview
  sections.push('## Mission Summary')
  sections.push(`**Goal:** ${mission.description}`)
  sections.push(`**Status:** ${mission.status}`)
  sections.push(`**Progress:** ${state.getProgress()}%`)
  sections.push('')

  // Current objective
  const currentObjective = mission.objectives.find(
    (o) => o.status === 'in_progress' || o.status === 'pending'
  )
  if (currentObjective) {
    sections.push('## Current Objective')
    sections.push(currentObjective.description)
    sections.push('')
  }

  // In-progress task details
  const inProgressTask = findInProgressTask(state)
  if (inProgressTask) {
    sections.push('## Current Task (Resume Here)')
    sections.push(`**Task:** ${inProgressTask.description}`)
    if (inProgressTask.acceptanceCriteria?.length) {
      sections.push('**Criteria:**')
      for (const criterion of inProgressTask.acceptanceCriteria) {
        sections.push(`- ${criterion}`)
      }
    }
    if (inProgressTask.filesChanged?.length) {
      sections.push(`**Files Changed:** ${inProgressTask.filesChanged.join(', ')}`)
    }
    sections.push('')
  }

  // Recent errors (if any)
  const recentErrors = getRecentErrors(state)
  if (recentErrors.length > 0) {
    sections.push('## Recent Errors (Avoid)')
    for (const error of recentErrors.slice(0, 3)) {
      sections.push(`- ${error}`)
    }
    sections.push('')
  }

  return sections.join('\n')
}

/**
 * Find the currently in-progress task
 */
function findInProgressTask(state: MissionState): {
  id: string
  description: string
  acceptanceCriteria?: string[]
  filesChanged?: string[]
} | null {
  const mission = state.getMission()
  if (!mission) return null

  for (const objective of mission.objectives) {
    for (const task of objective.tasks) {
      if (task.status === 'in_progress') {
        return {
          id: task.id,
          description: task.description,
          acceptanceCriteria: task.acceptanceCriteria,
          filesChanged: task.filesChanged,
        }
      }
    }
  }
  return null
}

/**
 * Get recent errors from mission history
 */
function getRecentErrors(state: MissionState): string[] {
  const mission = state.getMission()
  if (!mission) return []

  const errors: string[] = []

  for (const objective of mission.objectives) {
    for (const task of objective.tasks) {
      if (task.error) {
        errors.push(`${task.description}: ${task.error}`)
      }
    }
  }

  return errors.slice(-5) // Last 5 errors
}

// =============================================================================
// Hook Factory
// =============================================================================

/**
 * Create compaction hooks with closure over state
 */
export function createCompactionHooks(input: CompactionHooksInput): CompactionHooks {
  const { state, cwd, log } = input

  return {
    /**
     * Before Compaction
     *
     * - Save critical state
     * - Build continuation context
     * - Record compaction event
     */
    'compact.before': async (compactInput, output) => {
      const { sessionID, reason, contextTokens } = compactInput

      log('info', 'Compaction starting', {
        sessionId: sessionID,
        reason,
        contextTokens,
      })

      const mission = state.getMission()

      // Build preserved state
      output.preservedState = {
        missionId: mission?.id,
        missionStatus: mission?.status,
        progress: state.getProgress(),
        inProgressTaskId: findInProgressTask(state)?.id,
        timestamp: new Date().toISOString(),
      }

      // Build critical context
      output.criticalContext = buildCriticalContext(state)

      // Record event
      try {
        const eventStore = getEventStore()
        eventStore.append('system.context_compacted', {
          tokensBefore: contextTokens,
          tokensAfter: 0, // Will be updated in post-compact
          preservedState: true,
        })
      } catch {
        // Event store may not be initialized
      }

      // Log to history
      if (mission) {
        appendHistory(cwd, {
          type: 'context_compacted',
          timestamp: new Date().toISOString(),
          missionId: mission.id,
          data: {
            sessionId: sessionID,
            reason,
            tokensBefore: contextTokens,
          },
        })
      }
    },

    /**
     * After Compaction
     *
     * - Reload mission state
     * - Build continuation prompt
     * - Inject critical context
     */
    'compact.after': async (compactInput, output) => {
      const { sessionID, preservedState, tokensBefore, tokensAfter } = compactInput

      // Record compaction in history
      const history = compactionHistory.get(sessionID) ?? []
      history.push({
        timestamp: new Date().toISOString(),
        tokensBefore,
        tokensAfter,
        reason: 'compaction',
      })
      compactionHistory.set(sessionID, history)

      log('info', 'Compaction completed', {
        sessionId: sessionID,
        tokensBefore,
        tokensAfter,
        tokensSaved: tokensBefore - tokensAfter,
      })

      // Reload mission state from disk
      const mission = state.load()

      if (!mission) {
        log('debug', 'No mission to restore after compaction')
        return
      }

      // Get incomplete todos
      const todos = extractIncompleteTodos(state)
      const todosContinuation = formatTodosForContinuation(todos)

      // Build continuation prompt
      const continuationParts: string[] = []

      continuationParts.push('## Context Restored After Compaction')
      continuationParts.push('')
      continuationParts.push(`Mission "${mission.description}" is ${mission.status}.`)
      continuationParts.push(`Progress: ${state.getProgress()}%`)
      continuationParts.push('')

      if (todosContinuation) {
        continuationParts.push(todosContinuation)
        continuationParts.push('')
      }

      // Add any preserved context
      if (preservedState.inProgressTaskId) {
        const task = state.getTask(preservedState.inProgressTaskId as string)
        if (task && task.status === 'in_progress') {
          continuationParts.push('## Resume Current Task')
          continuationParts.push(`Continue working on: ${task.description}`)
          continuationParts.push('')
        }
      }

      output.continuationPrompt = continuationParts.join('\n')

      // Inject message about compaction
      output.injectedMessage = `[Context compacted - ${tokensBefore - tokensAfter} tokens freed. Mission state restored from disk.]`
    },
  }
}
