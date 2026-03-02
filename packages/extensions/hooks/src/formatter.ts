/**
 * Auto-formatter middleware — runs a formatter after file-editing tools.
 *
 * Detects the project's formatter config (biome.json, .prettierrc, deno.json)
 * and runs the appropriate format command on edited files.
 */

import type { ToolMiddleware, ToolMiddlewareContext } from '@ava/core-v2/extensions'
import type { SimpleLogger } from '@ava/core-v2/logger'
import type { IPlatformProvider } from '@ava/core-v2/platform'
import type { ToolResult } from '@ava/core-v2/tools'

/** Tools that modify files and should trigger formatting. */
const FILE_EDIT_TOOLS = new Set(['write_file', 'edit', 'create_file', 'apply_patch'])

/** Formatter config file → format command mapping. */
interface FormatterInfo {
  name: string
  command: (filePath: string) => string
}

const FORMATTER_CONFIGS: Array<{ configFile: string; formatter: FormatterInfo }> = [
  {
    configFile: 'biome.json',
    formatter: {
      name: 'biome',
      command: (f: string) => `npx biome format --write "${f}"`,
    },
  },
  {
    configFile: 'biome.jsonc',
    formatter: {
      name: 'biome',
      command: (f: string) => `npx biome format --write "${f}"`,
    },
  },
  {
    configFile: '.prettierrc',
    formatter: {
      name: 'prettier',
      command: (f: string) => `npx prettier --write "${f}"`,
    },
  },
  {
    configFile: '.prettierrc.json',
    formatter: {
      name: 'prettier',
      command: (f: string) => `npx prettier --write "${f}"`,
    },
  },
  {
    configFile: 'deno.json',
    formatter: {
      name: 'deno',
      command: (f: string) => `deno fmt "${f}"`,
    },
  },
]

/** Cache detected formatter per working directory. */
const formatterCache = new Map<string, FormatterInfo | null>()

/**
 * Detect which formatter to use by scanning for config files.
 */
async function detectFormatter(
  cwd: string,
  platform: IPlatformProvider
): Promise<FormatterInfo | null> {
  const cached = formatterCache.get(cwd)
  if (cached !== undefined) return cached

  for (const { configFile, formatter } of FORMATTER_CONFIGS) {
    const configPath = cwd.endsWith('/') ? `${cwd}${configFile}` : `${cwd}/${configFile}`
    try {
      const exists = await platform.fs.exists(configPath)
      if (exists) {
        formatterCache.set(cwd, formatter)
        return formatter
      }
    } catch {
      // Skip if can't check
    }
  }

  formatterCache.set(cwd, null)
  return null
}

/**
 * Extract the file path from tool arguments.
 */
function getFilePath(args: Record<string, unknown>): string | null {
  const path = args.path ?? args.filePath ?? args.file_path
  return typeof path === 'string' ? path : null
}

/**
 * Create a ToolMiddleware that auto-formats files after editing tools.
 */
export function createFormatterMiddleware(
  platform: IPlatformProvider,
  log: SimpleLogger
): ToolMiddleware {
  return {
    name: 'ava-formatter',
    priority: 50,

    async after(
      ctx: ToolMiddlewareContext,
      result: ToolResult
    ): Promise<{ result?: ToolResult } | undefined> {
      // Only run for file-editing tools
      if (!FILE_EDIT_TOOLS.has(ctx.toolName)) return undefined

      // Only format on success
      if (!result.success) return undefined

      const filePath = getFilePath(ctx.args)
      if (!filePath) return undefined

      const cwd = ctx.ctx.workingDirectory
      const formatter = await detectFormatter(cwd, platform)
      if (!formatter) return undefined

      try {
        const cmd = formatter.command(filePath)
        log.debug(`Running ${formatter.name} on ${filePath}`)
        await platform.shell.exec(cmd, { cwd, timeout: 15_000 })
      } catch (err) {
        const msg = err instanceof Error ? err.message : String(err)
        log.warn(`Formatter ${formatter.name} failed on ${filePath}: ${msg}`)
        // Don't crash — just warn
      }

      return undefined
    },
  }
}

/** Clear the formatter cache (for testing). */
export function clearFormatterCache(): void {
  formatterCache.clear()
}
