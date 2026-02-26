/**
 * glob tool — find files by pattern.
 */

import * as nodePath from 'node:path'
import * as z from 'zod'
import { getPlatform } from '../platform.js'
import { defineTool } from './define.js'
import { LIMITS, matchesGlob, resolvePath, shouldSkipDirectory } from './utils.js'

const schema = z.object({
  pattern: z.string().describe('Glob pattern (e.g., "**/*.ts", "src/**/*.tsx")'),
  path: z.string().optional().describe('Search root directory (defaults to working directory)'),
})

export const globTool = defineTool({
  name: 'glob',
  description:
    'Find files matching a glob pattern. Returns absolute paths sorted by modification time.',
  schema,
  permissions: ['read'],

  async execute(input, ctx) {
    const fs = getPlatform().fs
    const searchDir = input.path
      ? resolvePath(input.path, ctx.workingDirectory)
      : ctx.workingDirectory

    const matches: Array<{ path: string; mtime: number }> = []
    let truncated = false

    async function search(dir: string, relativePath: string): Promise<void> {
      if (ctx.signal.aborted || matches.length >= LIMITS.MAX_RESULTS) {
        truncated = matches.length >= LIMITS.MAX_RESULTS
        return
      }

      let entries: Awaited<ReturnType<typeof fs.readDirWithTypes>>
      try {
        entries = await fs.readDirWithTypes(dir)
      } catch {
        return // Skip unreadable directories
      }

      for (const entry of entries) {
        if (ctx.signal.aborted || matches.length >= LIMITS.MAX_RESULTS) {
          truncated = matches.length >= LIMITS.MAX_RESULTS
          return
        }

        const entryRelative = relativePath ? `${relativePath}/${entry.name}` : entry.name
        const entryAbsolute = nodePath.join(dir, entry.name)

        if (entry.isDirectory) {
          if (!shouldSkipDirectory(entry.name)) {
            await search(entryAbsolute, entryRelative)
          }
        } else if (entry.isFile) {
          if (matchesGlob(entryRelative, input.pattern)) {
            let mtime = 0
            try {
              const stat = await fs.stat(entryAbsolute)
              mtime = stat.mtime
            } catch {
              // Use 0 if stat fails
            }
            matches.push({ path: entryAbsolute, mtime })
          }
        }
      }
    }

    await search(searchDir, '')

    // Sort by modification time, newest first
    matches.sort((a, b) => b.mtime - a.mtime)

    if (matches.length === 0) {
      return {
        success: true,
        output: `No files found matching "${input.pattern}" in ${searchDir}`,
        metadata: { count: 0, truncated: false, pattern: input.pattern, searchDir },
      }
    }

    const paths = matches.map((m) => m.path)
    const output = `Found ${matches.length} file(s)${truncated ? ' (truncated)' : ''}:\n${paths.join('\n')}`

    return {
      success: true,
      output,
      metadata: { count: matches.length, truncated, pattern: input.pattern, searchDir },
      locations: matches.map((m) => ({ path: m.path, type: 'read' as const })),
    }
  },
})
