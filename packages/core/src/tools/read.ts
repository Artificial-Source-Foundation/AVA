/**
 * Read File Tool
 * Read contents of a file with pagination support
 */

import { getPlatform } from '../platform.js'
import { ToolError, ToolErrorType } from './errors.js'
import type { Tool, ToolContext, ToolResult } from './types.js'
import { formatLineNumber, isBinaryFile, LIMITS, resolvePathSafe, truncate } from './utils.js'

// ============================================================================
// Types
// ============================================================================

interface ReadParams {
  path: string
  offset?: number
  limit?: number
}

// ============================================================================
// Implementation
// ============================================================================

export const readTool: Tool<ReadParams> = {
  definition: {
    name: 'read_file',
    description: `Read contents of a file. For large files, use offset and limit to paginate. Default limit is ${LIMITS.MAX_LINES} lines. Lines over ${LIMITS.MAX_LINE_LENGTH} characters are truncated. Returns line-numbered output.`,
    input_schema: {
      type: 'object',
      properties: {
        path: {
          type: 'string',
          description: 'Path to the file to read (absolute or relative to working directory)',
        },
        offset: {
          type: 'number',
          description: 'Line number to start reading from (0-based). Default: 0',
        },
        limit: {
          type: 'number',
          description: `Maximum number of lines to read. Default: ${LIMITS.MAX_LINES}`,
        },
      },
      required: ['path'],
    },
  },

  validate(params: unknown): ReadParams {
    if (typeof params !== 'object' || params === null) {
      throw new ToolError(
        'Invalid params: expected object',
        ToolErrorType.INVALID_PARAMS,
        'read_file'
      )
    }

    const { path, offset, limit } = params as Record<string, unknown>

    if (typeof path !== 'string' || !path.trim()) {
      throw new ToolError(
        'Invalid path: must be non-empty string',
        ToolErrorType.INVALID_PARAMS,
        'read_file'
      )
    }

    if (offset !== undefined && (typeof offset !== 'number' || offset < 0)) {
      throw new ToolError(
        'Invalid offset: must be non-negative number',
        ToolErrorType.INVALID_PARAMS,
        'read_file'
      )
    }

    if (limit !== undefined && (typeof limit !== 'number' || limit <= 0)) {
      throw new ToolError(
        'Invalid limit: must be positive number',
        ToolErrorType.INVALID_PARAMS,
        'read_file'
      )
    }

    return {
      path: path.trim(),
      offset: offset as number | undefined,
      limit: limit as number | undefined,
    }
  },

  async execute(params: ReadParams, ctx: ToolContext): Promise<ToolResult> {
    const fs = getPlatform().fs
    const filePath = await resolvePathSafe(params.path, ctx.workingDirectory)

    // Check if file exists and is not a directory
    try {
      const fileStat = await fs.stat(filePath)
      if (fileStat.isDirectory) {
        return {
          success: false,
          output: `Cannot read directory: ${filePath}\nUse glob tool to list directory contents.`,
          error: ToolErrorType.PATH_IS_DIRECTORY,
        }
      }
    } catch {
      return {
        success: false,
        output: `File not found: ${filePath}`,
        error: ToolErrorType.FILE_NOT_FOUND,
      }
    }

    // Check for binary file
    if (await isBinaryFile(filePath)) {
      return {
        success: false,
        output: `Cannot read binary file: ${filePath}`,
        error: ToolErrorType.BINARY_FILE,
      }
    }

    // Check abort
    if (ctx.signal.aborted) {
      return {
        success: false,
        output: 'Operation was cancelled',
        error: 'ABORTED',
      }
    }

    // Read file content
    try {
      const content = await fs.readFile(filePath)
      const lines = content.split('\n')
      const totalLines = lines.length

      const offset = params.offset || 0
      const limit = params.limit || LIMITS.MAX_LINES

      // Validate offset
      if (offset >= totalLines) {
        return {
          success: true,
          output: `Offset ${offset} is beyond end of file (${totalLines} lines)`,
          metadata: { totalLines, offset, linesRead: 0 },
        }
      }

      // Extract requested range with limits
      const outputLines: string[] = []
      let bytes = 0
      let bytesTruncated = false

      const endLine = Math.min(totalLines, offset + limit)
      for (let i = offset; i < endLine; i++) {
        let line = lines[i]

        // Truncate long lines
        if (line.length > LIMITS.MAX_LINE_LENGTH) {
          line = truncate(line, LIMITS.MAX_LINE_LENGTH)
        }

        // Check byte limit
        const lineBytes = new TextEncoder().encode(line).length + 1 // +1 for newline
        if (bytes + lineBytes > LIMITS.MAX_BYTES) {
          bytesTruncated = true
          break
        }

        outputLines.push(`${formatLineNumber(i + 1, totalLines)}| ${line}`)
        bytes += lineBytes
      }

      const lastReadLine = offset + outputLines.length
      const hasMore = lastReadLine < totalLines || bytesTruncated
      const truncatedResult = hasMore

      // Build output
      let output = '<file>\n'
      output += outputLines.join('\n')

      if (truncatedResult) {
        if (bytesTruncated) {
          output += `\n\n(Output truncated at ${LIMITS.MAX_BYTES / 1024}KB. Use offset=${lastReadLine} to continue.)`
        } else {
          output += `\n\n(Showing lines ${offset + 1}-${lastReadLine} of ${totalLines}. Use offset=${lastReadLine} to continue.)`
        }
      } else {
        output += `\n\n(End of file - ${totalLines} lines)`
      }
      output += '\n</file>'

      return {
        success: true,
        output,
        metadata: {
          filePath,
          totalLines,
          linesRead: outputLines.length,
          offset,
          truncated: truncatedResult,
          bytesTruncated,
        },
        locations: [{ path: filePath, type: 'read', lines: [offset + 1, lastReadLine] }],
      }
    } catch (err) {
      const message = err instanceof Error ? err.message : String(err)
      return {
        success: false,
        output: `Error reading file: ${message}`,
        error: ToolErrorType.UNKNOWN,
      }
    }
  },
}
