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
import { loadInstructions, mergeInstructions } from './loader.js'
import { resolveSubdirectoryInstructions } from './subdirectory.js'
import type { InstructionConfig } from './types.js'
import { DEFAULT_INSTRUCTION_CONFIG } from './types.js'

/** Tool names that trigger subdirectory instruction walking. */
const WATCHED_TOOLS = new Set(['read_file', 'edit'])

export function activate(api: ExtensionAPI): Disposable {
  let userConfig: Partial<InstructionConfig> = {}
  try {
    userConfig = api.getSettings<Partial<InstructionConfig>>('instructions')
  } catch {
    // Settings category not registered — use defaults
  }
  const config = { ...DEFAULT_INSTRUCTION_CONFIG, ...userConfig }
  const disposables: Disposable[] = []

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

  // Tool middleware to intercept read_file and edit, then walk subdirectories
  const subdirMiddleware: ToolMiddleware = {
    name: 'instructions-subdirectory',
    priority: 90, // Low priority — runs after most other middleware

    async after(
      ctx: ToolMiddlewareContext,
      _result: ToolResult
    ): Promise<ToolMiddlewareResult | undefined> {
      if (!WATCHED_TOOLS.has(ctx.toolName)) return undefined
      if (!sessionCwd) return undefined

      // Extract file path from tool args
      const filePath = extractFilePath(ctx.toolName, ctx.args)
      if (!filePath) return undefined

      try {
        const newFiles = await resolveSubdirectoryInstructions(
          filePath,
          sessionCwd,
          api.platform.fs,
          config,
          alreadyLoaded
        )

        if (newFiles.length > 0) {
          // Track newly loaded paths for future dedup
          for (const f of newFiles) alreadyLoaded.add(f.path)

          const merged = mergeInstructions(newFiles)
          api.emit('instructions:subdirectory-loaded', {
            filePath,
            files: newFiles,
            merged,
            count: newFiles.length,
          })
          api.log.debug(`Subdirectory instructions: ${newFiles.length} file(s) from ${filePath}`)
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

/**
 * Extract the file path from tool arguments.
 */
function extractFilePath(toolName: string, args: Record<string, unknown>): string | null {
  switch (toolName) {
    case 'read_file':
      return typeof args.path === 'string'
        ? args.path
        : typeof args.file_path === 'string'
          ? args.file_path
          : null
    case 'edit':
      return typeof args.file_path === 'string'
        ? args.file_path
        : typeof args.path === 'string'
          ? args.path
          : null
    default:
      return null
  }
}
