/**
 * Codebase extension — repo map, symbols, and PageRank.
 *
 * Basic file indexing via glob with language detection.
 * Registers a /files command for listing indexed files.
 */

import { dispatchCompute } from '@ava/core-v2'
import type { Disposable, ExtensionAPI } from '@ava/core-v2/extensions'
import { createRepoMap, indexFiles } from './indexer.js'
import type { RepoMap } from './types.js'

interface RepoMapInputFile {
  path: string
  content: string
  dependencies: string[]
}

interface RankedEntry {
  path: string
  score: number
}

const IMPORT_FROM_RE = /(?:import|export)\s+[^\n]*?from\s+['"]([^'"]+)['"]/g
const IMPORT_RE = /import\s+['"]([^'"]+)['"]/g
const REQUIRE_RE = /require\(\s*['"]([^'"]+)['"]\s*\)/g

function extractDependencies(content: string): string[] {
  const deps = new Set<string>()
  for (const regex of [IMPORT_FROM_RE, IMPORT_RE, REQUIRE_RE]) {
    for (const match of content.matchAll(regex)) {
      const dep = match[1]
      if (dep) deps.add(dep)
    }
  }
  return [...deps]
}

function tsFallbackRank(files: RepoMapInputFile[]): RankedEntry[] {
  const incoming = new Map<string, number>()
  for (const file of files) {
    incoming.set(file.path, incoming.get(file.path) ?? 1)
  }

  const known = new Set(files.map((file) => file.path))
  for (const file of files) {
    for (const dep of file.dependencies) {
      if (known.has(dep)) {
        incoming.set(dep, (incoming.get(dep) ?? 0) + 1)
      }
    }
  }

  return files
    .map((file) => ({ path: file.path, score: incoming.get(file.path) ?? 0 }))
    .sort((a, b) => b.score - a.score)
}

async function computeRepoMap(
  fs: ExtensionAPI['platform']['fs'],
  files: Array<{ path: string; symbols: Array<{ kind: string }> }>,
  activeFiles: string[] = [],
  mentionedFiles: string[] = []
): Promise<RankedEntry[]> {
  const payload: RepoMapInputFile[] = []

  for (const file of files) {
    try {
      const content = await fs.readFile(file.path)
      payload.push({
        path: file.path,
        content,
        dependencies: extractDependencies(content),
      })
    } catch {
      // Skip unreadable files.
    }
  }

  try {
    const result = await dispatchCompute<{ files: RankedEntry[] }>(
      'compute_repo_map',
      {
        files: payload,
        query: '',
        limit: 200,
        activeFiles,
        mentionedFiles,
      },
      async () => ({ files: tsFallbackRank(payload) })
    )
    return result.files
  } catch {
    return tsFallbackRank(payload)
  }
}

export function activate(api: ExtensionAPI): Disposable {
  const disposables: Disposable[] = []
  let repoMap: RepoMap | null = null

  // Index files on session open
  disposables.push(
    api.on('session:opened', (data) => {
      const { workingDirectory } = data as { sessionId: string; workingDirectory: string }

      void indexFiles(workingDirectory, api.platform.fs).then((files) => {
        const activeFiles = files.slice(0, 5).map((file) => file.path)

        const mentionedFiles = [...files]
          .sort((a, b) => (b.symbols.length ?? 0) - (a.symbols.length ?? 0))
          .slice(0, 10)
          .map((file) => file.path)

        void computeRepoMap(api.platform.fs, files, activeFiles, mentionedFiles).then(
          (rankedFiles) => {
            repoMap = createRepoMap(files)
            repoMap.rankedFiles = rankedFiles
            void api.storage.set('repoMap', repoMap)
            api.emit('codebase:ready', {
              totalFiles: repoMap.totalFiles,
              totalSymbols: repoMap.totalSymbols,
            })
            api.log.debug(`Indexed ${repoMap.totalFiles} files`)
          }
        )
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

  disposables.push(
    api.registerCommand({
      name: 'repomap',
      description: 'Show ranked repository map entries',
      async execute() {
        if (!repoMap || repoMap.rankedFiles.length === 0) {
          return 'Repo map not ready yet. Open a session first.'
        }

        const lines = ['<repo_map>']
        for (const entry of repoMap.rankedFiles.slice(0, 40)) {
          lines.push(`${entry.path} (${entry.score.toFixed(4)})`)
        }
        lines.push('</repo_map>')
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
