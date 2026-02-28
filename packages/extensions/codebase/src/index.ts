/**
 * Codebase extension — repo map, symbols, and PageRank.
 *
 * Basic file indexing via glob with language detection.
 * Registers a /files command for listing indexed files.
 */

import type { Disposable, ExtensionAPI } from '@ava/core-v2/extensions'
import { createRepoMap, indexFiles } from './indexer.js'
import type { RepoMap } from './types.js'

export function activate(api: ExtensionAPI): Disposable {
  const disposables: Disposable[] = []
  let repoMap: RepoMap | null = null

  // Index files on session open
  disposables.push(
    api.on('session:opened', (data) => {
      const { workingDirectory } = data as { sessionId: string; workingDirectory: string }

      void indexFiles(workingDirectory, api.platform.fs).then((files) => {
        repoMap = createRepoMap(files)
        void api.storage.set('repoMap', repoMap)
        api.emit('codebase:ready', {
          totalFiles: repoMap.totalFiles,
          totalSymbols: repoMap.totalSymbols,
        })
        api.log.debug(`Indexed ${repoMap.totalFiles} files`)
      })
    })
  )

  // Register /files command
  disposables.push(
    api.registerCommand({
      name: 'files',
      description: 'List indexed files with language breakdown',
      async execute() {
        if (!repoMap) return 'Codebase not indexed yet. Open a session first.'

        const langCounts = new Map<string, number>()
        for (const file of repoMap.files) {
          langCounts.set(file.language, (langCounts.get(file.language) ?? 0) + 1)
        }

        const lines = [`**${repoMap.totalFiles} files indexed**\n`]
        const sorted = [...langCounts.entries()].sort((a, b) => b[1] - a[1])
        for (const [lang, count] of sorted) {
          lines.push(`- ${lang}: ${count}`)
        }

        return lines.join('\n')
      },
    })
  )

  // Register /symbols command
  disposables.push(
    api.registerCommand({
      name: 'symbols',
      description: 'List extracted symbols with type and location',
      async execute() {
        if (!repoMap) return 'Codebase not indexed yet. Open a session first.'
        if (repoMap.totalSymbols === 0)
          return 'No symbols extracted. Index may not include supported languages.'

        const kindCounts = new Map<string, number>()
        for (const file of repoMap.files) {
          for (const sym of file.symbols) {
            kindCounts.set(sym.kind, (kindCounts.get(sym.kind) ?? 0) + 1)
          }
        }

        const lines = [`**${repoMap.totalSymbols} symbols extracted**\n`]
        const sorted = [...kindCounts.entries()].sort((a, b) => b[1] - a[1])
        for (const [kind, count] of sorted) {
          lines.push(`- ${kind}: ${count}`)
        }

        return lines.join('\n')
      },
    })
  )

  api.log.debug('Codebase intelligence extension activated')

  return {
    dispose() {
      for (const d of disposables) d.dispose()
      repoMap = null
    },
  }
}
