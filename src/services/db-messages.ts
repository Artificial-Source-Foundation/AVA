/**
 * Database Message Operations
 *
 * CRUD for messages within sessions. Updates session timestamps via touchSession.
 */

import type { Message } from '../types'
import { initDatabase, touchSession } from './db-init'

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
      JSON.stringify(buildPersistedMessageMetadata(message)),
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
    `INSERT INTO messages (id, session_id, role, content, agent_id, created_at, tokens_used, metadata, cost_usd, model)
     SELECT hex(randomblob(16)), ?, role, content, agent_id, created_at, tokens_used, metadata, cost_usd, model
     FROM messages WHERE session_id = ? ORDER BY created_at ASC`,
    [targetSessionId, sourceSessionId]
  )
}

/**
 * Update a message — supports content, tokensUsed, costUSD, toolCalls, images, error, and metadata.
 *
 * toolCalls/images are merged into the metadata JSON column (same convention as insertMessages)
 * so they survive session reload. error is also stored in metadata under `_error`.
 */
export async function updateMessage(
  id: string,
  updates: Partial<
    Pick<
      Message,
      'content' | 'tokensUsed' | 'costUSD' | 'toolCalls' | 'images' | 'error' | 'metadata'
    >
  >
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
  if (updates.costUSD !== undefined) {
    setClauses.push('cost_usd = ?')
    values.push(updates.costUSD)
  }

  // toolCalls, images, and error are stored inside the metadata JSON column
  if (
    updates.metadata !== undefined ||
    updates.toolCalls !== undefined ||
    updates.images !== undefined ||
    updates.error !== undefined
  ) {
    // We need to merge with existing metadata — fetch current metadata first
    const rows = await database.select<Array<{ metadata: string | null }>>(
      'SELECT metadata FROM messages WHERE id = ?',
      [id]
    )
    const existing: Record<string, unknown> = rows[0]?.metadata
      ? (JSON.parse(rows[0].metadata) as Record<string, unknown>)
      : {}

    const merged: Record<string, unknown> = { ...existing, ...updates.metadata }
    if (updates.toolCalls && updates.toolCalls.length > 0) {
      merged.toolCalls = updates.toolCalls
    }
    if (updates.images !== undefined) {
      merged.images = updates.images
    }
    if (updates.error !== undefined) {
      merged._error = updates.error
    }

    setClauses.push('metadata = ?')
    values.push(JSON.stringify(merged))
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
        JSON.stringify(buildPersistedMessageMetadata(msg)),
        msg.costUSD || null,
        msg.model || null,
      ]
    )
  }
}

// ============================================================================
// Helpers
// ============================================================================

function buildPersistedMessageMetadata(
  message: Pick<Message, 'metadata' | 'toolCalls' | 'images'>
): Record<string, unknown> {
  return {
    ...message.metadata,
    ...(message.toolCalls && message.toolCalls.length > 0 ? { toolCalls: message.toolCalls } : {}),
    ...(message.images && message.images.length > 0 ? { images: message.images } : {}),
  }
}

/** Map database rows to Message objects */
function mapDbMessages(rows: Array<Record<string, unknown>>): Message[] {
  return rows.map((row) => {
    let metadata: Record<string, unknown> | undefined
    if (typeof row.metadata === 'string' && row.metadata.trim()) {
      try {
        metadata = JSON.parse(row.metadata) as Record<string, unknown>
      } catch {
        metadata = undefined
      }
    }
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
      images: metadata?.images as Message['images'],
      toolCalls: metadata?.toolCalls as Message['toolCalls'],
    }
  })
}
