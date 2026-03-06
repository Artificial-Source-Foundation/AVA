/**
 * Session Data Actions
 * Agents, file operations, terminal executions, memory items,
 * read-only files, and background plan state management.
 */

import {
  clearFileOperations as dbClearFileOperations,
  clearMemoryItems as dbClearMemoryItems,
  clearTerminalExecutions as dbClearTerminalExecutions,
  deleteMemoryItem as dbDeleteMemoryItem,
  updateAgentInDb as dbUpdateAgent,
  updateTerminalExecution as dbUpdateTerminalExecution,
  getAllMemoryItems,
  saveAgent,
  saveFileOperation,
  saveMemoryItem,
  saveTerminalExecution,
} from '../../services/database'
import { readFileContent } from '../../services/file-browser'
import {
  clearVersionHistory,
  getVersionCounts as getVersionCountsImpl,
  redoFileChange as redoFileChangeImpl,
  undoFileChange as undoFileChangeImpl,
} from '../../services/file-versions'
import { logError, logInfo } from '../../services/logger'
import type { Agent, FileOperation, MemoryItem, TerminalExecution } from '../../types'
import {
  currentSession,
  fileOperations,
  messages,
  readOnlyFiles,
  setAgents,
  setBackgroundPlanActive,
  setBackgroundPlanProgress,
  setFileOperations,
  setMemoryItems,
  setReadOnlyFiles,
  setTerminalExecutions,
} from './session-state'

// ============================================================================
// Agent Management
// ============================================================================

export function addAgent(agent: Agent): void {
  setAgents((prev) => [...prev, agent])
  saveAgent(agent).catch((err) => logError('Session', 'Failed to save agent', err))
}

export function updateAgent(id: string, updates: Partial<Agent>): void {
  setAgents((prev) => prev.map((agent) => (agent.id === id ? { ...agent, ...updates } : agent)))
  dbUpdateAgent(id, updates).catch((err) =>
    logError('Session', 'Failed to update agent in DB', err)
  )
}

export function removeAgent(id: string): void {
  setAgents((prev) => prev.filter((agent) => agent.id !== id))
}

// ============================================================================
// File Operations Management
// ============================================================================

export async function addFileOperation(operation: FileOperation): Promise<void> {
  setFileOperations((prev) => [operation, ...prev])
  try {
    await saveFileOperation(operation)
  } catch (err) {
    logError('Session', 'Failed to save file operation', err)
  }
}

export async function clearFileOperations(): Promise<void> {
  const sessionId = currentSession()?.id
  setFileOperations([])
  if (sessionId) {
    try {
      await dbClearFileOperations(sessionId)
      clearVersionHistory(sessionId)
    } catch (err) {
      logError('Session', 'Failed to clear file operations', err)
    }
  }
}

export async function undoFileChange(): Promise<string | null> {
  const sessionId = currentSession()?.id
  if (!sessionId) return null

  try {
    const result = await undoFileChangeImpl(sessionId, readFileContent)
    if (!result) return null

    const fs = await import('@tauri-apps/plugin-fs')
    await fs.writeTextFile(result.filePath, result.content)
    logInfo('Session', 'Undid file change', { filePath: result.filePath })
    return result.filePath
  } catch (err) {
    logError('Session', 'Failed to undo file change', err)
    return null
  }
}

export async function redoFileChange(): Promise<string | null> {
  const sessionId = currentSession()?.id
  if (!sessionId) return null

  try {
    const result = await redoFileChangeImpl(sessionId, readFileContent)
    if (!result) return null

    const fs = await import('@tauri-apps/plugin-fs')
    await fs.writeTextFile(result.filePath, result.content)
    logInfo('Session', 'Redid file change', { filePath: result.filePath })
    return result.filePath
  } catch (err) {
    logError('Session', 'Failed to redo file change', err)
    return null
  }
}

export function getVersionCounts(): { undoCount: number; redoCount: number } {
  const sessionId = currentSession()?.id
  if (!sessionId) return { undoCount: 0, redoCount: 0 }
  return getVersionCountsImpl(sessionId)
}

