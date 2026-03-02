/**
 * Recall indexer — indexes session messages into FTS5 for full-text search.
 *
 * Uses SQLite FTS5 virtual table with porter stemmer for
 * good-enough text search without external embedding models.
 */

import type { IDatabase } from '@ava/core-v2/platform'
import type { SessionState } from '@ava/core-v2/session'
import type { RecallIndexEntry } from './types.js'

const CREATE_FTS_TABLE = `
  CREATE VIRTUAL TABLE IF NOT EXISTS recall_fts USING fts5(
    session_id,
    message_index,
    role,
    content,
    tokenize='porter unicode61'
  )
`

/** Extract plain text from message content (handles string and block arrays). */
function extractText(content: unknown): string {
  if (typeof content === 'string') return content
  if (!Array.isArray(content)) return ''

  return content
    .map((block: Record<string, unknown>) => {
      if (block.type === 'text') return block.text as string
      if (block.type === 'tool_result') return block.content as string
      return ''
    })
    .filter(Boolean)
    .join('\n')
}

export class RecallIndexer {
  private initialized = false

  constructor(private db: IDatabase) {}

  async init(): Promise<void> {
    if (this.initialized) return
    await this.db.execute(CREATE_FTS_TABLE)
    this.initialized = true
  }

  /** Index all messages from a session. Replaces existing entries. */
  async indexSession(session: SessionState): Promise<number> {
    await this.init()

    // Remove existing entries for this session
    await this.db.execute('DELETE FROM recall_fts WHERE session_id = ?', [session.id])

    let indexed = 0
    for (let i = 0; i < session.messages.length; i++) {
      const msg = session.messages[i]!
      const text = extractText(msg.content)
      if (!text.trim()) continue

      await this.db.execute(
        'INSERT INTO recall_fts (session_id, message_index, role, content) VALUES (?, ?, ?, ?)',
        [session.id, String(i), msg.role, text]
      )
      indexed++
    }

    return indexed
  }

  /** Index a single message entry. */
  async indexEntry(entry: RecallIndexEntry): Promise<void> {
    await this.init()
    if (!entry.content.trim()) return

    await this.db.execute(
      'INSERT INTO recall_fts (session_id, message_index, role, content) VALUES (?, ?, ?, ?)',
      [entry.sessionId, String(entry.messageIndex), entry.role, entry.content]
    )
  }

  /** Remove all entries for a session. */
  async removeSession(sessionId: string): Promise<void> {
    await this.init()
    await this.db.execute('DELETE FROM recall_fts WHERE session_id = ?', [sessionId])
  }

  /** Reindex all provided sessions. */
  async reindexAll(sessions: SessionState[]): Promise<number> {
    await this.init()
    await this.db.execute('DELETE FROM recall_fts')

    let total = 0
    for (const session of sessions) {
      total += await this.indexSession(session)
    }
    return total
  }

  /** Get total indexed entry count. */
  async count(): Promise<number> {
    await this.init()
    const rows = await this.db.query<{ cnt: number }>('SELECT count(*) as cnt FROM recall_fts')
    return rows[0]?.cnt ?? 0
  }
}
