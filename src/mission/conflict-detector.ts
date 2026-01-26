/**
 * Delta9 File Conflict Detector
 *
 * Detects file conflicts between tasks before dispatch.
 * Inspired by SWARM's file-based decomposition pattern.
 *
 * Philosophy: "Every task explicitly owns files. Conflicts detected before dispatch."
 */

import type { Task } from '../types/mission.js'

// =============================================================================
// Types
// =============================================================================

export type ConflictType = 'write_write' | 'write_readonly'

export interface FileConflict {
  /** File path with conflict */
  file: string
  /** Task IDs involved in conflict */
  tasks: string[]
  /** Type of conflict */
  type: ConflictType
  /** Human-readable description */
  description: string
}

export interface ConflictCheckResult {
  /** Whether conflicts were found */
  hasConflicts: boolean
  /** List of detected conflicts */
  conflicts: FileConflict[]
  /** Summary message */
  summary: string
}

// =============================================================================
// Conflict Detection
// =============================================================================

/**
 * Detect file conflicts between active tasks.
 *
 * Checks for:
 * - write_write: Two tasks claiming the same file for modification
 * - write_readonly: One task claims write, another claims readonly
 *
 * @param tasks - All tasks to check for conflicts
 * @returns Conflict check result
 */
export function detectFileConflicts(tasks: Task[]): ConflictCheckResult {
  const conflicts: FileConflict[] = []

  // Maps for tracking file ownership
  const writeMap = new Map<string, string[]>() // file -> task IDs with write access
  const readonlyMap = new Map<string, string[]>() // file -> task IDs with readonly access

  // Only check active tasks (not completed/failed)
  const activeTasks = tasks.filter(
    (t) => t.status === 'pending' || t.status === 'in_progress' || t.status === 'blocked'
  )

  // Build ownership maps
  for (const task of activeTasks) {
    // Track write ownership
    for (const file of task.files ?? []) {
      const normalized = normalizeFilePath(file)
      const existing = writeMap.get(normalized) ?? []
      writeMap.set(normalized, [...existing, task.id])
    }

    // Track readonly access
    for (const file of task.filesReadonly ?? []) {
      const normalized = normalizeFilePath(file)
      const existing = readonlyMap.get(normalized) ?? []
      readonlyMap.set(normalized, [...existing, task.id])
    }
  }

  // Detect write-write conflicts
  for (const [file, taskIds] of writeMap) {
    if (taskIds.length > 1) {
      conflicts.push({
        file,
        tasks: taskIds,
        type: 'write_write',
        description: `Multiple tasks claim write access to ${file}: ${taskIds.join(', ')}`,
      })
    }
  }

  // Detect write-readonly conflicts
  for (const [file, writerIds] of writeMap) {
    const readerIds = readonlyMap.get(file) ?? []
    if (readerIds.length > 0 && writerIds.length > 0) {
      // Only flag if readers are different from writers
      const conflictingReaders = readerIds.filter((id) => !writerIds.includes(id))
      if (conflictingReaders.length > 0) {
        conflicts.push({
          file,
          tasks: [...writerIds, ...conflictingReaders],
          type: 'write_readonly',
          description: `File ${file} is being modified by ${writerIds.join(', ')} while ${conflictingReaders.join(', ')} expects readonly access`,
        })
      }
    }
  }

  // Build summary
  const summary = buildConflictSummary(conflicts)

  return {
    hasConflicts: conflicts.length > 0,
    conflicts,
    summary,
  }
}

/**
 * Check if a new task would conflict with existing tasks.
 *
 * @param newTask - Task to check
 * @param existingTasks - Already dispatched tasks
 * @returns Conflict check result
 */
export function checkTaskConflicts(
  newTask: Pick<Task, 'id' | 'files' | 'filesReadonly'>,
  existingTasks: Task[]
): ConflictCheckResult {
  // Create a synthetic task for conflict detection
  const syntheticTask: Task = {
    id: newTask.id,
    description: '',
    status: 'pending',
    attempts: 0,
    acceptanceCriteria: [],
    files: newTask.files,
    filesReadonly: newTask.filesReadonly,
  }

  // Only check against active existing tasks
  const activeTasks = existingTasks.filter(
    (t) => t.status === 'in_progress' || t.status === 'pending'
  )

  return detectFileConflicts([syntheticTask, ...activeTasks])
}

/**
 * Format conflicts for display to agent.
 */
export function formatConflicts(result: ConflictCheckResult): string {
  if (!result.hasConflicts) {
    return 'No file conflicts detected.'
  }

  const lines = ['[FILE CONFLICT DETECTED]', '', result.summary, '', 'Conflicts:']

  for (const conflict of result.conflicts) {
    lines.push(`  - ${conflict.description}`)
  }

  lines.push(
    '',
    'Resolution Options:',
    '  1. Ensure only one task claims each file',
    '  2. Complete conflicting task first before dispatching',
    '  3. Split the file into separate files',
    '  4. Make one task readonly if it only needs to read'
  )

  return lines.join('\n')
}

// =============================================================================
// Helpers
// =============================================================================

/**
 * Normalize file path for comparison.
 * Handles leading/trailing slashes, ./ prefixes, etc.
 */
function normalizeFilePath(path: string): string {
  return path
    .replace(/^\.\//, '') // Remove leading ./
    .replace(/^\//, '') // Remove leading /
    .replace(/\/$/, '') // Remove trailing /
    .toLowerCase() // Case-insensitive (for cross-platform)
}

/**
 * Build summary message for conflicts.
 */
function buildConflictSummary(conflicts: FileConflict[]): string {
  if (conflicts.length === 0) {
    return 'No conflicts detected.'
  }

  const writeWriteCount = conflicts.filter((c) => c.type === 'write_write').length
  const writeReadonlyCount = conflicts.filter((c) => c.type === 'write_readonly').length

  const parts: string[] = []

  if (writeWriteCount > 0) {
    parts.push(`${writeWriteCount} write-write conflict${writeWriteCount > 1 ? 's' : ''}`)
  }

  if (writeReadonlyCount > 0) {
    parts.push(`${writeReadonlyCount} write-readonly conflict${writeReadonlyCount > 1 ? 's' : ''}`)
  }

  return `Found ${parts.join(' and ')} across ${conflicts.length} file${conflicts.length > 1 ? 's' : ''}.`
}
