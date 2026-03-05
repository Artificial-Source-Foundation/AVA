/**
 * File watcher extension — watches files for changes and emits events.
 *
 * On session:opened, starts watching `.git/HEAD` and emits `git:branch-changed`
 * when HEAD changes (branch switch, checkout, etc). Uses polling via platform fs.
 */

import { join } from 'node:path'
import type { Disposable, ExtensionAPI } from '@ava/core-v2/extensions'
import {
  type AvaCommentDirective,
  directiveSignature,
  extractAvaCommentDirectives,
} from './comment-detector.js'
import { FileWatcher } from './watcher.js'

const RESCAN_INTERVAL_MS = 5000
const WATCH_EXTENSIONS = new Set([
  '.ts',
  '.tsx',
  '.js',
  '.jsx',
  '.mjs',
  '.cjs',
  '.py',
  '.md',
  '.go',
  '.rs',
  '.java',
  '.c',
  '.cc',
  '.cpp',
  '.h',
  '.hpp',
  '.cs',
  '.sh',
  '.yaml',
  '.yml',
])

function hasWatchedExtension(path: string): boolean {
  for (const ext of WATCH_EXTENSIONS) {
    if (path.endsWith(ext)) return true
  }
  return false
}

function shouldSkipDir(path: string): boolean {
  return (
    path.includes('/.git/') ||
    path.endsWith('/.git') ||
    path.includes('/node_modules/') ||
    path.endsWith('/node_modules') ||
    path.includes('/.ava/snapshots/')
  )
}

async function discoverProjectFiles(
  api: ExtensionAPI,
  root: string,
  limit = 1000
): Promise<string[]> {
  const results: string[] = []
  const queue: string[] = [root]

  while (queue.length > 0 && results.length < limit) {
    const dir = queue.shift()
    if (!dir) break
    if (shouldSkipDir(dir)) continue

    let entries: Array<{ name: string; isFile: boolean; isDirectory: boolean }> = []
    try {
      entries = await api.platform.fs.readDirWithTypes(dir)
    } catch {
      continue
    }

    for (const entry of entries) {
      const fullPath = `${dir.endsWith('/') ? dir.slice(0, -1) : dir}/${entry.name}`
      if (entry.isDirectory) {
        if (!shouldSkipDir(fullPath)) queue.push(fullPath)
        continue
      }
      if (!entry.isFile) continue
      if (!hasWatchedExtension(fullPath)) continue
      results.push(fullPath)
      if (results.length >= limit) break
    }
  }

  return results
}

export function activate(api: ExtensionAPI): Disposable {
  const disposables: Disposable[] = []
  let watcher: FileWatcher | null = null
  const watchedFiles = new Set<string>()
  const lastDirectiveSignatures = new Map<string, Set<string>>()
  let rescanTimer: ReturnType<typeof setInterval> | null = null

  async function emitDirectives(
    filePath: string,
    workingDirectory: string,
    directives: AvaCommentDirective[]
  ): Promise<void> {
    const nextSignatures = new Set(directives.map((d) => directiveSignature(filePath, d)))
    const previous = lastDirectiveSignatures.get(filePath) ?? new Set<string>()

    for (const directive of directives) {
      const sig = directiveSignature(filePath, directive)
      if (previous.has(sig)) continue
      api.emit('ava:comment-detected', {
        filePath,
        workingDirectory,
        marker: directive.marker,
        message: directive.message,
        line: directive.line,
      })
    }

    lastDirectiveSignatures.set(filePath, nextSignatures)
  }

  async function inspectFile(filePath: string, workingDirectory: string): Promise<void> {
    try {
      const content = await api.platform.fs.readFile(filePath)
      const directives = extractAvaCommentDirectives(content)
      await emitDirectives(filePath, workingDirectory, directives)
    } catch {
      lastDirectiveSignatures.delete(filePath)
    }
  }

  async function watchNewFiles(workingDirectory: string): Promise<void> {
    if (!watcher) return
    const files = await discoverProjectFiles(api, workingDirectory)
    for (const filePath of files) {
      if (watchedFiles.has(filePath)) continue
      watchedFiles.add(filePath)
      await watcher.watch(filePath)
      await inspectFile(filePath, workingDirectory)
    }
  }

  function stopRescan(): void {
    if (rescanTimer) {
      clearInterval(rescanTimer)
      rescanTimer = null
    }
  }

  // Create watcher on session open — watch .git/HEAD for branch changes
  disposables.push(
    api.on('session:opened', (data) => {
      const { workingDirectory } = data as { sessionId: string; workingDirectory: string }
      const gitHeadPath = join(workingDirectory, '.git', 'HEAD')
      watchedFiles.clear()
      lastDirectiveSignatures.clear()
      stopRescan()

      watcher = new FileWatcher(api.platform.fs, (event) => {
        api.log.debug(`File changed: ${event.path}`)

        if (event.path === gitHeadPath) {
          // Read the new HEAD content to determine the branch
          void api.platform.fs
            .readFile(gitHeadPath)
            .then((content) => {
              const trimmed = content.trim()
              // HEAD format: "ref: refs/heads/<branch>" or a commit hash
              const branchMatch = /^ref: refs\/heads\/(.+)$/.exec(trimmed)
              const branch = branchMatch?.[1] ?? trimmed

              api.emit('git:branch-changed', {
                branch,
                raw: trimmed,
                workingDirectory,
              })
              api.log.debug(`Branch changed to: ${branch}`)
            })
            .catch((err: unknown) => {
              api.log.warn(`Failed to read .git/HEAD: ${String(err)}`)
            })
          return
        }

        void inspectFile(event.path, workingDirectory)
      })

      watchedFiles.add(gitHeadPath)
      void watcher.watch(gitHeadPath)
      void watchNewFiles(workingDirectory)
      rescanTimer = setInterval(() => {
        void watchNewFiles(workingDirectory)
      }, RESCAN_INTERVAL_MS)

      api.log.debug(`File watcher started for ${gitHeadPath}`)
    })
  )

  api.log.debug('File watcher extension activated')

  return {
    dispose() {
      stopRescan()
      watcher?.dispose()
      watcher = null
      for (const d of disposables) d.dispose()
    },
  }
}
