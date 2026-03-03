/**
 * Instructions extension — loads project/directory instructions.
 *
 * Listens for session:opened events and loads instruction files
 * from the working directory upward. Emits `instructions:loaded` with
 * the merged content so the CLI/app can inject it into the system prompt.
 *
 * Also intercepts read_file and edit tool calls to discover
 * subdirectory AGENTS.md files via tool middleware.
 */

import type {
  Disposable,
  ExtensionAPI,
  ToolMiddleware,
  ToolMiddlewareContext,
  ToolMiddlewareResult,
} from '@ava/core-v2/extensions'
import type { ToolResult } from '@ava/core-v2/tools'
import { DiscoveryCache } from './discovery-cache.js'
import { loadInstructions, mergeInstructions } from './loader.js'
import { extractPaths, toDirectoryKey } from './path-extractor.js'
import { resolveSubdirectoryInstructions } from './subdirectory.js'
import type { InstructionConfig } from './types.js'
import { DEFAULT_INSTRUCTION_CONFIG } from './types.js'

export function activate(api: ExtensionAPI): Disposable {
  let userConfig: Partial<InstructionConfig> = {}
  try {
    userConfig = api.getSettings<Partial<InstructionConfig>>('instructions')
  } catch {
    // Settings category not registered — use defaults
  }
  const config = { ...DEFAULT_INSTRUCTION_CONFIG, ...userConfig }
  const disposables: Disposable[] = []
  const watchedTools = new Set(config.watchedTools)
  const cache = new DiscoveryCache(config.discoveryCacheTtlMs)

  /** Session-scoped set of instruction paths already loaded. */
  const alreadyLoaded = new Set<string>()

  /** Cached working directory from session:opened. */
  let sessionCwd = ''

  disposables.push(
    api.on('session:opened', (data) => {
      const { sessionId, workingDirectory } = data as {
        sessionId: string
        workingDirectory: string
      }

      sessionCwd = workingDirectory
      alreadyLoaded.clear()
      cache.clear()

      void loadInstructions(workingDirectory, api.platform.fs, config, api.log).then((files) => {
        if (files.length > 0) {
          const merged = mergeInstructions(files)
          // Track all initially loaded paths for dedup
          for (const f of files) alreadyLoaded.add(f.path)
          void api.storage.set(`instructions:${sessionId}`, files)
          api.emit('instructions:loaded', {
            sessionId,
            files,
            merged,
            count: files.length,
          })
          api.log.info(`Loaded ${files.length} instruction file(s)`)
        }
      })
    })
  )

  // Tool middleware to discover subdirectory instructions from file-access tools
  const subdirMiddleware: ToolMiddleware = {
    name: 'instructions-subdirectory',
    priority: 90, // Low priority — runs after most other middleware

    async after(
      ctx: ToolMiddlewareContext,
      _result: ToolResult
    ): Promise<ToolMiddlewareResult | undefined> {
      if (!watchedTools.has(ctx.toolName)) return undefined
      if (!sessionCwd) return undefined

      const paths = extractPaths(ctx.toolName, ctx.args, sessionCwd)
      if (paths.length === 0) return undefined

      try {
        const allNewFiles = [] as Awaited<ReturnType<typeof resolveSubdirectoryInstructions>>

        for (const path of paths) {
          const key = toDirectoryKey(path)
          const cached = cache.get(key)

          if (cached) {
            const unseen = cached.filter((file) => !alreadyLoaded.has(file.path))
            for (const file of unseen) alreadyLoaded.add(file.path)
            if (unseen.length > 0) allNewFiles.push(...unseen)
            continue
          }

          const newFiles = await resolveSubdirectoryInstructions(
            path,
            sessionCwd,
            api.platform.fs,
            config,
            alreadyLoaded
          )
          cache.set(key, newFiles)
          if (newFiles.length > 0) {
            for (const file of newFiles) alreadyLoaded.add(file.path)
            allNewFiles.push(...newFiles)
          }
        }

        if (allNewFiles.length > 0) {
          const merged = mergeInstructions(allNewFiles)
          api.emit('instructions:subdirectory-loaded', {
            filePath: paths[0],
            files: allNewFiles,
            merged,
            count: allNewFiles.length,
          })
          api.log.debug(
            `Subdirectory instructions: ${allNewFiles.length} file(s) from ${paths.join(', ')}`
          )
        }
      } catch (error) {
        const message = error instanceof Error ? error.message : String(error)
        api.log.warn(`Subdirectory instruction scan failed: ${message}`)
      }

      return undefined
    },
  }

  disposables.push(api.addToolMiddleware(subdirMiddleware))

  api.log.debug('Instructions extension activated')

  return {
    dispose() {
      for (const d of disposables) d.dispose()
    },
  }
}
