/**
 * Database Service
 * SQLite operations for sessions, messages, agents, and file changes
 */

import Database from '@tauri-apps/plugin-sql'
import type {
  Agent,
  FileOperation,
  MemoryItem,
  Message,
  Session,
  SessionWithStats,
  TerminalExecution,
} from '../types'
import { runMigrations } from './migrations'

let db: Database | null = null

// ============================================================================
// Database Initialization
// ============================================================================

/**
 * Initialize database connection and run migrations
 */
export async function initDatabase(): Promise<Database> {
  if (db) return db

  db = await Database.load('sqlite:ava.db')
  await runMigrations(db)

  return db
}

/**
 * Get the raw database instance (for adapters like DesktopSessionStorage).
 * Initializes if not already done.
 */
export async function getDb(): Promise<Database> {
  return initDatabase()
}

// ============================================================================
// Session Operations
// ============================================================================

/**
 * Create a new session
 */
export async function createSession(
  name: string,
  projectId?: string,
  parentSessionId?: string
): Promise<Session> {
  const database = await initDatabase()
  const id = crypto.randomUUID()
  const now = Date.now()

  await database.execute(
    'INSERT INTO sessions (id, name, project_id, parent_session_id, created_at, updated_at, status) VALUES (?, ?, ?, ?, ?, ?, ?)',
    [id, name, projectId || null, parentSessionId || null, now, now, 'active']
  )

  return { id, projectId, parentSessionId, name, createdAt: now, updatedAt: now, status: 'active' }
}

/**
 * Get sessions with computed stats (message count, total tokens, last preview)
 * @param projectId - Optional project ID to filter sessions
 */
export async function getSessionsWithStats(projectId?: string): Promise<SessionWithStats[]> {
  const database = await initDatabase()

  // Build WHERE clause based on projectId
  let whereClause = "WHERE s.status != 'archived'"
  const params: unknown[] = []

  if (projectId) {
    whereClause += ' AND s.project_id = ?'
    params.push(projectId)
  }

  const rows = await database.select<Array<Record<string, unknown>>>(
    `
    SELECT
      s.*,
      COUNT(m.id) as message_count,
      COALESCE(SUM(m.tokens_used), 0) as total_tokens,
      COALESCE(SUM(m.cost_usd), 0) as total_cost,
      (SELECT content FROM messages WHERE session_id = s.id ORDER BY created_at DESC LIMIT 1) as last_preview
    FROM sessions s
    LEFT JOIN messages m ON m.session_id = s.id
    ${whereClause}
    GROUP BY s.id
    ORDER BY s.updated_at DESC
  `,
    params
  )

  return rows.map((row) => ({
    id: row.id as string,
    projectId: (row.project_id as string) || undefined,
    parentSessionId: (row.parent_session_id as string) || undefined,
    slug: (row.slug as string) || undefined,
    busySince: (row.busy_since as number | null) ?? undefined,
    name: row.name as string,
    createdAt: row.created_at as number,
    updatedAt: row.updated_at as number,
    status: row.status as Session['status'],
    metadata: row.metadata ? JSON.parse(row.metadata as string) : undefined,
    messageCount: (row.message_count as number) || 0,
    totalTokens: (row.total_tokens as number) || 0,
    lastPreview: row.last_preview as string | undefined,
  }))
}

/**
 * Update session fields
 */
export async function updateSession(
  id: string,
  updates: Partial<Pick<Session, 'name' | 'status' | 'metadata' | 'slug' | 'busySince'>>
): Promise<void> {
  const database = await initDatabase()
  const setClauses: string[] = ['updated_at = ?']
  const values: unknown[] = [Date.now()]

  if (updates.name !== undefined) {
    setClauses.push('name = ?')
    values.push(updates.name)
  }
  if (updates.status !== undefined) {
    setClauses.push('status = ?')
    values.push(updates.status)
  }
  if (updates.metadata !== undefined) {
    setClauses.push('metadata = ?')
    values.push(JSON.stringify(updates.metadata))
  }
  if (updates.slug !== undefined) {
    setClauses.push('slug = ?')
    values.push(updates.slug)
  }
  if ('busySince' in updates) {
    setClauses.push('busy_since = ?')
    values.push(updates.busySince ?? null)
  }

  values.push(id)
  await database.execute(`UPDATE sessions SET ${setClauses.join(', ')} WHERE id = ?`, values)
}

/**
 * Get archived sessions with stats
 */
