/**
 * File Versions Service
 *
 * Maintains a stack of file snapshots for undo/redo during a session.
 * Uses the originalContent/newContent from FileOperations as version history.
 */

import type { FileOperation } from '../types'

interface FileVersion {
  filePath: string
  content: string
  operationId: string
  timestamp: number
}

interface FileVersionStack {
  /** Stack of undone changes (for redo) */
  redoStack: FileVersion[]
  /** Stack of applied changes (for undo) */
  undoStack: FileVersion[]
}

// Per-session version stacks, keyed by session ID
const sessionStacks = new Map<string, FileVersionStack>()

function getStack(sessionId: string): FileVersionStack {
  let stack = sessionStacks.get(sessionId)
  if (!stack) {
    stack = { redoStack: [], undoStack: [] }
    sessionStacks.set(sessionId, stack)
  }
  return stack
}

/**
 * Record a file change from a completed tool operation.
 * Call this after each file-modifying tool execution.
 */
export function recordFileChange(sessionId: string, operation: FileOperation): void {
  if (!operation.originalContent && !operation.newContent) return
  if (operation.type === 'read') return

  const stack = getStack(sessionId)

  // Push the "before" state to the undo stack
  stack.undoStack.push({
    filePath: operation.filePath,
    content: operation.originalContent ?? '', // empty string for new files
    operationId: operation.id,
    timestamp: operation.timestamp,
  })

  // Clear redo stack when a new change is made (standard undo/redo behavior)
  stack.redoStack.length = 0
}

/**
 * Undo the last file change. Returns the file path + content to write,
 * or null if nothing to undo.
 */
export function undoFileChange(
  sessionId: string,
  currentFileContents: (filePath: string) => Promise<string | null>
): Promise<{ filePath: string; content: string } | null> {
  return performUndoRedo(sessionId, 'undo', currentFileContents)
}

/**
 * Redo the last undone file change. Returns the file path + content to write,
 * or null if nothing to redo.
 */
export function redoFileChange(
  sessionId: string,
  currentFileContents: (filePath: string) => Promise<string | null>
): Promise<{ filePath: string; content: string } | null> {
  return performUndoRedo(sessionId, 'redo', currentFileContents)
}

async function performUndoRedo(
  sessionId: string,
  direction: 'undo' | 'redo',
  currentFileContents: (filePath: string) => Promise<string | null>
): Promise<{ filePath: string; content: string } | null> {
  const stack = getStack(sessionId)
  const source = direction === 'undo' ? stack.undoStack : stack.redoStack
  const target = direction === 'undo' ? stack.redoStack : stack.undoStack

  const entry = source.pop()
  if (!entry) return null

  // Save current content to the opposite stack before reverting
  const currentContent = await currentFileContents(entry.filePath)
  target.push({
    filePath: entry.filePath,
    content: currentContent ?? '',
    operationId: entry.operationId,
    timestamp: Date.now(),
  })

  return { filePath: entry.filePath, content: entry.content }
}

/** Get undo/redo stack sizes for a session */
export function getVersionCounts(sessionId: string): { undoCount: number; redoCount: number } {
  const stack = sessionStacks.get(sessionId)
  if (!stack) return { undoCount: 0, redoCount: 0 }
  return { undoCount: stack.undoStack.length, redoCount: stack.redoStack.length }
}

/** Clear all version history for a session */
export function clearVersionHistory(sessionId: string): void {
  sessionStacks.delete(sessionId)
}
