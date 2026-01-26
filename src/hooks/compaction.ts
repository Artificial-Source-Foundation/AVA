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
// Critical Context - 5-Section Template (oh-my-opencode pattern)
// =============================================================================

/**
 * Build critical context using 5-section structured template.
 *
 * Sections:
 * 1. USER REQUESTS - What the user originally asked for
 * 2. FINAL GOAL - Mission description (ultimate objective)
 * 3. WORK COMPLETED - Tasks done with files changed
 * 4. REMAINING TASKS - Pending/in-progress with planned files
 * 5. MUST NOT DO - Constraints, guardrails, failed approaches
 *
 * Based on oh-my-opencode's SUMMARIZE_CONTEXT_PROMPT pattern.
 */
function buildCriticalContext(state: MissionState): string {
  const mission = state.getMission()
  if (!mission) return ''

  const sections: string[] = []

  // Header
  sections.push('# Delta9 Session Restored')
  sections.push('')

  // Section 1: USER REQUESTS (Original intent)
  sections.push('## 1. USER REQUESTS')
  sections.push(mission.description)
  sections.push('')

  // Section 2: FINAL GOAL
  sections.push('## 2. FINAL GOAL')
  sections.push(`Mission: ${mission.description}`)
  sections.push(`Status: ${mission.status}`)
  sections.push(`Progress: ${state.getProgress()}%`)
  sections.push('')

  // Section 3: WORK COMPLETED
  sections.push('## 3. WORK COMPLETED')
  const completedTasks = getCompletedTasks(state)
  if (completedTasks.length > 0) {
    for (const task of completedTasks) {
      const files = task.filesChanged?.join(', ') || 'no files'
      sections.push(`- [${task.id}] ${task.description} (${files})`)
    }
  } else {
    sections.push('- No tasks completed yet')
  }
  sections.push('')

  // Section 4: REMAINING TASKS
  sections.push('## 4. REMAINING TASKS')
  const remainingTasks = getRemainingTasks(state)
  if (remainingTasks.length > 0) {
    for (const task of remainingTasks) {
      const status = task.status === 'in_progress' ? '[IN PROGRESS]' : '[PENDING]'
      const files = task.files?.join(', ') || 'unspecified files'
      sections.push(`- ${status} [${task.id}] ${task.description} (files: ${files})`)
    }
  } else {
    sections.push('- All tasks completed')
  }
  sections.push('')

  // Section 5: MUST NOT DO
  sections.push('## 5. MUST NOT DO')
  const constraints = collectConstraints(state)
  if (constraints.length > 0) {
    for (const constraint of constraints) {
      sections.push(`- ${constraint}`)
    }
  } else {
    sections.push('- None specified')
  }
  sections.push('')

  // Immediate actions
  sections.push('## IMMEDIATE ACTIONS')
  sections.push('1. Run `mission_status()` to see current state')
  sections.push('2. Find next ready task or resume in-progress task')
  sections.push('3. Dispatch to operator with `dispatch_task()`')
  sections.push('')
  sections.push('DO NOT re-read files already analyzed. Trust completed work.')

  return sections.join('\n')
}

/**
 * Get completed tasks with file information
 */
function getCompletedTasks(state: MissionState): Array<{
  id: string
  description: string
  filesChanged?: string[]
}> {
  const mission = state.getMission()
  if (!mission) return []

  const completed: Array<{
    id: string
    description: string
    filesChanged?: string[]
  }> = []

  for (const objective of mission.objectives) {
    for (const task of objective.tasks) {
      if (task.status === 'completed') {
        completed.push({
          id: task.id,
          description: task.description,
          filesChanged: task.filesChanged,
        })
      }
    }
  }

  return completed
}

/**
 * Get remaining (pending/in-progress) tasks with planned files
 */
function getRemainingTasks(state: MissionState): Array<{
  id: string
  description: string
  status: string
  files?: string[]
}> {
  const mission = state.getMission()
  if (!mission) return []

  const remaining: Array<{
    id: string
    description: string
    status: string
    files?: string[]
  }> = []

  for (const objective of mission.objectives) {
    for (const task of objective.tasks) {
      if (task.status === 'pending' || task.status === 'in_progress') {
        remaining.push({
          id: task.id,
          description: task.description,
          status: task.status,
          files: task.files,
        })
      }
    }
  }

  // Sort: in_progress first
  return remaining.sort((a, b) => {
    if (a.status === 'in_progress' && b.status !== 'in_progress') return -1
    if (b.status === 'in_progress' && a.status !== 'in_progress') return 1
    return 0
  })
}

/**
 * Collect all constraints: guardrails + task mustNot + failed approaches
 */
function collectConstraints(state: MissionState): string[] {
  const mission = state.getMission()
  if (!mission) return []

  const constraints: string[] = []

  // Mission guardrails (if they exist in mission - we'll check)
  // Note: Mission type may need extending for guardrails

  // Collect task-level mustNot constraints
  for (const objective of mission.objectives) {
    for (const task of objective.tasks) {
      if (task.mustNot && task.mustNot.length > 0) {
        for (const c of task.mustNot) {
          if (!constraints.includes(c)) {
            constraints.push(c)
          }
        }
      }
    }
  }

  // Add failed approaches (tasks that failed)
  const failedTasks = []
  for (const objective of mission.objectives) {
    for (const task of objective.tasks) {
      if (task.status === 'failed' && task.error) {
        failedTasks.push(`Avoid: ${task.description} (failed: ${task.error})`)
      }
    }
  }
  constraints.push(...failedTasks.slice(0, 3)) // Limit to 3

  return constraints
}

/**
 * Find the currently in-progress task
 */
function findInProgressTask(state: MissionState): {
  id: string
  description: string
  acceptanceCriteria?: string[]
  filesChanged?: string[]
  files?: string[]
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
          files: task.files,
        }
      }
    }
  }
  return null
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
