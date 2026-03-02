/**
 * Recall extension — semantic search across session history.
 *
 * Uses SQLite FTS5 for full-text search with porter stemmer.
 * Indexes session messages on agent:finish, provides recall tool
 * and /recall slash command.
 */

import type { Disposable, ExtensionAPI } from '@ava/core-v2/extensions'
import { RecallIndexer } from './indexer.js'
import { RecallSearch } from './search.js'
import { createRecallTool } from './tool.js'

export function activate(api: ExtensionAPI): Disposable {
  let indexer: RecallIndexer | null = null
  let search: RecallSearch | null = null

  // Lazy init — wait for database to be available
  async function ensureInitialized(): Promise<boolean> {
    if (indexer && search) return true

    try {
      const db = api.platform.database
      if (!db) return false

      indexer = new RecallIndexer(db)
      search = new RecallSearch(db)
      await indexer.init()
      return true
    } catch {
      api.log.debug('Recall: database not available, skipping initialization')
      return false
    }
  }

  // Index session on agent:finish
  const finishDisposable = api.on('agent:finish', async (data: unknown) => {
    const { sessionId } = data as { sessionId: string }
    if (!(await ensureInitialized()) || !indexer) return

    try {
      const session = api.getSessionManager().get(sessionId)
      if (!session) return

      const count = await indexer.indexSession(session)
      api.log.debug(`Recall: indexed ${count} messages from session ${sessionId.slice(0, 8)}`)
    } catch (err) {
      api.log.debug(`Recall: failed to index session ${sessionId}: ${err}`)
    }
  })

  // Register recall tool when database is available
  let toolDisposable: Disposable | null = null
  const initDisposable = api.on('session:opened', async () => {
    if (!(await ensureInitialized()) || !search) return
    if (toolDisposable) return // already registered

    const tool = createRecallTool(search)
    toolDisposable = api.registerTool(tool)
    api.log.debug('Recall: tool registered')
  })

  // Register /recall command
  const cmdDisposable = api.registerCommand({
    name: 'recall',
    description: 'Search past conversations',
    async execute(args: string) {
      if (!(await ensureInitialized()) || !search) {
        return 'Recall: database not available'
      }

      if (!args.trim()) {
        return 'Usage: /recall <search query>'
      }

      const results = await search.search(args.trim(), { limit: 5 })
      if (results.length === 0) {
        return `No results found for "${args.trim()}"`
      }

      return results
        .map((r, i) => `${i + 1}. [${r.role}] ${r.snippet} (session ${r.sessionId.slice(0, 8)})`)
        .join('\n')
    },
  })

  api.log.debug('Recall extension activated')

  return {
    dispose() {
      finishDisposable.dispose()
      initDisposable.dispose()
      cmdDisposable.dispose()
      toolDisposable?.dispose()
    },
  }
}
