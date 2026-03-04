/**
 * glob tool — find files by pattern.
 */

import * as z from 'zod'
import { getPlatform } from '../platform.js'
import { defineTool } from './define.js'
import { LIMITS, resolvePath } from './utils.js'

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

    const paths = await fs.glob(input.pattern, searchDir)
    const truncated = paths.length > LIMITS.MAX_RESULTS
    const limited = paths.slice(0, LIMITS.MAX_RESULTS)
    const matches: Array<{ path: string; mtime: number }> = []

    for (const path of limited) {
      if (ctx.signal.aborted) {
        break
      }

      let mtime = 0
      try {
        const stat = await fs.stat(path)
        mtime = stat.mtime
      } catch {
        // Use 0 if stat fails
      }
      matches.push({ path, mtime })
    }

    // Sort by modification time, newest first
    matches.sort((a, b) => b.mtime - a.mtime)

    if (matches.length === 0) {
      return {
        success: true,
        output: `No files found matching "${input.pattern}" in ${searchDir}`,
        metadata: { count: 0, truncated: false, pattern: input.pattern, searchDir },
      }
    }

    const matchPaths = matches.map((m) => m.path)
    const output = `Found ${matches.length} file(s)${truncated ? ' (truncated)' : ''}:\n${matchPaths.join('\n')}`

    return {
      success: true,
      output,
      metadata: { count: matches.length, truncated, pattern: input.pattern, searchDir },
      locations: matches.map((m) => ({ path: m.path, type: 'read' as const })),
    }
  },
})