// ============================================================================
// Terminal Executions Management
// ============================================================================

export async function addTerminalExecution(execution: TerminalExecution): Promise<void> {
  setTerminalExecutions((prev) => [execution, ...prev])
  try {
    await saveTerminalExecution(execution)
  } catch (err) {
    logError('Session', 'Failed to save terminal execution', err)
  }
}

export async function updateTerminalExecution(
  id: string,
  updates: Partial<TerminalExecution>
): Promise<void> {
  setTerminalExecutions((prev) =>
    prev.map((exec) => (exec.id === id ? { ...exec, ...updates } : exec))
  )
  try {
    await dbUpdateTerminalExecution(id, updates)
  } catch (err) {
    logError('Session', 'Failed to update terminal execution', err)
  }
}

export async function clearTerminalExecutions(): Promise<void> {
  const sessionId = currentSession()?.id
  setTerminalExecutions([])
  if (sessionId) {
    try {
      await dbClearTerminalExecutions(sessionId)
    } catch (err) {
      logError('Session', 'Failed to clear terminal executions', err)
    }
  }
}

// ============================================================================
// Memory Items Management
// ============================================================================

export async function addMemoryItem(item: MemoryItem): Promise<void> {
  setMemoryItems((prev) => [item, ...prev])
  try {
    await saveMemoryItem(item)
  } catch (err) {
    logError('Session', 'Failed to save memory item', err)
  }
}

export async function removeMemoryItem(id: string): Promise<void> {
  setMemoryItems((prev) => prev.filter((item) => item.id !== id))
  try {
    await dbDeleteMemoryItem(id)
  } catch (err) {
    logError('Session', 'Failed to delete memory item', err)
  }
}

export async function clearMemoryItems(): Promise<void> {
  const sessionId = currentSession()?.id
  setMemoryItems([])
  if (sessionId) {
    try {
      await dbClearMemoryItems(sessionId)
    } catch (err) {
      logError('Session', 'Failed to clear memory items', err)
    }
  }
}

export async function queryMemoriesAcrossSessions(projectId?: string): Promise<MemoryItem[]> {
  try {
    return await getAllMemoryItems(projectId)
  } catch (err) {
    logError('Session', 'Failed to query cross-session memories', err)
    return []
  }
}

// ============================================================================
// Revert Files After Message
// ============================================================================

export async function revertFilesAfter(messageId: string): Promise<number> {
  const msgs = messages()
  const index = msgs.findIndex((m) => m.id === messageId)
  if (index === -1) return 0

  const targetTimestamp = msgs[index].createdAt
  const ops = fileOperations().filter((op) => op.timestamp > targetTimestamp && op.originalContent)

  if (ops.length === 0) return 0

  let reverted = 0
  try {
    const fs = await import('@tauri-apps/plugin-fs')
    for (const op of ops) {
      if (op.originalContent) {
        try {
          await fs.writeTextFile(op.filePath, op.originalContent)
          reverted++
        } catch (err) {
          logError('Session', `Failed to revert ${op.filePath}`, err)
        }
      }
    }
  } catch (err) {
    logError('Session', 'Failed to import Tauri FS for revert', err)
  }
  return reverted
}

// ============================================================================
// Read-Only File Context
// ============================================================================

export function toggleReadOnly(filePath: string): void {
  setReadOnlyFiles((prev) =>
    prev.includes(filePath) ? prev.filter((f) => f !== filePath) : [...prev, filePath]
  )
}

export function isReadOnly(filePath: string): boolean {
  return readOnlyFiles().includes(filePath)
}

// ============================================================================
// Background Plan Execution
// ============================================================================

export function startBackgroundPlan(): void {
  setBackgroundPlanActive(true)
  setBackgroundPlanProgress('Plan running...')
}

export function updateBackgroundPlanProgress(text: string): void {
  setBackgroundPlanProgress(text)
}

export function stopBackgroundPlan(): void {
  setBackgroundPlanActive(false)
  setBackgroundPlanProgress('')
}