export async function getArchivedSessions(projectId?: string): Promise<SessionWithStats[]> {
  const database = await initDatabase()

  let whereClause = "WHERE s.status = 'archived'"
  const params: unknown[] = []

  if (projectId) {
    whereClause += ' AND s.project_id = ?'
    params.push(projectId)
  }

  const rows = await database.select<Array<Record<string, unknown>>>(
    `
    SELECT
      s.*,
      COUNT(m.id) as message_count,
      COALESCE(SUM(m.tokens_used), 0) as total_tokens,
      COALESCE(SUM(m.cost_usd), 0) as total_cost,
      (SELECT content FROM messages WHERE session_id = s.id ORDER BY created_at DESC LIMIT 1) as last_preview
    FROM sessions s
    LEFT JOIN messages m ON m.session_id = s.id
    ${whereClause}
    GROUP BY s.id
    ORDER BY s.updated_at DESC
  `,
    params
  )

  return rows.map((row) => ({
    id: row.id as string,
    projectId: (row.project_id as string) || undefined,
    parentSessionId: (row.parent_session_id as string) || undefined,
    slug: (row.slug as string) || undefined,
    busySince: (row.busy_since as number | null) ?? undefined,
    name: row.name as string,
    createdAt: row.created_at as number,
    updatedAt: row.updated_at as number,
    status: row.status as Session['status'],
    metadata: row.metadata ? JSON.parse(row.metadata as string) : undefined,
    messageCount: (row.message_count as number) || 0,
    totalTokens: (row.total_tokens as number) || 0,
    lastPreview: row.last_preview as string | undefined,
  }))
}

/**
 * Update session's updated_at timestamp
 */
async function touchSession(id: string): Promise<void> {
  const database = await initDatabase()
  await database.execute('UPDATE sessions SET updated_at = ? WHERE id = ?', [Date.now(), id])
}

/**
 * Archive a session (soft delete)
 */
export async function archiveSession(id: string): Promise<void> {
  await updateSession(id, { status: 'archived' })
}

/**
 * Delete a session permanently (cascades to messages)
 */
export async function deleteSession(id: string): Promise<void> {
  const database = await initDatabase()
  // Messages will be cascade deleted due to foreign key
  await database.execute('DELETE FROM sessions WHERE id = ?', [id])
}

// ============================================================================
// Message Operations
// ============================================================================

/**
 * Save a new message
 */
export async function saveMessage(message: Omit<Message, 'id' | 'createdAt'>): Promise<Message> {
  const database = await initDatabase()
  const id = crypto.randomUUID()
  const createdAt = Date.now()

  await database.execute(
    `INSERT INTO messages (id, session_id, role, content, agent_id, created_at, tokens_used, metadata, cost_usd, model)
     VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)`,
    [
      id,
      message.sessionId,
      message.role,
      message.content,
      message.agentId || null,
      createdAt,
      message.tokensUsed || 0,
      JSON.stringify(message.metadata || {}),
      message.costUSD || null,
      message.model || null,
    ]
  )

  // Update session's updated_at
  await touchSession(message.sessionId)

  return { ...message, id, createdAt }
}

/**
 * Get all messages for a session
 */
export async function getMessages(sessionId: string): Promise<Message[]> {
  const database = await initDatabase()
  const rows = await database.select<Array<Record<string, unknown>>>(
    'SELECT * FROM messages WHERE session_id = ? ORDER BY created_at ASC',
    [sessionId]
  )
  return mapDbMessages(rows)
}

/**
 * Duplicate all messages from one session to another
 */
export async function duplicateSessionMessages(
  sourceSessionId: string,
  targetSessionId: string
): Promise<void> {
  const database = await initDatabase()
  await database.execute(
    `INSERT INTO messages (id, session_id, role, content, agent_id, created_at, tokens_used, metadata)
     SELECT hex(randomblob(16)), ?, role, content, agent_id, created_at, tokens_used, metadata
     FROM messages WHERE session_id = ? ORDER BY created_at ASC`,
    [targetSessionId, sourceSessionId]
  )
}

/**
 * Update a message
 */
export async function updateMessage(
  id: string,
  updates: Partial<Pick<Message, 'content' | 'tokensUsed' | 'metadata'>>
): Promise<void> {
  const database = await initDatabase()

  const setClauses: string[] = []
  const values: unknown[] = []

  if (updates.content !== undefined) {
    setClauses.push('content = ?')
    values.push(updates.content)
  }
  if (updates.tokensUsed !== undefined) {
    setClauses.push('tokens_used = ?')
    values.push(updates.tokensUsed)
  }
  if (updates.metadata !== undefined) {
    setClauses.push('metadata = ?')
    values.push(JSON.stringify(updates.metadata))
  }

  if (setClauses.length === 0) return

  values.push(id)
  await database.execute(`UPDATE messages SET ${setClauses.join(', ')} WHERE id = ?`, values)
}

/**
 * Delete a single message by ID
 */
