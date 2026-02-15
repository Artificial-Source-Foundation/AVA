/**
 * Delete File Tool
 * Remove a file from the filesystem
 */

import { getPlatform } from '../platform.js'
import { ToolError, ToolErrorType } from './errors.js'
import type { Tool, ToolContext, ToolResult } from './types.js'
import { resolvePathSafe } from './utils.js'

// ============================================================================
// Types
// ============================================================================

interface DeleteParams {
  path: string
}

// ============================================================================
// Implementation
// ============================================================================

export const deleteTool: Tool<DeleteParams> = {
  definition: {
    name: 'delete_file',
    description:
      'Delete a file from the filesystem. Cannot delete directories - only individual files.',
    input_schema: {
      type: 'object',
      properties: {
        path: {
          type: 'string',
          description: 'Path to the file to delete (absolute or relative to working directory)',
        },
      },
      required: ['path'],
    },
  },

  validate(params: unknown): DeleteParams {
    if (typeof params !== 'object' || params === null) {
      throw new ToolError(
        'Invalid params: expected object',
        ToolErrorType.INVALID_PARAMS,
        'delete_file'
      )
    }

    const { path } = params as Record<string, unknown>

    if (typeof path !== 'string' || !path.trim()) {
      throw new ToolError(
        'Invalid path: must be non-empty string',
        ToolErrorType.INVALID_PARAMS,
        'delete_file'
      )
    }

    return {
      path: path.trim(),
    }
  },

  async execute(params: DeleteParams, ctx: ToolContext): Promise<ToolResult> {
    const fs = getPlatform().fs
    const filePath = await resolvePathSafe(params.path, ctx.workingDirectory)

    // Check abort signal
    if (ctx.signal.aborted) {
      return {
        success: false,
        output: 'Operation was cancelled',
        error: ToolErrorType.EXECUTION_ABORTED,
      }
    }

    // Check if file exists
    try {
      if (!(await fs.exists(filePath))) {
        return {
          success: false,
          output: `File not found: ${filePath}`,
          error: ToolErrorType.FILE_NOT_FOUND,
        }
      }
    } catch {
      return {
        success: false,
        output: `Cannot access path: ${filePath}`,
        error: ToolErrorType.UNKNOWN,
      }
    }

    // Check if it's a directory
    try {
      const fileStat = await fs.stat(filePath)
      if (fileStat.isDirectory) {
        return {
          success: false,
          output: `Cannot delete directory: ${filePath}\nUse shell commands for directory deletion.`,
          error: ToolErrorType.PATH_IS_DIRECTORY,
        }
      }
    } catch {
      // stat failed but exists succeeded - proceed with deletion attempt
    }

    // Check abort again before delete
    if (ctx.signal.aborted) {
      return {
        success: false,
        output: 'Operation was cancelled',
        error: ToolErrorType.EXECUTION_ABORTED,
      }
    }

    // Delete file
    try {
      await fs.remove(filePath)

      return {
        success: true,
        output: `Deleted file: ${filePath}`,
        metadata: {
          filePath,
        },
        locations: [{ path: filePath, type: 'delete' }],
      }
    } catch (err) {
      const message = err instanceof Error ? err.message : String(err)
      return {
        success: false,
        output: `Error deleting file: ${message}`,
        error: ToolErrorType.UNKNOWN,
      }
    }
  },
}
