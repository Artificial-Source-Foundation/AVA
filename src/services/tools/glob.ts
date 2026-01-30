/**
 * Glob Tool
 * Find files matching a glob pattern
 */

import { readDir, stat } from '@tauri-apps/plugin-fs'
import { ToolError, ToolErrorType } from './errors'
import type { Tool, ToolContext, ToolResult } from './types'
import { LIMITS, matchesGlob, resolvePath, shouldSkipDirectory } from './utils'

// ============================================================================
// Types
// ============================================================================

interface GlobParams {
  pattern: string
  path?: string
}

// ============================================================================
// Implementation
// ============================================================================

export const globTool: Tool<GlobParams> = {
  definition: {
    name: 'glob',
    description: `Find files matching a glob pattern. Supports * (any chars in single directory), ** (recursive), and {a,b} alternatives. Returns up to ${LIMITS.MAX_RESULTS} files sorted by modification time (newest first). Skips hidden directories and node_modules.`,
    input_schema: {
      type: 'object',
      properties: {
        pattern: {
          type: 'string',
          description: 'Glob pattern to match (e.g., "**/*.ts", "src/**/*.tsx", "*.{js,ts}")',
        },
        path: {
          type: 'string',
          description: 'Directory to search in. Defaults to working directory.',
        },
      },
      required: ['pattern'],
    },
  },

  validate(params: unknown): GlobParams {
    if (typeof params !== 'object' || params === null) {
      throw new ToolError('Invalid params: expected object', ToolErrorType.INVALID_PARAMS, 'glob')
    }

    const { pattern, path } = params as Record<string, unknown>

    if (typeof pattern !== 'string' || !pattern.trim()) {
      throw new ToolError(
        'Invalid pattern: must be non-empty string',
        ToolErrorType.INVALID_PARAMS,
        'glob'
      )
    }

    return {
      pattern: pattern.trim(),
      path: typeof path === 'string' ? path.trim() : undefined,
    }
  },

  async execute(params: GlobParams, ctx: ToolContext): Promise<ToolResult> {
    const searchDir = params.path
      ? resolvePath(params.path, ctx.workingDirectory)
      : ctx.workingDirectory

    const matches: Array<{ path: string; mtime: number }> = []
    let truncated = false

    /**
     * Recursively search directory for matching files
     */
    async function searchDirectory(dir: string, relativePath: string = ''): Promise<void> {
      // Check abort signal
      if (ctx.signal.aborted) return

      // Check if we've hit the limit
      if (matches.length >= LIMITS.MAX_RESULTS) {
        truncated = true
        return
      }

      try {
        const entries = await readDir(dir)

        for (const entry of entries) {
          if (ctx.signal.aborted || matches.length >= LIMITS.MAX_RESULTS) break

          const fullPath = `${dir}/${entry.name}`
          const relPath = relativePath ? `${relativePath}/${entry.name}` : entry.name

          if (entry.isDirectory) {
            // Skip directories that should be ignored
            if (!shouldSkipDirectory(entry.name)) {
              await searchDirectory(fullPath, relPath)
            }
          } else if (entry.isFile) {
            // Check if file matches pattern
            if (matchesGlob(relPath, params.pattern)) {
              try {
                const fileStat = await stat(fullPath)
                matches.push({
                  path: fullPath,
                  mtime: fileStat.mtime?.getTime() || 0,
                })
              } catch {
                // If we can't stat the file, still include it with mtime 0
                matches.push({ path: fullPath, mtime: 0 })
              }
            }
          }
        }
      } catch (err) {
        // Skip directories we can't read (permissions, etc.)
        console.warn(`Failed to read directory ${dir}:`, err)
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

    // Sort by modification time (newest first)
    matches.sort((a, b) => b.mtime - a.mtime)

    // Build output
    if (matches.length === 0) {
      return {
        success: true,
        output: `No files found matching pattern: ${params.pattern}`,
        metadata: { count: 0, truncated: false, pattern: params.pattern },
      }
    }

    let output = `Found ${matches.length} file${matches.length === 1 ? '' : 's'}:\n\n`
    output += matches.map((m) => m.path).join('\n')

    if (truncated) {
      output += `\n\n(Results truncated at ${LIMITS.MAX_RESULTS}. Use a more specific pattern or path.)`
    }

    return {
      success: true,
      output,
      metadata: {
        count: matches.length,
        truncated,
        pattern: params.pattern,
        searchDir,
      },
    }
  },
}
