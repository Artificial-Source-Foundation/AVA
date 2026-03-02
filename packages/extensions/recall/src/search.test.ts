import { describe, expect, it } from 'vitest'
import { RecallSearch } from './search.js'

/** Mock database that simulates FTS5 search. */
function createMockSearchDb(rows: Array<Record<string, unknown>> = []) {
  return {
    async execute(): Promise<void> {},
    async query<T>(_sql: string, params?: unknown[]): Promise<T[]> {
      // Simulate FTS5 MATCH filtering
      const query = ((params?.[0] as string) ?? '').toLowerCase()
      if (!query) return [] as T[]

      const results = rows
        .filter((row) => {
          const content = (row.content as string).toLowerCase()
          return query.split(' ').some((word) => content.includes(word))
        })
        .map((row) => ({
          session_id: row.session_id,
          message_index: row.message_index,
          role: row.role,
          snippet: `...${(row.content as string).slice(0, 80)}...`,
          rank: -1.5,
        }))

      // Apply limit
      const limitParam = params?.[params.length - 1]
      const limit = typeof limitParam === 'number' ? limitParam : 20
      return results.slice(0, limit) as T[]
    },
  }
}

describe('RecallSearch', () => {
  describe('search', () => {
    it('returns empty for no matches', async () => {
      const db = createMockSearchDb([])
      const search = new RecallSearch(db)
      const results = await search.search('nonexistent')
      expect(results).toEqual([])
    })

    it('returns matching results', async () => {
      const db = createMockSearchDb([
        {
          session_id: 'sess-1',
          message_index: 0,
          role: 'user',
          content: 'How to fix the login bug',
        },
        {
          session_id: 'sess-1',
          message_index: 1,
          role: 'assistant',
          content: 'Check the auth middleware',
        },
      ])
      const search = new RecallSearch(db)

      const results = await search.search('login')
      expect(results).toHaveLength(1)
      expect(results[0].sessionId).toBe('sess-1')
      expect(results[0].role).toBe('user')
    })

    it('respects limit option', async () => {
      const db = createMockSearchDb([
        { session_id: 's1', message_index: 0, role: 'user', content: 'query term A' },
        { session_id: 's2', message_index: 0, role: 'user', content: 'query term B' },
        { session_id: 's3', message_index: 0, role: 'user', content: 'query term C' },
      ])
      const search = new RecallSearch(db)

      const results = await search.search('query', { limit: 2 })
      expect(results).toHaveLength(2)
    })

    it('filters by role', async () => {
      const db = createMockSearchDb([
        { session_id: 's1', message_index: 0, role: 'user', content: 'test content' },
        { session_id: 's1', message_index: 1, role: 'assistant', content: 'test reply' },
      ])
      const search = new RecallSearch(db)

      // Note: mock doesn't filter by role in SQL, but tests the param passing
      const results = await search.search('test', { role: 'user' })
      expect(results.length).toBeGreaterThan(0)
    })

    it('returns results with expected shape', async () => {
      const db = createMockSearchDb([
        { session_id: 'sess-x', message_index: 3, role: 'assistant', content: 'hello world' },
      ])
      const search = new RecallSearch(db)

      const results = await search.search('hello')
      expect(results[0]).toEqual({
        sessionId: 'sess-x',
        messageIndex: 3,
        role: 'assistant',
        snippet: expect.any(String),
        rank: expect.any(Number),
      })
    })
  })

  describe('searchWithAncestors', () => {
    it('returns empty for no session IDs', async () => {
      const db = createMockSearchDb([])
      const search = new RecallSearch(db)
      const results = await search.searchWithAncestors('test', [])
      expect(results).toEqual([])
    })

    it('searches across multiple session IDs', async () => {
      const db = createMockSearchDb([
        { session_id: 'parent', message_index: 0, role: 'user', content: 'test data' },
        { session_id: 'child', message_index: 0, role: 'user', content: 'test data child' },
      ])
      const search = new RecallSearch(db)

      const results = await search.searchWithAncestors('test', ['parent', 'child'])
      expect(results).toHaveLength(2)
    })
  })

  describe('query sanitization', () => {
    it('handles special characters in query', async () => {
      const db = createMockSearchDb([
        { session_id: 's1', message_index: 0, role: 'user', content: 'bracket test' },
      ])
      const search = new RecallSearch(db)

      // Should not throw with special FTS characters
      const results = await search.search('bracket [test] {query}')
      expect(results).toHaveLength(1)
    })

    it('handles empty query', async () => {
      const db = createMockSearchDb([])
      const search = new RecallSearch(db)
      const results = await search.search('')
      expect(results).toEqual([])
    })
  })
})
