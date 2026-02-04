/**
 * Multi-Edit Tool
 * Apply multiple sequential edits to a single file atomically
 *
 * Based on OpenCode's multiedit tool pattern
 */

import { z } from 'zod'
import { getPlatform } from '../platform.js'
import { defineTool } from './define.js'
import { replace } from './edit.js'
import { DEFAULT_REPLACERS } from './edit-replacers.js'
import { ToolErrorType } from './errors.js'
import { sanitizeContent } from './sanitize.js'
import type { ToolResult } from './types.js'
import { resolvePath } from './utils.js'

// ============================================================================
// Constants
// ============================================================================

/** Maximum file size to edit (5MB) */
const MAX_FILE_SIZE = 5 * 1024 * 1024

/** Maximum number of edits per call */
const MAX_EDITS = 50

// ============================================================================
// Schema
// ============================================================================

const EditSchema = z.object({
  oldString: z.string().describe('The exact text to replace'),
  newString: z.string().describe('The replacement text'),
})

const MultiEditSchema = z.object({
  filePath: z.string().describe('Absolute path to the file to modify'),
  edits: z
    .array(EditSchema)
    .min(1)
    .max(MAX_EDITS)
    .describe(`Array of edits to apply sequentially (1-${MAX_EDITS})`),
})

type MultiEditParams = z.infer<typeof MultiEditSchema>

// ============================================================================
// Tool Implementation
// ============================================================================

export const multieditTool = defineTool({
  name: 'multiedit',
  description: `Apply multiple edits to a single file atomically.

Key features:
- All edits are applied in order, atomically (all-or-nothing)
- If any edit fails, the file is not modified
- Uses the same fuzzy matching as the edit tool
- More efficient than multiple individual edit calls

Usage rules:
1. Each edit's oldString must be unique in the file content at that point
2. Edits are applied sequentially - later edits see the result of earlier ones
3. If any edit fails, the entire operation is rolled back
4. Maximum ${MAX_EDITS} edits per call

Example:
\`\`\`json
{
  "filePath": "/path/to/file.ts",
  "edits": [
    { "oldString": "const foo = 1", "newString": "const foo = 42" },
    { "oldString": "function bar()", "newString": "function bar(x: number)" }
  ]
}
\`\`\``,

  schema: MultiEditSchema,

  permissions: ['write'],

  locations: (input) => [{ path: input.filePath, type: 'write' }],

  async execute(params: MultiEditParams, ctx): Promise<ToolResult> {
    const fs = getPlatform().fs

    // Resolve file path
    const filePath = resolvePath(params.filePath, ctx.workingDirectory)

    // Check abort signal
    if (ctx.signal.aborted) {
      return {
        success: false,
        output: 'Operation was cancelled',
        error: ToolErrorType.EXECUTION_ABORTED,
      }
    }

    try {
      // Check file exists
      const stat = await fs.stat(filePath)
      if (!stat.isFile) {
        return {
          success: false,
          output: `Path is a directory, not a file: ${filePath}`,
          error: ToolErrorType.PATH_IS_DIRECTORY,
        }
      }

      // Check file size
      if (stat.size > MAX_FILE_SIZE) {
        return {
          success: false,
          output: `File too large to edit (${(stat.size / 1024 / 1024).toFixed(1)}MB). Maximum: 5MB`,
          error: ToolErrorType.CONTENT_TOO_LARGE,
        }
      }

      // Read current content
      const originalContent = await fs.readFile(filePath)
      let content = originalContent
      const appliedEdits: Array<{ index: number; oldString: string; newString: string }> = []

      // Apply edits sequentially
      for (let i = 0; i < params.edits.length; i++) {
        const edit = params.edits[i]

        // Check abort between edits
        if (ctx.signal.aborted) {
          return {
            success: false,
            output: 'Operation was cancelled during edit sequence',
            error: ToolErrorType.EXECUTION_ABORTED,
          }
        }

        // Skip no-op edits
        if (edit.oldString === edit.newString) {
          continue
        }

        // Sanitize newString
        const sanitizedNewString = sanitizeContent(edit.newString)

        try {
          content = replace(content, edit.oldString, sanitizedNewString, false, DEFAULT_REPLACERS)
          appliedEdits.push({
            index: i,
            oldString: edit.oldString.slice(0, 50) + (edit.oldString.length > 50 ? '...' : ''),
            newString:
              sanitizedNewString.slice(0, 50) + (sanitizedNewString.length > 50 ? '...' : ''),
          })
        } catch (err) {
          const message = err instanceof Error ? err.message : String(err)
          return {
            success: false,
            output: `Edit #${i + 1} failed: ${message}\n\nNo changes were made to the file.`,
            error: ToolErrorType.INVALID_PARAMS,
            metadata: {
              filePath,
              failedEditIndex: i,
              failedEditOldString: edit.oldString.slice(0, 100),
              appliedEditsBeforeFailure: appliedEdits.length,
            },
          }
        }
      }

      // All edits succeeded - write the result
      if (content !== originalContent) {
        await fs.writeFile(filePath, content)
      }

      // Calculate stats
      const oldLines = originalContent.split('\n').length
      const newLines = content.split('\n').length
      const linesDelta = newLines - oldLines

      let output = `Multi-edit applied successfully: ${appliedEdits.length} edits`
      if (linesDelta > 0) {
        output += ` (+${linesDelta} lines)`
      } else if (linesDelta < 0) {
        output += ` (${linesDelta} lines)`
      }

      // Add summary of edits
      output += '\n\n**Applied edits:**\n'
      for (const edit of appliedEdits) {
        output += `- Edit #${edit.index + 1}: "${edit.oldString}" → "${edit.newString}"\n`
      }

      // Stream metadata if available
      if (ctx.metadata) {
        ctx.metadata({
          title: `Multi-edited ${filePath.split('/').pop()}`,
          metadata: {
            editCount: appliedEdits.length,
            linesDelta,
          },
        })
      }

      return {
        success: true,
        output,
        metadata: {
          filePath,
          editCount: appliedEdits.length,
          totalEditsRequested: params.edits.length,
          oldLines,
          newLines,
          linesDelta,
        },
        locations: [{ path: filePath, type: 'write' }],
      }
    } catch (err) {
      // Handle file not found
      if (err instanceof Error && err.message.includes('ENOENT')) {
        return {
          success: false,
          output: `File not found: ${filePath}`,
          error: ToolErrorType.FILE_NOT_FOUND,
        }
      }

      // Handle permission errors
      if (err instanceof Error && err.message.includes('EACCES')) {
        return {
          success: false,
          output: `Permission denied: ${filePath}`,
          error: ToolErrorType.PERMISSION_DENIED,
        }
      }

      const message = err instanceof Error ? err.message : String(err)
      return {
        success: false,
        output: `Error editing file: ${message}`,
        error: ToolErrorType.UNKNOWN,
      }
    }
  },
})
