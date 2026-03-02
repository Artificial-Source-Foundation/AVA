import type { SessionState } from '@ava/core-v2/session'
import { beforeEach, describe, expect, it } from 'vitest'
import { RecallIndexer } from './indexer.js'

/** In-memory SQLite-like database for testing. */
function createMockDatabase() {
  const tables = new Map<string, Array<Record<string, unknown>>>()

  return {
    async execute(sql: string, params?: unknown[]): Promise<void> {
      // Track FTS table creation
      if (sql.includes('CREATE VIRTUAL TABLE')) {
        tables.set('recall_fts', [])
        return
      }

      // Handle DELETE
      if (sql.startsWith('DELETE FROM recall_fts')) {
        const table = tables.get('recall_fts') ?? []
        if (params?.[0]) {
          const filtered = table.filter((row) => row.session_id !== params[0])
          tables.set('recall_fts', filtered)
        } else {
          tables.set('recall_fts', [])
        }
        return
      }

      // Handle INSERT
      if (sql.startsWith('INSERT INTO recall_fts')) {
        const table = tables.get('recall_fts') ?? []
        table.push({
          session_id: params?.[0],
          message_index: params?.[1],
          role: params?.[2],
          content: params?.[3],
        })
        tables.set('recall_fts', table)
      }
    },

    async query<T>(sql: string, _params?: unknown[]): Promise<T[]> {
      if (sql.includes('count(*)')) {
        const table = tables.get('recall_fts') ?? []
        return [{ cnt: table.length } as unknown as T]
      }
      return []
    },

    getTable(name: string) {
      return tables.get(name) ?? []
    },
  }
}

function createMockSession(
  id: string,
  messages: Array<{ role: string; content: string }>
): SessionState {
  return {
    id,
    messages: messages.map((m) => ({ role: m.role as 'user' | 'assistant', content: m.content })),
    workingDirectory: '/tmp',
    toolCallCount: 0,
    tokenStats: { inputTokens: 0, outputTokens: 0, messages: new Map() },
    openFiles: new Map(),
    env: {},
    createdAt: Date.now(),
    updatedAt: Date.now(),
    status: 'active',
  }
}

describe('RecallIndexer', () => {
  let db: ReturnType<typeof createMockDatabase>
  let indexer: RecallIndexer

  beforeEach(() => {
    db = createMockDatabase()
    indexer = new RecallIndexer(db)
  })

  describe('init', () => {
    it('creates FTS5 table', async () => {
      await indexer.init()
      expect(db.getTable('recall_fts')).toBeDefined()
    })

    it('is idempotent', async () => {
      await indexer.init()
      await indexer.init() // should not throw
    })
  })

  describe('indexSession', () => {
    it('indexes all messages from a session', async () => {
      const session = createMockSession('sess-1', [
        { role: 'user', content: 'Hello world' },
        { role: 'assistant', content: 'Hi there!' },
      ])

      const count = await indexer.indexSession(session)
      expect(count).toBe(2)
    })

    it('skips empty messages', async () => {
      const session = createMockSession('sess-2', [
        { role: 'user', content: 'Hello' },
        { role: 'assistant', content: '' },
        { role: 'user', content: '  ' },
      ])

      const count = await indexer.indexSession(session)
      expect(count).toBe(1)
    })

    it('replaces existing entries on re-index', async () => {
      const session = createMockSession('sess-3', [{ role: 'user', content: 'First version' }])

      await indexer.indexSession(session)
      session.messages.push({ role: 'assistant', content: 'Updated' })
      const count = await indexer.indexSession(session)

      expect(count).toBe(2)
    })

    it('handles content blocks (arrays)', async () => {
      const session: SessionState = {
        ...createMockSession('sess-4', []),
        messages: [
          {
            role: 'assistant',
            content: [
              { type: 'text', text: 'Some text' },
              { type: 'tool_use', id: 't1', name: 'read', input: {} },
            ] as never,
          },
        ],
      }

      const count = await indexer.indexSession(session)
      expect(count).toBe(1)
    })
  })

  describe('indexEntry', () => {
    it('indexes a single entry', async () => {
      await indexer.indexEntry({
        sessionId: 'sess-5',
        messageIndex: 0,
        role: 'user',
        content: 'Test content',
      })

      const count = await indexer.count()
      expect(count).toBe(1)
    })

    it('skips empty content', async () => {
      await indexer.indexEntry({
        sessionId: 'sess-6',
        messageIndex: 0,
        role: 'user',
        content: '   ',
      })

      const count = await indexer.count()
      expect(count).toBe(0)
    })
  })

  describe('removeSession', () => {
    it('removes all entries for a session', async () => {
      const session = createMockSession('sess-7', [{ role: 'user', content: 'Hello' }])

      await indexer.indexSession(session)
      await indexer.removeSession('sess-7')

      const count = await indexer.count()
      expect(count).toBe(0)
    })
  })

  describe('reindexAll', () => {
    it('clears and reindexes all sessions', async () => {
      const s1 = createMockSession('sess-a', [{ role: 'user', content: 'A' }])
      const s2 = createMockSession('sess-b', [{ role: 'user', content: 'B' }])

      const total = await indexer.reindexAll([s1, s2])
      expect(total).toBe(2)
    })
  })

  describe('count', () => {
    it('returns 0 for empty index', async () => {
      const count = await indexer.count()
      expect(count).toBe(0)
    })
  })
})
