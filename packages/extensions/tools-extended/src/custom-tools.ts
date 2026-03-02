/**
 * Custom user tools — discovers and loads tools from user-defined directories.
 *
 * Scans `.ava/tools/` in the project directory and `~/.ava/tools/` globally.
 * Each `.ts`/`.js` file should export a tool definition (default export).
 */

import * as os from 'node:os'
import * as path from 'node:path'
import type { Disposable, ExtensionAPI } from '@ava/core-v2/extensions'
import type { Tool } from '@ava/core-v2/tools'

/** Directories to scan for custom tool files. */
function getToolDirs(cwd: string): string[] {
  return [path.join(cwd, '.ava', 'tools'), path.join(os.homedir(), '.ava', 'tools')]
}

/**
 * Check if a filename is a loadable tool file.
 */
function isToolFile(name: string): boolean {
  return (
    (name.endsWith('.ts') || name.endsWith('.js')) &&
    !name.endsWith('.test.ts') &&
    !name.endsWith('.test.js') &&
    !name.endsWith('.d.ts')
  )
}

/**
 * Validate that a loaded module looks like a tool definition.
 */
function isToolDefinition(obj: unknown): obj is Tool {
  if (!obj || typeof obj !== 'object') return false
  const candidate = obj as Record<string, unknown>
  if (!candidate.definition || typeof candidate.definition !== 'object') return false
  const def = candidate.definition as Record<string, unknown>
  return typeof def.name === 'string' && typeof def.description === 'string'
}

/**
 * Load custom tools from `.ava/tools/` directories.
 *
 * Each file should have a default export that is either:
 * - A tool definition object (with `definition` and `execute`)
 * - A function that returns a tool definition
 */
export async function loadCustomTools(cwd: string, api: ExtensionAPI): Promise<Disposable[]> {
  const disposables: Disposable[] = []
  const dirs = getToolDirs(cwd)

  for (const dir of dirs) {
    let entries: string[]
    try {
      entries = await api.platform.fs.readDir(dir)
    } catch {
      // Directory doesn't exist — skip
      continue
    }

    const toolFiles = entries.filter(isToolFile)

    for (const fileName of toolFiles) {
      const filePath = path.join(dir, fileName)
      try {
        const mod = (await import(filePath)) as Record<string, unknown>
        let tool: unknown = mod.default

        // If default export is a function, call it to get the tool
        if (typeof tool === 'function') {
          tool = (tool as () => unknown)()
        }

        if (isToolDefinition(tool)) {
          const disposable = api.registerTool(tool)
          disposables.push(disposable)
          api.log.info(
            `Loaded custom tool: ${(tool.definition as { name: string }).name} from ${filePath}`
          )
        } else {
          api.log.warn(`Skipping ${filePath}: not a valid tool definition`)
        }
      } catch (err) {
        const msg = err instanceof Error ? err.message : String(err)
        api.log.warn(`Failed to load custom tool ${filePath}: ${msg}`)
      }
    }
  }

  return disposables
}
