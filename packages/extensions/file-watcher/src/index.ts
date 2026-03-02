/**
 * File watcher extension — watches files for changes and emits events.
 *
 * On session:opened, starts watching `.git/HEAD` and emits `git:branch-changed`
 * when HEAD changes (branch switch, checkout, etc). Uses polling via platform fs.
 */

import { join } from 'node:path'
import type { Disposable, ExtensionAPI } from '@ava/core-v2/extensions'
import { FileWatcher } from './watcher.js'

export function activate(api: ExtensionAPI): Disposable {
  const disposables: Disposable[] = []
  let watcher: FileWatcher | null = null

  // Create watcher on session open — watch .git/HEAD for branch changes
  disposables.push(
    api.on('session:opened', (data) => {
      const { workingDirectory } = data as { sessionId: string; workingDirectory: string }
      const gitHeadPath = join(workingDirectory, '.git', 'HEAD')

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
        }
      })

      void watcher.watch(gitHeadPath)
      api.log.debug(`File watcher started for ${gitHeadPath}`)
    })
  )

  api.log.debug('File watcher extension activated')

  return {
    dispose() {
      watcher?.dispose()
      watcher = null
      for (const d of disposables) d.dispose()
    },
  }
}
