/**
 * Write File Tool
 * Write content to a file, creating or overwriting as needed
 */

import { getPlatform } from '../platform.js'
import { ToolError, ToolErrorType } from './errors.js'
import type { Tool, ToolContext, ToolResult } from './types.js'
import { LIMITS, resolvePath } from './utils.js'

// ============================================================================
// Types
// ============================================================================

interface WriteParams {
  path: string
  content: string
}

// ============================================================================
// Implementation
// ============================================================================

export const writeTool: Tool<WriteParams> = {
  definition: {
    name: 'write_file',
    description: `Write content to a file. Creates the file if it doesn't exist, or overwrites if it does. For new files where you want to prevent accidental overwrites, use create_file instead. Maximum content size is ${LIMITS.MAX_BYTES / 1024}KB. Parent directories are created automatically.`,
    input_schema: {
      type: 'object',
      properties: {
        path: {
          type: 'string',
          description: 'Path to the file (absolute or relative to working directory)',
        },
        content: {
          type: 'string',
          description: 'Content to write to the file',
        },
      },
      required: ['path', 'content'],
    },
  },

  validate(params: unknown): WriteParams {
    if (typeof params !== 'object' || params === null) {
      throw new ToolError(
        'Invalid params: expected object',
        ToolErrorType.INVALID_PARAMS,
        'write_file'
      )
    }

    const { path, content } = params as Record<string, unknown>

    if (typeof path !== 'string' || !path.trim()) {
      throw new ToolError(
        'Invalid path: must be non-empty string',
        ToolErrorType.INVALID_PARAMS,
        'write_file'
      )
    }

    if (typeof content !== 'string') {
      throw new ToolError(
        'Invalid content: must be string',
        ToolErrorType.INVALID_PARAMS,
        'write_file'
      )
    }

    // Check content size
    const contentBytes = new TextEncoder().encode(content).length
    if (contentBytes > LIMITS.MAX_BYTES) {
      throw new ToolError(
        `Content too large: ${contentBytes} bytes exceeds limit of ${LIMITS.MAX_BYTES} bytes`,
        ToolErrorType.CONTENT_TOO_LARGE,
        'write_file'
      )
    }

    return {
      path: path.trim(),
      content,
    }
  },

  async execute(params: WriteParams, ctx: ToolContext): Promise<ToolResult> {
    const fs = getPlatform().fs
    const filePath = resolvePath(params.path, ctx.workingDirectory)

    // Check abort signal
    if (ctx.signal.aborted) {
      return {
        success: false,
        output: 'Operation was cancelled',
        error: ToolErrorType.EXECUTION_ABORTED,
      }
    }

    // Check if path is a directory
    let fileExisted = false
    try {
      if (await fs.exists(filePath)) {
        const fileStat = await fs.stat(filePath)
        if (fileStat.isDirectory) {
          return {
            success: false,
            output: `Cannot write to directory: ${filePath}`,
            error: ToolErrorType.PATH_IS_DIRECTORY,
          }
        }
        fileExisted = true
      }
    } catch {
      // exists() or stat() failed - proceed with creation
    }

    // Create parent directories if needed
    const parentDir = filePath.substring(0, filePath.lastIndexOf('/'))
    if (parentDir) {
      try {
        await fs.mkdir(parentDir)
      } catch {
        // Directory might already exist, continue
      }
    }

    // Check abort again before write
    if (ctx.signal.aborted) {
      return {
        success: false,
        output: 'Operation was cancelled',
        error: ToolErrorType.EXECUTION_ABORTED,
      }
    }

    // Write file
    try {
      await fs.writeFile(filePath, params.content)

      const lineCount = params.content.split('\n').length
      const byteCount = new TextEncoder().encode(params.content).length
      const action = fileExisted ? 'Updated' : 'Created'

      return {
        success: true,
        output: `${action} file: ${filePath}\n${lineCount} lines, ${byteCount} bytes`,
        metadata: {
          filePath,
          lines: lineCount,
          bytes: byteCount,
          overwritten: fileExisted,
        },
        locations: [{ path: filePath, type: 'write' }],
      }
    } catch (err) {
      const message = err instanceof Error ? err.message : String(err)
      return {
        success: false,
        output: `Error writing file: ${message}`,
        error: ToolErrorType.UNKNOWN,
      }
    }
  },
}