export async function deleteMessageFromDb(id: string): Promise<void> {
  const database = await initDatabase()
  await database.execute('DELETE FROM messages WHERE id = ?', [id])
}

/**
 * Delete all messages in a session created at or after a given timestamp.
 * Used for conversation rollback.
 */
export async function deleteMessagesFromTimestamp(
  sessionId: string,
  fromCreatedAt: number
): Promise<number> {
  const database = await initDatabase()
  const result = await database.execute(
    'DELETE FROM messages WHERE session_id = ? AND created_at >= ?',
    [sessionId, fromCreatedAt]
  )
  return result.rowsAffected
}

// ============================================================================
// Helper Functions
// ============================================================================

/**
 * Map database rows to Message objects
 */
function mapDbMessages(rows: Array<Record<string, unknown>>): Message[] {
  return rows.map((row) => {
    const metadata = row.metadata
      ? (JSON.parse(row.metadata as string) as Record<string, unknown>)
      : undefined
    return {
      id: row.id as string,
      sessionId: row.session_id as string,
      role: row.role as Message['role'],
      content: row.content as string,
      agentId: row.agent_id as string | undefined,
      createdAt: row.created_at as number,
      tokensUsed: row.tokens_used as number | undefined,
      costUSD: (row.cost_usd as number | null) ?? undefined,
      model: (row.model as string | null) ?? undefined,
      metadata,
      toolCalls: metadata?.toolCalls as Message['toolCalls'],
    }
  })
}

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

/**
 * Clear memory items for a session
 */
export async function clearMemoryItems(sessionId: string): Promise<void> {
  const database = await initDatabase()
  await database.execute('DELETE FROM memory_items WHERE session_id = ?', [sessionId])
}

/**
 * Delete all messages for a session (used by checkpoint rollback)
 */
export async function deleteSessionMessages(sessionId: string): Promise<void> {
  const database = await initDatabase()
  await database.execute('DELETE FROM messages WHERE session_id = ?', [sessionId])
}

/**
 * Insert multiple messages in a batch (used by checkpoint restore)
 */
export async function insertMessages(msgs: Message[]): Promise<void> {
  const database = await initDatabase()
  for (const msg of msgs) {
    await database.execute(
      `INSERT INTO messages (id, session_id, role, content, agent_id, created_at, tokens_used, metadata, cost_usd, model)
       VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)`,
      [
        msg.id,
        msg.sessionId,
        msg.role,
        msg.content,
        msg.agentId || null,
        msg.createdAt,
        msg.tokensUsed || 0,
        JSON.stringify(msg.metadata || {}),
        msg.costUSD || null,
        msg.model || null,
      ]
    )
  }
}

// ============================================================================
// Agent Operations
// ============================================================================

/**
 * Save a new agent
 */
export async function saveAgent(agent: Agent): Promise<void> {
  const database = await initDatabase()
  await database.execute(
    `INSERT INTO agents (id, session_id, type, status, model, created_at, completed_at, assigned_files, task_description, result)
     VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)`,
    [
      agent.id,
      agent.sessionId,
      agent.type,
      agent.status,
      agent.model,
      agent.createdAt,
      agent.completedAt || null,
      agent.assignedFiles ? JSON.stringify(agent.assignedFiles) : null,
      agent.taskDescription || null,
      agent.result ? JSON.stringify(agent.result) : null,
    ]
  )
}

/**
 * Get all agents for a session
 */
export async function getAgents(sessionId: string): Promise<Agent[]> {
  const database = await initDatabase()
  const rows = await database.select<Array<Record<string, unknown>>>(
    'SELECT * FROM agents WHERE session_id = ? ORDER BY created_at ASC',
    [sessionId]
  )
  return rows.map((row) => ({
    id: row.id as string,
    sessionId: row.session_id as string,
    type: row.type as Agent['type'],
    status: row.status as Agent['status'],
    model: row.model as string,
    createdAt: row.created_at as number,
    completedAt: (row.completed_at as number | null) ?? undefined,
    assignedFiles: row.assigned_files
      ? (JSON.parse(row.assigned_files as string) as string[])
      : undefined,
    taskDescription: (row.task_description as string | null) ?? undefined,
    result: row.result ? (JSON.parse(row.result as string) as Agent['result']) : undefined,
  }))
}

/**
 * Update an agent's properties
 */
