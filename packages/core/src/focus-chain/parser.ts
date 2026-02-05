/**
 * Focus Chain Parser
 *
 * Parse markdown task lists into structured FocusTask objects
 */

import type { FocusTask, TaskStatus } from './types.js'

// ============================================================================
// Regex Patterns
// ============================================================================

/**
 * Match a markdown task item:
 * - [ ] Pending task
 * - [x] Completed task
 * - [~] In progress task
 * - [!] Blocked task
 */
const TASK_PATTERN = /^(\s*)- \[([ x~!])\] (.+)$/

/**
 * Status character mapping
 */
const STATUS_CHARS: Record<string, TaskStatus> = {
  ' ': 'pending',
  x: 'completed',
  '~': 'in_progress',
  '!': 'blocked',
}

const CHAR_STATUS: Record<TaskStatus, string> = {
  pending: ' ',
  completed: 'x',
  in_progress: '~',
  blocked: '!',
}

// ============================================================================
// Parser Functions
// ============================================================================

/**
 * Parse a markdown file into FocusTask array
 */
export function parseMarkdown(content: string): FocusTask[] {
  const lines = content.split('\n')
  const tasks: FocusTask[] = []
  const parentStack: { id: string; level: number }[] = []
  let taskIndex = 0

  for (let lineNum = 0; lineNum < lines.length; lineNum++) {
    const line = lines[lineNum]
    const match = line.match(TASK_PATTERN)

    if (!match) continue

    const [, indent, statusChar, text] = match
    const level = Math.floor(indent.length / 2) // 2 spaces per level
    const status = STATUS_CHARS[statusChar] || 'pending'
    const id = `task-${taskIndex++}`

    // Find parent based on indentation
    while (parentStack.length > 0 && parentStack[parentStack.length - 1].level >= level) {
      parentStack.pop()
    }

    const parentId = parentStack.length > 0 ? parentStack[parentStack.length - 1].id : undefined

    const task: FocusTask = {
      id,
      text: text.trim(),
      status,
      level,
      line: lineNum + 1,
      parentId,
      childIds: [],
    }

    // Add as child to parent
    if (parentId) {
      const parent = tasks.find((t) => t.id === parentId)
      if (parent) {
        parent.childIds.push(id)
      }
    }

    tasks.push(task)
    parentStack.push({ id, level })
  }

  return tasks
}

/**
 * Serialize FocusTask array back to markdown
 */
export function serializeToMarkdown(tasks: FocusTask[], title?: string): string {
  const lines: string[] = []

  // Add title if provided
  if (title) {
    lines.push(`# ${title}`)
    lines.push('')
  }

  // Sort tasks by original line number to preserve order
  const sortedTasks = [...tasks].sort((a, b) => a.line - b.line)

  for (const task of sortedTasks) {
    const indent = '  '.repeat(task.level)
    const statusChar = CHAR_STATUS[task.status]
    lines.push(`${indent}- [${statusChar}] ${task.text}`)

    // Add notes as sub-items if present
    if (task.notes) {
      const noteLines = task.notes.split('\n')
      for (const noteLine of noteLines) {
        lines.push(`${'  '.repeat(task.level + 1)}> ${noteLine}`)
      }
    }
  }

  return lines.join('\n')
}

/**
 * Update a task's status in markdown content
 */
export function updateTaskInMarkdown(
  content: string,
  taskId: string,
  tasks: FocusTask[],
  newStatus: TaskStatus
): string {
  const task = tasks.find((t) => t.id === taskId)
  if (!task) return content

  const lines = content.split('\n')
  const lineIndex = task.line - 1

  if (lineIndex >= 0 && lineIndex < lines.length) {
    const line = lines[lineIndex]
    const match = line.match(TASK_PATTERN)

    if (match) {
      const [, indent, , text] = match
      const newStatusChar = CHAR_STATUS[newStatus]
      lines[lineIndex] = `${indent}- [${newStatusChar}] ${text}`
    }
  }

  return lines.join('\n')
}

/**
 * Add a new task to markdown content
 */
export function addTaskToMarkdown(
  content: string,
  text: string,
  options: { level?: number; afterLine?: number; status?: TaskStatus } = {}
): string {
  const { level = 0, afterLine, status = 'pending' } = options
  const lines = content.split('\n')

  const indent = '  '.repeat(level)
  const statusChar = CHAR_STATUS[status]
  const newLine = `${indent}- [${statusChar}] ${text}`

  if (afterLine !== undefined && afterLine > 0 && afterLine <= lines.length) {
    lines.splice(afterLine, 0, newLine)
  } else {
    // Add at the end
    if (lines[lines.length - 1] !== '') {
      lines.push('')
    }
    lines.push(newLine)
  }

  return lines.join('\n')
}

/**
 * Remove a task from markdown content
 */
export function removeTaskFromMarkdown(
  content: string,
  taskId: string,
  tasks: FocusTask[]
): string {
  const task = tasks.find((t) => t.id === taskId)
  if (!task) return content

  const lines = content.split('\n')

  // Remove the task line and any child lines
  const linesToRemove = new Set<number>()
  linesToRemove.add(task.line - 1)

  // Find all descendant tasks
  const findDescendants = (parentId: string) => {
    for (const t of tasks) {
      if (t.parentId === parentId) {
        linesToRemove.add(t.line - 1)
        findDescendants(t.id)
      }
    }
  }
  findDescendants(taskId)

  // Filter out removed lines
  return lines.filter((_, index) => !linesToRemove.has(index)).join('\n')
}

/**
 * Calculate progress statistics
 */
export function calculateProgress(tasks: FocusTask[]): {
  total: number
  completed: number
  inProgress: number
  blocked: number
  pending: number
  percentComplete: number
} {
  const total = tasks.length
  const completed = tasks.filter((t) => t.status === 'completed').length
  const inProgress = tasks.filter((t) => t.status === 'in_progress').length
  const blocked = tasks.filter((t) => t.status === 'blocked').length
  const pending = tasks.filter((t) => t.status === 'pending').length

  return {
    total,
    completed,
    inProgress,
    blocked,
    pending,
    percentComplete: total > 0 ? Math.round((completed / total) * 100) : 0,
  }
}

/**
 * Get the next actionable task (pending, not blocked, no incomplete children)
 */
export function getNextTask(tasks: FocusTask[]): FocusTask | null {
  // Find tasks that are pending and have no incomplete children
  const actionable = tasks.filter((task) => {
    if (task.status !== 'pending') return false

    // Check if all children are completed
    if (task.childIds.length > 0) {
      const allChildrenComplete = task.childIds.every((childId) => {
        const child = tasks.find((t) => t.id === childId)
        return child?.status === 'completed'
      })
      if (!allChildrenComplete) return false
    }

    return true
  })

  // Return the first actionable task (by line order)
  return actionable.sort((a, b) => a.line - b.line)[0] || null
}
