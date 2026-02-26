/**
 * ls tool — directory listing.
 */

import { getPlatform } from '@ava/core-v2/platform'
import { defineTool } from '@ava/core-v2/tools'
import * as z from 'zod'

const DEFAULT_IGNORES = new Set([
  'node_modules',
  '.git',
  '.svn',
  'dist',
  'build',
  'coverage',
  '__pycache__',
  '.venv',
  'venv',
  '.tox',
  '.mypy_cache',
  '.next',
  '.nuxt',
  '.output',
  '.cache',
])

export const lsTool = defineTool({
  name: 'ls',
  description: 'List directory contents. Returns a tree-view listing.',
  schema: z.object({
    path: z.string().optional().describe('Directory to list (defaults to working directory)'),
    maxFiles: z.number().optional().describe('Maximum files to return (default 100)'),
  }),
  permissions: ['read'],
  locations: (input) => [{ path: input.path ?? '.', type: 'read' }],
  async execute(input, ctx) {
    if (ctx.signal?.aborted) {
      return { success: false, output: '', error: 'Aborted' }
    }

    const fs = getPlatform().fs
    const dirPath = input.path || ctx.workingDirectory
    const maxFiles = input.maxFiles ?? 100

    try {
      const entries = await fs.readDir(dirPath)
      const filtered = entries.filter((name: string) => !DEFAULT_IGNORES.has(name))
      const limited = filtered.slice(0, maxFiles)
      const output = limited.join('\n')
      const suffix =
        filtered.length > maxFiles ? `\n... and ${filtered.length - maxFiles} more` : ''
      return { success: true, output: output + suffix }
    } catch (err) {
      return {
        success: false,
        output: '',
        error: `Failed to list ${dirPath}: ${err instanceof Error ? err.message : String(err)}`,
      }
    }
  },
})