export async function updateAgentInDb(
  id: string,
  updates: Partial<Pick<Agent, 'status' | 'completedAt' | 'result' | 'taskDescription'>>
): Promise<void> {
  const database = await initDatabase()
  const setClauses: string[] = []
  const values: unknown[] = []

  if (updates.status !== undefined) {
    setClauses.push('status = ?')
    values.push(updates.status)
  }
  if (updates.completedAt !== undefined) {
    setClauses.push('completed_at = ?')
    values.push(updates.completedAt)
  }
  if (updates.result !== undefined) {
    setClauses.push('result = ?')
    values.push(JSON.stringify(updates.result))
  }
  if (updates.taskDescription !== undefined) {
    setClauses.push('task_description = ?')
    values.push(updates.taskDescription)
  }

  if (setClauses.length === 0) return

  values.push(id)
  await database.execute(`UPDATE agents SET ${setClauses.join(', ')} WHERE id = ?`, values)
}

// ============================================================================
// Project Usage Stats
// ============================================================================

export interface ProjectUsageStats {
  totalTokens: number
  totalCost: number
  sessionCount: number
  messageCount: number
}

export interface ModelBreakdownEntry {
  model: string
  usageCount: number
  totalTokens: number
  totalCost: number
}

export interface DailyUsageEntry {
  date: string
  tokens: number
  cost: number
  messages: number
}

/** Aggregate usage stats across all sessions for a project */
export async function getProjectUsageStats(projectId: string): Promise<ProjectUsageStats> {
  const database = await initDatabase()
  const rows = await database.select<Array<Record<string, unknown>>>(
    `SELECT
      COALESCE(SUM(m.tokens_used), 0) as total_tokens,
      COALESCE(SUM(m.cost_usd), 0) as total_cost,
      COUNT(DISTINCT s.id) as session_count,
      COUNT(m.id) as message_count
    FROM sessions s
    LEFT JOIN messages m ON m.session_id = s.id
    WHERE s.project_id = ?`,
    [projectId]
  )
  const row = rows[0] || {}
  return {
    totalTokens: (row.total_tokens as number) || 0,
    totalCost: (row.total_cost as number) || 0,
    sessionCount: (row.session_count as number) || 0,
    messageCount: (row.message_count as number) || 0,
  }
}

/** Group usage by model for a project */
export async function getModelBreakdown(projectId: string): Promise<ModelBreakdownEntry[]> {
  const database = await initDatabase()
  const rows = await database.select<Array<Record<string, unknown>>>(
    `SELECT
      COALESCE(m.model, 'unknown') as model,
      COUNT(*) as usage_count,
      COALESCE(SUM(m.tokens_used), 0) as total_tokens,
      COALESCE(SUM(m.cost_usd), 0) as total_cost
    FROM messages m
    JOIN sessions s ON s.id = m.session_id
    WHERE s.project_id = ? AND m.model IS NOT NULL
    GROUP BY m.model
    ORDER BY total_tokens DESC`,
    [projectId]
  )
  return rows.map((row) => ({
    model: row.model as string,
    usageCount: (row.usage_count as number) || 0,
    totalTokens: (row.total_tokens as number) || 0,
    totalCost: (row.total_cost as number) || 0,
  }))
}

/** Daily usage aggregates for the last N days */
export async function getDailyUsage(projectId: string, days = 30): Promise<DailyUsageEntry[]> {
  const database = await initDatabase()
  const cutoff = Date.now() - days * 24 * 60 * 60 * 1000
  const rows = await database.select<Array<Record<string, unknown>>>(
    `SELECT
      date(m.created_at / 1000, 'unixepoch') as date,
      COALESCE(SUM(m.tokens_used), 0) as tokens,
      COALESCE(SUM(m.cost_usd), 0) as cost,
      COUNT(*) as messages
    FROM messages m
    JOIN sessions s ON s.id = m.session_id
    WHERE s.project_id = ? AND m.created_at >= ?
    GROUP BY date(m.created_at / 1000, 'unixepoch')
    ORDER BY date ASC`,
    [projectId, cutoff]
  )
  return rows.map((row) => ({
    date: row.date as string,
    tokens: (row.tokens as number) || 0,
    cost: (row.cost as number) || 0,
    messages: (row.messages as number) || 0,
  }))
}

// ============================================================================
// Checkpoint Operations
// ============================================================================

/**
 * Get checkpoint memory items for a session
 */
export async function getCheckpoints(
  sessionId: string
): Promise<Array<{ id: string; timestamp: number; description: string; messageCount: number }>> {
  const database = await initDatabase()
  const rows = await database.select<Array<Record<string, unknown>>>(
    "SELECT * FROM memory_items WHERE session_id = ? AND type = 'checkpoint' ORDER BY created_at ASC",
    [sessionId]
  )
  return rows.map((row) => {
    let messageCount = 0
    try {
      const data = JSON.parse(row.preview as string) as { messages?: unknown[] }
      messageCount = data.messages?.length ?? 0
    } catch {
      /* ignore */
    }
    return {
      id: row.id as string,
      timestamp: row.created_at as number,
      description: row.title as string,
      messageCount,
    }
  })
}
