/**
 * Create File Tool
 * Create a new file with content (fails if file already exists)
 */

import { getPlatform } from '../platform.js'
import { ToolError, ToolErrorType } from './errors.js'
import type { Tool, ToolContext, ToolResult } from './types.js'
import { LIMITS, resolvePath } from './utils.js'

// ============================================================================
// Types
// ============================================================================

interface CreateParams {
  path: string
  content: string
}

// ============================================================================
// Implementation
// ============================================================================

export const createTool: Tool<CreateParams> = {
  definition: {
    name: 'create_file',
    description: `Create a new file with the specified content. Fails if the file already exists - use write_file to overwrite existing files. Maximum content size is ${LIMITS.MAX_BYTES / 1024}KB. Parent directories are created automatically.`,
    input_schema: {
      type: 'object',
      properties: {
        path: {
          type: 'string',
          description: 'Path to the new file (absolute or relative to working directory)',
        },
        content: {
          type: 'string',
          description: 'Content to write to the file',
        },
      },
      required: ['path', 'content'],
    },
  },

  validate(params: unknown): CreateParams {
    if (typeof params !== 'object' || params === null) {
      throw new ToolError(
        'Invalid params: expected object',
        ToolErrorType.INVALID_PARAMS,
        'create_file'
      )
    }

    const { path, content } = params as Record<string, unknown>

    if (typeof path !== 'string' || !path.trim()) {
      throw new ToolError(
        'Invalid path: must be non-empty string',
        ToolErrorType.INVALID_PARAMS,
        'create_file'
      )
    }

    if (typeof content !== 'string') {
      throw new ToolError(
        'Invalid content: must be string',
        ToolErrorType.INVALID_PARAMS,
        'create_file'
      )
    }

    // Check content size
    const contentBytes = new TextEncoder().encode(content).length
    if (contentBytes > LIMITS.MAX_BYTES) {
      throw new ToolError(
        `Content too large: ${contentBytes} bytes exceeds limit of ${LIMITS.MAX_BYTES} bytes`,
        ToolErrorType.CONTENT_TOO_LARGE,
        'create_file'
      )
    }

    return {
      path: path.trim(),
      content,
    }
  },

  async execute(params: CreateParams, ctx: ToolContext): Promise<ToolResult> {
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

    // Check if file already exists
    try {
      if (await fs.exists(filePath)) {
        // Check if it's a directory
        try {
          const fileStat = await fs.stat(filePath)
          if (fileStat.isDirectory) {
            return {
              success: false,
              output: `Cannot create file: path is a directory: ${filePath}`,
              error: ToolErrorType.PATH_IS_DIRECTORY,
            }
          }
        } catch {
          // stat failed, but exists returned true - treat as file
        }

        return {
          success: false,
          output: `File already exists: ${filePath}\nUse write_file to overwrite existing files.`,
          error: ToolErrorType.FILE_ALREADY_EXISTS,
        }
      }
    } catch {
      // exists() failed - proceed with creation (file likely doesn't exist)
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

      return {
        success: true,
        output: `Created file: ${filePath}\n${lineCount} lines, ${byteCount} bytes`,
        metadata: {
          filePath,
          lines: lineCount,
          bytes: byteCount,
        },
        locations: [{ path: filePath, type: 'write' }],
      }
    } catch (err) {
      const message = err instanceof Error ? err.message : String(err)
      return {
        success: false,
        output: `Error creating file: ${message}`,
        error: ToolErrorType.UNKNOWN,
      }
    }
  },
}
