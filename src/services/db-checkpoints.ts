/**
 * Database Checkpoint Operations
 *
 * Queries for checkpoint memory items used by the rollback system.
 */

import { initDatabase } from './db-init'

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
