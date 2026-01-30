/**
 * Database Service
 * SQLite operations for sessions, messages, agents, and file changes
 */

import Database from '@tauri-apps/plugin-sql'
import type { Message, Session, SessionWithStats } from '../types'
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

  db = await Database.load('sqlite:estela.db')
  await runMigrations(db)

  return db
}

// ============================================================================
// Session Operations
// ============================================================================

/**
 * Create a new session
 */
export async function createSession(name: string): Promise<Session> {
  const database = await initDatabase()
  const id = crypto.randomUUID()
  const now = Date.now()

  await database.execute(
    'INSERT INTO sessions (id, name, created_at, updated_at, status) VALUES (?, ?, ?, ?, ?)',
    [id, name, now, now, 'active']
  )

  return { id, name, createdAt: now, updatedAt: now, status: 'active' }
}

/**
 * Get sessions with computed stats (message count, total tokens, last preview)
 */
export async function getSessionsWithStats(): Promise<SessionWithStats[]> {
  const database = await initDatabase()
  const rows = await database.select<Array<Record<string, unknown>>>(`
    SELECT
      s.*,
      COUNT(m.id) as message_count,
      COALESCE(SUM(m.tokens_used), 0) as total_tokens,
      (SELECT content FROM messages WHERE session_id = s.id ORDER BY created_at DESC LIMIT 1) as last_preview
    FROM sessions s
    LEFT JOIN messages m ON m.session_id = s.id
    WHERE s.status != 'archived'
    GROUP BY s.id
    ORDER BY s.updated_at DESC
  `)

  return rows.map((row) => ({
    id: row.id as string,
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
  updates: Partial<Pick<Session, 'name' | 'status' | 'metadata'>>
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

  values.push(id)
  await database.execute(`UPDATE sessions SET ${setClauses.join(', ')} WHERE id = ?`, values)
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
    `INSERT INTO messages (id, session_id, role, content, agent_id, created_at, tokens_used, metadata)
     VALUES (?, ?, ?, ?, ?, ?, ?, ?)`,
    [
      id,
      message.sessionId,
      message.role,
      message.content,
      message.agentId || null,
      createdAt,
      message.tokensUsed || 0,
      JSON.stringify(message.metadata || {}),
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

// ============================================================================
// Helper Functions
// ============================================================================

/**
 * Map database rows to Message objects
 */
function mapDbMessages(rows: Array<Record<string, unknown>>): Message[] {
  return rows.map((row) => ({
    id: row.id as string,
    sessionId: row.session_id as string,
    role: row.role as Message['role'],
    content: row.content as string,
    agentId: row.agent_id as string | undefined,
    createdAt: row.created_at as number,
    tokensUsed: row.tokens_used as number | undefined,
    metadata: row.metadata ? JSON.parse(row.metadata as string) : undefined,
  }))
}
