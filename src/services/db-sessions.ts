/**
 * Database Session Operations
 *
 * CRUD for sessions including archive/delete and stats queries.
 */

import type { Session, SessionWithStats } from '../types'
import { initDatabase } from './db-init'

/**
 * Create a new session
 */
export async function createSession(
  name: string,
  projectId?: string,
  parentSessionId?: string,
  metadata?: Record<string, unknown>
): Promise<Session> {
  const database = await initDatabase()
  const id = crypto.randomUUID()
  const now = Date.now()

  await database.execute(
    'INSERT INTO sessions (id, name, project_id, parent_session_id, created_at, updated_at, status, metadata) VALUES (?, ?, ?, ?, ?, ?, ?, ?)',
    [
      id,
      name,
      projectId || null,
      parentSessionId || null,
      now,
      now,
      'active',
      metadata ? JSON.stringify(metadata) : null,
    ]
  )

  return {
    id,
    projectId,
    parentSessionId,
    name,
    createdAt: now,
    updatedAt: now,
    status: 'active',
    metadata,
  }
}

/** Map a session row to SessionWithStats */
function mapSessionRow(row: Record<string, unknown>): SessionWithStats {
  return {
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
  }
}

/**
 * Get sessions with computed stats (message count, total tokens, last preview)
 * @param projectId - Optional project ID to filter sessions
 */
export async function getSessionsWithStats(projectId?: string): Promise<SessionWithStats[]> {
  const database = await initDatabase()

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

  return rows.map(mapSessionRow)
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

  return rows.map(mapSessionRow)
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
  await database.execute('DELETE FROM sessions WHERE id = ?', [id])
}
