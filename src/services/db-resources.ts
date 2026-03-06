/**
 * Database Resource Operations
 *
 * CRUD for file operations, terminal executions, and memory items.
 */

import type { FileOperation, MemoryItem, TerminalExecution } from '../types'
import { initDatabase } from './db-init'

// ============================================================================
// File Operations
// ============================================================================

/**
 * Save a file operation
 */
export async function saveFileOperation(operation: FileOperation): Promise<void> {
  const database = await initDatabase()
  await database.execute(
    `INSERT INTO file_operations
     (id, session_id, agent_id, agent_name, type, file_path, timestamp, lines,
      lines_added, lines_removed, is_new, original_content, new_content)
     VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)`,
    [
      operation.id,
      operation.sessionId,
      operation.agentId || null,
      operation.agentName || null,
      operation.type,
      operation.filePath,
      operation.timestamp,
      operation.lines || null,
      operation.linesAdded || null,
      operation.linesRemoved || null,
      operation.isNew ? 1 : 0,
      operation.originalContent || null,
      operation.newContent || null,
    ]
  )
}

/**
 * Get file operations for a session
 */
export async function getFileOperations(sessionId: string): Promise<FileOperation[]> {
  const database = await initDatabase()
  const rows = await database.select<Array<Record<string, unknown>>>(
    'SELECT * FROM file_operations WHERE session_id = ? ORDER BY timestamp DESC',
    [sessionId]
  )
  return rows.map((row) => ({
    id: row.id as string,
    sessionId: row.session_id as string,
    agentId: row.agent_id as string | undefined,
    agentName: row.agent_name as string | undefined,
    type: row.type as FileOperation['type'],
    filePath: row.file_path as string,
    timestamp: row.timestamp as number,
    lines: row.lines as number | undefined,
    linesAdded: row.lines_added as number | undefined,
    linesRemoved: row.lines_removed as number | undefined,
    isNew: row.is_new === 1,
    originalContent: row.original_content as string | undefined,
    newContent: row.new_content as string | undefined,
  }))
}

/**
 * Clear file operations for a session
 */
export async function clearFileOperations(sessionId: string): Promise<void> {
  const database = await initDatabase()
  await database.execute('DELETE FROM file_operations WHERE session_id = ?', [sessionId])
}

// ============================================================================
// Terminal Executions
// ============================================================================

/**
 * Save a terminal execution
 */
export async function saveTerminalExecution(execution: TerminalExecution): Promise<void> {
  const database = await initDatabase()
  await database.execute(
    `INSERT INTO terminal_executions
     (id, session_id, agent_id, agent_name, command, output, status, exit_code,
      started_at, completed_at, cwd)
     VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)`,
    [
      execution.id,
      execution.sessionId,
      execution.agentId || null,
      execution.agentName || null,
      execution.command,
      execution.output,
      execution.status,
      execution.exitCode ?? null,
      execution.startedAt,
      execution.completedAt || null,
      execution.cwd || null,
    ]
  )
}

/**
 * Update a terminal execution (e.g., when it completes)
 */
export async function updateTerminalExecution(
  id: string,
  updates: Partial<Pick<TerminalExecution, 'output' | 'status' | 'exitCode' | 'completedAt'>>
): Promise<void> {
  const database = await initDatabase()
  const setClauses: string[] = []
  const values: unknown[] = []

  if (updates.output !== undefined) {
    setClauses.push('output = ?')
    values.push(updates.output)
  }
  if (updates.status !== undefined) {
    setClauses.push('status = ?')
    values.push(updates.status)
  }
  if (updates.exitCode !== undefined) {
    setClauses.push('exit_code = ?')
    values.push(updates.exitCode)
  }
  if (updates.completedAt !== undefined) {
    setClauses.push('completed_at = ?')
    values.push(updates.completedAt)
  }

  if (setClauses.length === 0) return

  values.push(id)
  await database.execute(
    `UPDATE terminal_executions SET ${setClauses.join(', ')} WHERE id = ?`,
    values
  )
}

/**
 * Get terminal executions for a session
 */
export async function getTerminalExecutions(sessionId: string): Promise<TerminalExecution[]> {
  const database = await initDatabase()
  const rows = await database.select<Array<Record<string, unknown>>>(
    'SELECT * FROM terminal_executions WHERE session_id = ? ORDER BY started_at DESC',
    [sessionId]
  )
  return rows.map((row) => ({
    id: row.id as string,
    sessionId: row.session_id as string,
    agentId: row.agent_id as string | undefined,
    agentName: row.agent_name as string | undefined,
    command: row.command as string,
    output: row.output as string,
    status: row.status as TerminalExecution['status'],
    exitCode: row.exit_code as number | undefined,
    startedAt: row.started_at as number,
    completedAt: row.completed_at as number | undefined,
    cwd: row.cwd as string | undefined,
  }))
}

/**
 * Clear terminal executions for a session
 */
export async function clearTerminalExecutions(sessionId: string): Promise<void> {
  const database = await initDatabase()
  await database.execute('DELETE FROM terminal_executions WHERE session_id = ?', [sessionId])
}

// ============================================================================
// Memory Items
// ============================================================================

/**
 * Save a memory item
 */
export async function saveMemoryItem(item: MemoryItem): Promise<void> {
  const database = await initDatabase()
  await database.execute(
    `INSERT INTO memory_items
     (id, session_id, type, title, preview, tokens, created_at, source)
     VALUES (?, ?, ?, ?, ?, ?, ?, ?)`,
    [
      item.id,
      item.sessionId,
      item.type,
      item.title,
      item.preview,
      item.tokens,
      item.createdAt,
      item.source || null,
    ]
  )
}

/**
 * Get memory items for a session
 */
export async function getMemoryItems(sessionId: string): Promise<MemoryItem[]> {
  const database = await initDatabase()
  const rows = await database.select<Array<Record<string, unknown>>>(
    'SELECT * FROM memory_items WHERE session_id = ? ORDER BY created_at DESC',
    [sessionId]
  )
  return mapMemoryRow(rows)
}

/**
 * Delete a memory item
 */
export async function deleteMemoryItem(id: string): Promise<void> {
  const database = await initDatabase()
  await database.execute('DELETE FROM memory_items WHERE id = ?', [id])
}

/**
 * Get all memory items across sessions, optionally filtered by project.
 * Returns items sorted by created_at descending.
 */
export async function getAllMemoryItems(projectId?: string): Promise<MemoryItem[]> {
  const database = await initDatabase()
  const query = projectId
    ? `SELECT mi.* FROM memory_items mi
       JOIN sessions s ON mi.session_id = s.id
       WHERE s.project_id = ?
       ORDER BY mi.created_at DESC`
    : 'SELECT * FROM memory_items ORDER BY created_at DESC'
  const params = projectId ? [projectId] : []
  const rows = await database.select<Array<Record<string, unknown>>>(query, params)
  return mapMemoryRow(rows)
}

/**
 * Clear memory items for a session
 */
export async function clearMemoryItems(sessionId: string): Promise<void> {
  const database = await initDatabase()
  await database.execute('DELETE FROM memory_items WHERE session_id = ?', [sessionId])
}

// ============================================================================
// Helpers
// ============================================================================

/** Map database rows to MemoryItem objects */
function mapMemoryRow(rows: Array<Record<string, unknown>>): MemoryItem[] {
  return rows.map((row) => ({
    id: row.id as string,
    sessionId: row.session_id as string,
    type: row.type as MemoryItem['type'],
    title: row.title as string,
    preview: row.preview as string,
    tokens: row.tokens as number,
    createdAt: row.created_at as number,
    source: row.source as string | undefined,
  }))
}
