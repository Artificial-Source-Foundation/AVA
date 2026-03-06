import { randomUUID } from 'node:crypto'
import * as os from 'node:os'
import * as path from 'node:path'
import type { ToolMiddleware, ToolMiddlewareContext } from '@ava/core-v2/extensions'
import { getPlatform } from '@ava/core-v2/platform'
import type { AnyTool, ToolResult } from '@ava/core-v2/tools'

export interface ModifyContext<T> {
  getFilePath(params: T): string
  getCurrentContent(params: T): Promise<string>
  getProposedContent(params: T): string
  createUpdatedParams(current: string, modified: string, original: T): T
}

type UnknownArgs = Record<string, unknown>

const modifiableContexts = new Map<string, ModifyContext<UnknownArgs>>()

function asRecord(value: unknown): UnknownArgs {
  if (value && typeof value === 'object') {
    return value as UnknownArgs
  }
  return {}
}

function resolveToolPath(inputPath: string, cwd: string): string {
  return path.isAbsolute(inputPath) ? inputPath : path.join(cwd, inputPath)
}

function createDefaultContext(toolName: string): ModifyContext<UnknownArgs> | null {
  if (toolName === 'edit') {
    return {
      getFilePath(params) {
        const p = params.filePath
        return typeof p === 'string' ? p : ''
      },
      async getCurrentContent(params) {
        const p = params.filePath
        if (typeof p !== 'string' || p.length === 0) {
          return ''
        }
        const fs = getPlatform().fs
        try {
          return await fs.readFile(p)
        } catch {
          return ''
        }
      },
      getProposedContent(params) {
        const proposed = params.newString
        return typeof proposed === 'string' ? proposed : ''
      },
      createUpdatedParams(_current, modified, original) {
        return { ...original, newString: modified }
      },
    }
  }

  if (toolName === 'write_file') {
    return {
      getFilePath(params) {
        const p = params.path
        return typeof p === 'string' ? p : ''
      },
      async getCurrentContent(params) {
        const p = params.path
        if (typeof p !== 'string' || p.length === 0) {
          return ''
        }
        const fs = getPlatform().fs
        try {
          return await fs.readFile(p)
        } catch {
          return ''
        }
      },
      getProposedContent(params) {
        const proposed = params.content
        return typeof proposed === 'string' ? proposed : ''
      },
      createUpdatedParams(_current, modified, original) {
        return { ...original, content: modified }
      },
    }
  }

  if (toolName === 'apply_patch') {
    return {
      getFilePath(params) {
        const patch = params.patch
        if (typeof patch !== 'string') {
          return 'apply_patch.diff'
        }
        const line = patch.split('\n').find((entry) => entry.startsWith('*** Update File: '))
        return line ? line.replace('*** Update File: ', '').trim() : 'apply_patch.diff'
      },
      async getCurrentContent() {
        return ''
      },
      getProposedContent(params) {
        const patch = params.patch
        return typeof patch === 'string' ? patch : ''
      },
      createUpdatedParams(_current, modified, original) {
        return { ...original, patch: modified }
      },
    }
  }

  return null
}

export function makeModifiable<T extends object>(
  tool: AnyTool,
  context: ModifyContext<T>
): AnyTool {
  const toolName = tool.definition.name
  const adapter: ModifyContext<UnknownArgs> = {
    getFilePath(params) {
      return context.getFilePath(params as T)
    },
    async getCurrentContent(params) {
      return context.getCurrentContent(params as T)
    },
    getProposedContent(params) {
      return context.getProposedContent(params as T)
    },
    createUpdatedParams(current, modified, original) {
      return context.createUpdatedParams(current, modified, original as T) as UnknownArgs
    },
  }

  modifiableContexts.set(toolName, adapter)
  const definitionWithMarker = {
    ...tool.definition,
    modifiable: true,
  } as unknown as typeof tool.definition

  return {
    ...tool,
    definition: definitionWithMarker,
  }
}

function getContext(toolName: string): ModifyContext<UnknownArgs> | null {
  return modifiableContexts.get(toolName) ?? createDefaultContext(toolName)
}

function getEditor(): string {
  const visual = process.env.VISUAL
  if (visual && visual.trim().length > 0) {
    return visual
  }
  const editor = process.env.EDITOR
  if (editor && editor.trim().length > 0) {
    return editor
  }
  return 'vi'
}

function getTempPaths(filePath: string): { currentPath: string; proposedPath: string } {
  const base = path.basename(filePath || 'buffer.txt')
  const id = randomUUID()
  const dir = os.tmpdir()
  return {
    currentPath: path.join(dir, `ava-edit-${id}-current-${base}`),
    proposedPath: path.join(dir, `ava-edit-${id}-proposed-${base}`),
  }
}

async function cleanupTempFiles(paths: string[]): Promise<void> {
  const fs = getPlatform().fs
  await Promise.all(
    paths.map(async (entry) => {
      try {
        const exists = await fs.exists(entry)
        if (exists) {
          await fs.remove(entry)
        }
      } catch {
        // best effort cleanup
      }
    })
  )
}

export function createModifiableToolMiddleware(
  emit?: (event: string, data: unknown) => void
): ToolMiddleware {
  return {
    name: 'ava-modifiable-tool',
    priority: 10,
    async before(ctx: ToolMiddlewareContext): Promise<{ args?: UnknownArgs } | undefined> {
      const context = getContext(ctx.toolName)
      if (!context) {
        return undefined
      }

      const params = asRecord(ctx.args)
      const filePath = context.getFilePath(params)
      const cwd = ctx.ctx.workingDirectory
      const resolvedPath = filePath ? resolveToolPath(filePath, cwd) : filePath

      const currentContent = await context.getCurrentContent({ ...params, filePath: resolvedPath })
      const proposedContent = context.getProposedContent(params)
      const temp = getTempPaths(resolvedPath || ctx.toolName)
      const fs = getPlatform().fs

      try {
        await fs.writeFile(temp.currentPath, currentContent)
        await fs.writeFile(temp.proposedPath, proposedContent)

        if (emit) {
          emit('tool:modify-request', {
            toolName: ctx.toolName,
            filePath: resolvedPath,
            currentPath: temp.currentPath,
            proposedPath: temp.proposedPath,
          })
        } else {
          const editor = getEditor()
          const command = `${editor} "${temp.currentPath}" "${temp.proposedPath}"`
          await getPlatform().shell.exec(command, {
            cwd,
            timeout: 120_000,
          })
        }

        const modified = await fs.readFile(temp.proposedPath)
        const updated = context.createUpdatedParams(currentContent, modified, params)
        return { args: updated }
      } finally {
        await cleanupTempFiles([temp.currentPath, temp.proposedPath])
      }
    },
  }
}

export function resetModifiableToolRegistry(): void {
  modifiableContexts.clear()
}

export function passthroughToolResult(result: ToolResult): ToolResult {
  return result
}
