import type { ToolMiddleware, ToolMiddlewareContext } from '@ava/core-v2/extensions'
import type { SimpleLogger } from '@ava/core-v2/logger'
import type { IPlatformProvider } from '@ava/core-v2/platform'
import type { ToolResult } from '@ava/core-v2/tools'

export interface FormatterChange {
  file: string
  editChanges: string
  formatterChanges: string
  formatterName: string
}

interface FormatterInfo {
  name: string
}

const FILE_EDIT_TOOLS = new Set(['write_file', 'edit', 'create_file', 'apply_patch'])
const FORMATTER_CONFIGS: Array<{ configFile: string; formatter: FormatterInfo }> = [
  { configFile: 'biome.json', formatter: { name: 'biome' } },
  { configFile: 'biome.jsonc', formatter: { name: 'biome' } },
  { configFile: '.prettierrc', formatter: { name: 'prettier' } },
  { configFile: '.prettierrc.json', formatter: { name: 'prettier' } },
  { configFile: 'deno.json', formatter: { name: 'deno' } },
]

const formatterCache = new Map<string, FormatterInfo | null>()

function getFilePath(args: Record<string, unknown>): string | null {
  const direct = args.path ?? args.filePath ?? args.file_path
  return typeof direct === 'string' ? direct : null
}

function makeKey(sessionId: string, toolName: string, filePath: string): string {
  return `${sessionId}:${toolName}:${filePath}`
}

function resolvePostEdit(before: string, ctx: ToolMiddlewareContext): string {
  const args = ctx.args
  if (ctx.toolName === 'write_file') {
    const content = args.content
    return typeof content === 'string' ? content : before
  }

  if (ctx.toolName === 'edit') {
    const oldString = args.oldString
    const newString = args.newString
    if (
      typeof oldString === 'string' &&
      typeof newString === 'string' &&
      before.includes(oldString)
    ) {
      return before.replace(oldString, newString)
    }
  }

  return before
}

function renderDiff(before: string, after: string): string {
  if (before === after) {
    return ''
  }

  const beforeLines = before.split('\n')
  const afterLines = after.split('\n')
  const max = Math.max(beforeLines.length, afterLines.length)
  const chunks: string[] = []

  for (let i = 0; i < max; i += 1) {
    const oldLine = beforeLines[i] ?? ''
    const newLine = afterLines[i] ?? ''
    if (oldLine === newLine) {
      continue
    }
    chunks.push(`@@ line ${i + 1} @@`, `- ${oldLine}`, `+ ${newLine}`)
  }

  return chunks.join('\n')
}

async function detectFormatter(
  cwd: string,
  platform: IPlatformProvider
): Promise<FormatterInfo | null> {
  const cached = formatterCache.get(cwd)
  if (cached !== undefined) {
    return cached
  }

  for (const item of FORMATTER_CONFIGS) {
    const configPath = cwd.endsWith('/') ? `${cwd}${item.configFile}` : `${cwd}/${item.configFile}`
    try {
      if (await platform.fs.exists(configPath)) {
        formatterCache.set(cwd, item.formatter)
        return item.formatter
      }
    } catch {
      // best-effort detection only
    }
  }

  formatterCache.set(cwd, null)
  return null
}

export function createFormatterDetectionMiddleware(
  platform: IPlatformProvider,
  logger: SimpleLogger
): ToolMiddleware {
  const snapshots = new Map<string, string>()

  return {
    name: 'ava-formatter-detection',
    priority: 51,

    async before(ctx: ToolMiddlewareContext): Promise<undefined> {
      if (!FILE_EDIT_TOOLS.has(ctx.toolName)) {
        return undefined
      }

      const filePath = getFilePath(ctx.args)
      if (!filePath) {
        return undefined
      }

      try {
        const previous = await platform.fs.readFile(filePath)
        snapshots.set(makeKey(ctx.ctx.sessionId, ctx.toolName, filePath), previous)
      } catch {
        snapshots.set(makeKey(ctx.ctx.sessionId, ctx.toolName, filePath), '')
      }

      return undefined
    },

    async after(
      ctx: ToolMiddlewareContext,
      result: ToolResult
    ): Promise<{ result?: ToolResult } | undefined> {
      if (!FILE_EDIT_TOOLS.has(ctx.toolName) || !result.success) {
        return undefined
      }

      const filePath = getFilePath(ctx.args)
      if (!filePath) {
        return undefined
      }

      const key = makeKey(ctx.ctx.sessionId, ctx.toolName, filePath)
      const before = snapshots.get(key) ?? ''
      snapshots.delete(key)

      const formatter = await detectFormatter(ctx.ctx.workingDirectory, platform)
      if (!formatter) {
        return undefined
      }

      let after = before
      try {
        after = await platform.fs.readFile(filePath)
      } catch {
        return undefined
      }

      const simulatedPostEdit = resolvePostEdit(before, ctx)
      const formatterChanges = renderDiff(simulatedPostEdit, after)
      if (!formatterChanges) {
        return undefined
      }

      const formatterChange: FormatterChange = {
        file: filePath,
        editChanges: renderDiff(before, simulatedPostEdit),
        formatterChanges,
        formatterName: formatter.name,
      }

      logger.debug(`Formatter detection: ${formatter.name} changed ${filePath}`)

      return {
        result: {
          ...result,
          metadata: {
            ...(result.metadata ?? {}),
            formatterApplied: true,
            formatterDiff: formatterChanges,
            formatterChange,
          },
        },
      }
    },
  }
}

export function clearFormatterDetectionCache(): void {
  formatterCache.clear()
}
