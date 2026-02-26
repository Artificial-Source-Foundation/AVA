/**
 * grep tool — search file contents by regex.
 */

import * as nodePath from 'node:path'
import * as z from 'zod'
import { getPlatform } from '../platform.js'
import { defineTool } from './define.js'
import { ToolError, ToolErrorType } from './errors.js'
import {
  isBinaryFile,
  LIMITS,
  matchesGlob,
  resolvePath,
  shouldSkipDirectory,
  truncate,
} from './utils.js'

const schema = z.object({
  pattern: z.string().describe('Regular expression pattern to search for'),
  path: z.string().optional().describe('Search root directory (defaults to working directory)'),
  include: z.string().optional().describe('Glob pattern to filter filenames (e.g., "*.ts")'),
})

interface Match {
  file: string
  line: number
  content: string
}

export const grepTool = defineTool({
  name: 'grep',
  description:
    'Search file contents using a regular expression. Returns matching lines grouped by file.',
  schema,
  permissions: ['read'],

  validate(params) {
    const parsed = z.parse(schema, params)
    // Validate regex
    try {
      new RegExp(parsed.pattern)
    } catch {
      throw new ToolError(
        `Invalid regex pattern: ${parsed.pattern}`,
        ToolErrorType.INVALID_PATTERN,
        'grep'
      )
    }
    return parsed
  },

  async execute(input, ctx) {
    const fs = getPlatform().fs
    const searchDir = input.path
      ? resolvePath(input.path, ctx.workingDirectory)
      : ctx.workingDirectory

    const regex = new RegExp(input.pattern, 'g')
    const matches: Match[] = []
    let truncated = false

    async function searchFile(filePath: string): Promise<void> {
      if (await isBinaryFile(filePath)) return

      let content: string
      try {
        content = await fs.readFile(filePath)
      } catch {
        return
      }

      const lines = content.split('\n')
      for (let i = 0; i < lines.length; i++) {
        if (matches.length >= LIMITS.MAX_RESULTS) {
          truncated = true
          return
        }
        regex.lastIndex = 0
        if (regex.test(lines[i])) {
          matches.push({
            file: filePath,
            line: i + 1,
            content: truncate(lines[i].trim(), LIMITS.MAX_LINE_LENGTH),
          })
        }
      }
    }

    async function searchDirectory(dir: string): Promise<void> {
      if (ctx.signal.aborted || truncated) return

      let entries: Awaited<ReturnType<typeof fs.readDirWithTypes>>
      try {
        entries = await fs.readDirWithTypes(dir)
      } catch {
        return
      }

      for (const entry of entries) {
        if (ctx.signal.aborted || truncated) return

        const entryPath = nodePath.join(dir, entry.name)

        if (entry.isDirectory) {
          if (!shouldSkipDirectory(entry.name)) {
            await searchDirectory(entryPath)
          }
        } else if (entry.isFile) {
          if (input.include && !matchesGlob(entry.name, input.include)) {
            continue
          }
          await searchFile(entryPath)
        }
      }
    }

    await searchDirectory(searchDir)

    if (matches.length === 0) {
      return {
        success: true,
        output: `No matches found for "${input.pattern}" in ${searchDir}`,
        metadata: { count: 0, fileCount: 0, truncated: false, pattern: input.pattern, searchDir },
      }
    }

    // Group by file
    const byFile = new Map<string, Match[]>()
    for (const match of matches) {
      const existing = byFile.get(match.file)
      if (existing) {
        existing.push(match)
      } else {
        byFile.set(match.file, [match])
      }
    }

    const lines: string[] = [
      `Found ${matches.length} match(es) in ${byFile.size} file(s)${truncated ? ' (truncated)' : ''}:`,
      '',
    ]

    for (const [file, fileMatches] of byFile) {
      lines.push(`${file}:`)
      for (const m of fileMatches) {
        lines.push(`  Line ${m.line}: ${m.content}`)
      }
      lines.push('')
    }

    return {
      success: true,
      output: lines.join('\n'),
      metadata: {
        count: matches.length,
        fileCount: byFile.size,
        truncated,
        pattern: input.pattern,
        searchDir,
        include: input.include,
      },
      locations: [...byFile.keys()].map((f) => ({ path: f, type: 'read' as const })),
    }
  },
})
