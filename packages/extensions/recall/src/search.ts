/**
 * Recall search — FTS5 MATCH queries with BM25 ranking.
 */

import type { IDatabase } from '@ava/core-v2/platform'
import type { RecallResult, RecallSearchOptions } from './types.js'

export class RecallSearch {
  constructor(private db: IDatabase) {}

  /**
   * Search indexed messages using FTS5 MATCH.
   * Returns results ranked by BM25 relevance score.
   */
  async search(query: string, options?: RecallSearchOptions): Promise<RecallResult[]> {
    const limit = options?.limit ?? 20
    const conditions: string[] = ['recall_fts MATCH ?']
    const params: (string | number)[] = [this.sanitizeQuery(query)]

    if (options?.sessionId) {
      conditions.push('session_id = ?')
      params.push(options.sessionId)
    }

    if (options?.role) {
      conditions.push('role = ?')
      params.push(options.role)
    }

    const where = conditions.join(' AND ')
    params.push(limit)

    const sql = `
      SELECT
        session_id,
        message_index,
        role,
        snippet(recall_fts, 3, '<mark>', '</mark>', '...', 40) as snippet,
        bm25(recall_fts) as rank
      FROM recall_fts
      WHERE ${where}
      ORDER BY rank
      LIMIT ?
    `

    const rows = await this.db.query<{
      session_id: string
      message_index: number
      role: string
      snippet: string
      rank: number
    }>(sql, params)

    return rows.map((row) => ({
      sessionId: row.session_id,
      messageIndex: row.message_index,
      role: row.role,
      snippet: row.snippet,
      rank: row.rank,
    }))
  }

  /**
   * Search across ancestor sessions (for branch-aware recall).
   * Uses the DAG's getAncestors to include parent chain.
   */
  async searchWithAncestors(
    query: string,
    sessionIds: string[],
    options?: Omit<RecallSearchOptions, 'sessionId'>
  ): Promise<RecallResult[]> {
    if (sessionIds.length === 0) return []

    const limit = options?.limit ?? 20
    const placeholders = sessionIds.map(() => '?').join(', ')
    const conditions = ['recall_fts MATCH ?', `session_id IN (${placeholders})`]
    const params: (string | number)[] = [this.sanitizeQuery(query), ...sessionIds]

    if (options?.role) {
      conditions.push('role = ?')
      params.push(options.role)
    }

    const where = conditions.join(' AND ')
    params.push(limit)

    const sql = `
      SELECT
        session_id,
        message_index,
        role,
        snippet(recall_fts, 3, '<mark>', '</mark>', '...', 40) as snippet,
        bm25(recall_fts) as rank
      FROM recall_fts
      WHERE ${where}
      ORDER BY rank
      LIMIT ?
    `

    const rows = await this.db.query<{
      session_id: string
      message_index: number
      role: string
      snippet: string
      rank: number
    }>(sql, params)

    return rows.map((row) => ({
      sessionId: row.session_id,
      messageIndex: row.message_index,
      role: row.role,
      snippet: row.snippet,
      rank: row.rank,
    }))
  }

  /** Sanitize FTS5 query — escape special characters. */
  private sanitizeQuery(query: string): string {
    // FTS5 uses double quotes for phrase queries, * for prefix
    // Strip characters that could break the query
    return query
      .replace(/[{}[\]()^~!\\]/g, ' ')
      .replace(/\s+/g, ' ')
      .trim()
  }
}
