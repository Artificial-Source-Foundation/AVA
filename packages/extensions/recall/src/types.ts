/**
 * Recall types — search results, index entries.
 */

export interface RecallSearchOptions {
  limit?: number
  sessionId?: string
  role?: 'user' | 'assistant'
  includeBranches?: boolean
}

export interface RecallResult {
  sessionId: string
  messageIndex: number
  role: string
  snippet: string
  rank: number
  sessionName?: string
}

export interface RecallIndexEntry {
  sessionId: string
  messageIndex: number
  role: string
  content: string
}
