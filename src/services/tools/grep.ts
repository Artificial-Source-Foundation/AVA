/**
 * Grep Tool
 * Search file contents using regex patterns
 */

import { readDir, readTextFile } from '@tauri-apps/plugin-fs'
import { ToolError, ToolErrorType } from './errors'
import type { Tool, ToolContext, ToolResult } from './types'
import {
  isBinaryFile,
  LIMITS,
  matchesGlob,
  resolvePath,
  shouldSkipDirectory,
  truncate,
} from './utils'

// ============================================================================
// Types
// ============================================================================

interface GrepParams {
  pattern: string
  path?: string
  include?: string
}

interface Match {
  file: string
  line: number
  content: string
}

// ============================================================================
// Implementation
// ============================================================================

export const grepTool: Tool<GrepParams> = {
  definition: {
    name: 'grep',
    description: `Search file contents using regex patterns. Returns up to ${LIMITS.MAX_RESULTS} matches grouped by file. Use 'include' to filter by filename pattern.`,
    input_schema: {
      type: 'object',
      properties: {
        pattern: {
          type: 'string',
          description: 'Regex pattern to search for (e.g., "function\\s+\\w+", "TODO:")',
        },
        path: {
          type: 'string',
          description: 'Directory to search in. Defaults to working directory.',
        },
        include: {
          type: 'string',
          description: 'Glob pattern to filter files (e.g., "*.ts", "*.{js,tsx}")',
        },
      },
      required: ['pattern'],
    },
  },

  validate(params: unknown): GrepParams {
    if (typeof params !== 'object' || params === null) {
      throw new ToolError('Invalid params: expected object', ToolErrorType.INVALID_PARAMS, 'grep')
    }

    const { pattern, path, include } = params as Record<string, unknown>

    if (typeof pattern !== 'string' || !pattern.trim()) {
      throw new ToolError(
        'Invalid pattern: must be non-empty string',
        ToolErrorType.INVALID_PARAMS,
        'grep'
      )
    }

    // Validate regex
    try {
      new RegExp(pattern)
    } catch (err) {
      throw new ToolError(
        `Invalid regex pattern: ${err instanceof Error ? err.message : 'syntax error'}`,
        ToolErrorType.INVALID_PATTERN,
        'grep'
      )
    }

    return {
      pattern: pattern.trim(),
      path: typeof path === 'string' ? path.trim() : undefined,
      include: typeof include === 'string' ? include.trim() : undefined,
    }
  },

  async execute(params: GrepParams, ctx: ToolContext): Promise<ToolResult> {
    const searchDir = params.path
      ? resolvePath(params.path, ctx.workingDirectory)
      : ctx.workingDirectory

    const regex = new RegExp(params.pattern, 'g')
    const matches: Match[] = []
    let truncated = false

    /**
     * Search a single file for matches
     */
    async function searchFile(filePath: string): Promise<void> {
      if (ctx.signal.aborted || matches.length >= LIMITS.MAX_RESULTS) return

      try {
        // Skip binary files
        if (await isBinaryFile(filePath)) return

        const content = await readTextFile(filePath)
        const lines = content.split('\n')

        for (let i = 0; i < lines.length && matches.length < LIMITS.MAX_RESULTS; i++) {
          const line = lines[i]

          // Reset regex lastIndex for global flag
          regex.lastIndex = 0

          if (regex.test(line)) {
            matches.push({
              file: filePath,
              line: i + 1,
              content: truncate(line.trim(), LIMITS.MAX_LINE_LENGTH),
            })

            if (matches.length >= LIMITS.MAX_RESULTS) {
              truncated = true
              return
            }
          }
        }
      } catch {
        // Skip files that can't be read
      }
    }

    /**
     * Recursively search directory
     */
    async function searchDirectory(dir: string): Promise<void> {
      if (ctx.signal.aborted || matches.length >= LIMITS.MAX_RESULTS) return

      try {
        const entries = await readDir(dir)

        for (const entry of entries) {
          if (ctx.signal.aborted || matches.length >= LIMITS.MAX_RESULTS) break

          const fullPath = `${dir}/${entry.name}`

          if (entry.isDirectory) {
            if (!shouldSkipDirectory(entry.name)) {
              await searchDirectory(fullPath)
            }
          } else if (entry.isFile) {
            // Check include pattern if specified
            if (params.include && !matchesGlob(entry.name, params.include)) {
              continue
            }

            await searchFile(fullPath)
          }
        }
      } catch {
        // Skip directories we can't read
      }
    }

    // Start search
    await searchDirectory(searchDir)

    // Check abort
    if (ctx.signal.aborted) {
      return {
        success: false,
        output: 'Operation was cancelled',
        error: 'ABORTED',
      }
    }

    // Build output
    if (matches.length === 0) {
      return {
        success: true,
        output: `No matches found for pattern: ${params.pattern}`,
        metadata: { count: 0, truncated: false, pattern: params.pattern },
      }
    }

    // Group matches by file
    const byFile = new Map<string, Match[]>()
    for (const match of matches) {
      const existing = byFile.get(match.file) || []
      existing.push(match)
      byFile.set(match.file, existing)
    }

    // Format output
    let output = `Found ${matches.length} match${matches.length === 1 ? '' : 'es'} in ${byFile.size} file${byFile.size === 1 ? '' : 's'}:\n`

    for (const [file, fileMatches] of byFile) {
      output += `\n${file}:\n`
      for (const match of fileMatches) {
        output += `  Line ${match.line}: ${match.content}\n`
      }
    }

    if (truncated) {
      output += `\n(Results truncated at ${LIMITS.MAX_RESULTS}. Use a more specific pattern or path.)`
    }

    return {
      success: true,
      output,
      metadata: {
        count: matches.length,
        fileCount: byFile.size,
        truncated,
        pattern: params.pattern,
        searchDir,
        include: params.include,
      },
    }
  },
}
