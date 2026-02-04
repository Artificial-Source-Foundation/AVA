/**
 * Edit Tool
 * String replacement with fuzzy matching for robust code editing
 *
 * Based on OpenCode's edit.ts pattern with multiple replacer strategies
 */

import { getPlatform } from '../platform.js'
import { DEFAULT_REPLACERS, normalizeLineEndings, type Replacer } from './edit-replacers.js'
import { ToolError, ToolErrorType } from './errors.js'
import { sanitizeContent } from './sanitize.js'
import type { Tool, ToolContext, ToolResult } from './types.js'
import { resolvePath } from './utils.js'

// ============================================================================
// Types
// ============================================================================

interface EditParams {
  /** Absolute path to the file to modify */
  filePath: string
  /** Text to find and replace */
  oldString: string
  /** Text to replace it with */
  newString: string
  /** Replace all occurrences (default: false) */
  replaceAll?: boolean
}

// ============================================================================
// Constants
// ============================================================================

/** Maximum file size to edit (5MB) */
const MAX_FILE_SIZE = 5 * 1024 * 1024

// ============================================================================
// Core Replace Function
// ============================================================================

/**
 * Replace oldString with newString in content using fuzzy matching
 *
 * @param content - File content
 * @param oldString - String to find
 * @param newString - Replacement string
 * @param replaceAll - Replace all occurrences
 * @param replacers - Replacer strategies to use
 * @returns Modified content
 * @throws Error if oldString not found or ambiguous matches
 */
export function replace(
  content: string,
  oldString: string,
  newString: string,
  replaceAll = false,
  replacers: Replacer[] = DEFAULT_REPLACERS
): string {
  if (oldString === newString) {
    throw new Error('oldString and newString must be different')
  }

  // Normalize line endings for consistent matching
  const normalizedContent = normalizeLineEndings(content)
  const normalizedOld = normalizeLineEndings(oldString)

  let notFound = true

  for (const replacer of replacers) {
    for (const search of replacer(normalizedContent, normalizedOld)) {
      const index = normalizedContent.indexOf(search)
      if (index === -1) continue

      notFound = false

      if (replaceAll) {
        return normalizedContent.replaceAll(search, newString)
      }

      // Check for unique match (not ambiguous)
      const lastIndex = normalizedContent.lastIndexOf(search)
      if (index !== lastIndex) {
        continue // Multiple matches with this replacer, try next
      }

      // Single unique match found
      return (
        normalizedContent.substring(0, index) +
        newString +
        normalizedContent.substring(index + search.length)
      )
    }
  }

  if (notFound) {
    throw new Error('oldString not found in file content')
  }

  throw new Error(
    'Found multiple matches for oldString. Provide more surrounding context in oldString to identify the correct match, or use replaceAll to replace all occurrences.'
  )
}

/**
 * Generate a simple unified diff
 */
function generateDiff(filePath: string, oldContent: string, newContent: string): string {
  const oldLines = oldContent.split('\n')
  const newLines = newContent.split('\n')

  const changes: string[] = []
  changes.push(`--- ${filePath}`)
  changes.push(`+++ ${filePath}`)

  // Simple line-by-line diff (not optimal but functional)
  let i = 0
  let j = 0
  while (i < oldLines.length || j < newLines.length) {
    if (i >= oldLines.length) {
      // Remaining lines are additions
      changes.push(`+${newLines[j]}`)
      j++
    } else if (j >= newLines.length) {
      // Remaining lines are deletions
      changes.push(`-${oldLines[i]}`)
      i++
    } else if (oldLines[i] === newLines[j]) {
      // Same line
      changes.push(` ${oldLines[i]}`)
      i++
      j++
    } else {
      // Different - check if old line exists later in new
      const oldInNew = newLines.indexOf(oldLines[i], j)
      const newInOld = oldLines.indexOf(newLines[j], i)

      if (oldInNew === -1 || (newInOld !== -1 && newInOld < oldInNew)) {
        // Old line was deleted
        changes.push(`-${oldLines[i]}`)
        i++
      } else {
        // New line was added
        changes.push(`+${newLines[j]}`)
        j++
      }
    }
  }

  return changes.join('\n')
}

// ============================================================================
// Tool Implementation
// ============================================================================

export const editTool: Tool<EditParams> = {
  definition: {
    name: 'edit',
    description: `Performs exact string replacements in files. Use for editing existing files.

Key features:
- Uses fuzzy matching to handle whitespace and indentation differences
- Validates that oldString is unique to prevent accidental replacements
- Use replaceAll: true to replace all occurrences

Usage rules:
1. The edit will FAIL if oldString is not unique. Provide more surrounding context to make it unique.
2. Use replaceAll only when you intend to replace every occurrence.
3. Always preserve the exact indentation style of the file.
4. For new files, use the create tool instead.`,
    input_schema: {
      type: 'object',
      properties: {
        filePath: {
          type: 'string',
          description: 'Absolute path to the file to modify',
        },
        oldString: {
          type: 'string',
          description:
            'The exact text to replace. Include enough surrounding context to make it unique.',
        },
        newString: {
          type: 'string',
          description: 'The text to replace it with (must be different from oldString)',
        },
        replaceAll: {
          type: 'boolean',
          description: 'Replace all occurrences of oldString (default: false)',
        },
      },
      required: ['filePath', 'oldString', 'newString'],
    },
  },

  validate(params: unknown): EditParams {
    if (typeof params !== 'object' || params === null) {
      throw new ToolError('Invalid params: expected object', ToolErrorType.INVALID_PARAMS, 'edit')
    }

    const { filePath, oldString, newString, replaceAll } = params as Record<string, unknown>

    if (typeof filePath !== 'string' || !filePath.trim()) {
      throw new ToolError(
        'Invalid filePath: must be non-empty string',
        ToolErrorType.INVALID_PARAMS,
        'edit'
      )
    }

    if (typeof oldString !== 'string') {
      throw new ToolError('Invalid oldString: must be string', ToolErrorType.INVALID_PARAMS, 'edit')
    }

    if (typeof newString !== 'string') {
      throw new ToolError('Invalid newString: must be string', ToolErrorType.INVALID_PARAMS, 'edit')
    }

    if (oldString === newString) {
      throw new ToolError(
        'oldString and newString must be different',
        ToolErrorType.INVALID_PARAMS,
        'edit'
      )
    }

    if (replaceAll !== undefined && typeof replaceAll !== 'boolean') {
      throw new ToolError(
        'Invalid replaceAll: must be boolean',
        ToolErrorType.INVALID_PARAMS,
        'edit'
      )
    }

    return {
      filePath: filePath.trim(),
      oldString,
      newString,
      replaceAll: replaceAll === true,
    }
  },

  async execute(params: EditParams, ctx: ToolContext): Promise<ToolResult> {
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
      const oldContent = await fs.readFile(filePath)

      // Sanitize newString (strip markdown fences, normalize line endings, etc.)
      const sanitizedNewString = sanitizeContent(params.newString)

      // Handle empty oldString (create/append)
      if (params.oldString === '') {
        const newContent = sanitizedNewString
        await fs.writeFile(filePath, newContent)

        return {
          success: true,
          output: `File created/replaced successfully: ${filePath}`,
          metadata: {
            filePath,
            mode: 'create',
            bytesWritten: new TextEncoder().encode(newContent).length,
          },
          locations: [{ path: filePath, type: 'write' }],
        }
      }

      // Perform replacement
      let newContent: string
      try {
        newContent = replace(oldContent, params.oldString, sanitizedNewString, params.replaceAll)
      } catch (err) {
        const message = err instanceof Error ? err.message : String(err)
        return {
          success: false,
          output: `Edit failed: ${message}`,
          error: ToolErrorType.INVALID_PARAMS,
          metadata: {
            filePath,
            oldStringPreview: params.oldString.slice(0, 100),
          },
        }
      }

      // Write updated content
      await fs.writeFile(filePath, newContent)

      // Generate diff for output
      const diff = generateDiff(filePath, oldContent, newContent)

      // Calculate stats
      const oldLines = oldContent.split('\n').length
      const newLines = newContent.split('\n').length
      const linesDelta = newLines - oldLines

      let output = 'Edit applied successfully.'
      if (linesDelta > 0) {
        output += ` (+${linesDelta} lines)`
      } else if (linesDelta < 0) {
        output += ` (${linesDelta} lines)`
      }

      output += `\n\n<diff>\n${diff}\n</diff>`

      // Stream metadata if available
      if (ctx.metadata) {
        ctx.metadata({
          title: `Edited ${filePath.split('/').pop()}`,
          metadata: {
            diff,
            additions: newLines > oldLines ? newLines - oldLines : 0,
            deletions: oldLines > newLines ? oldLines - newLines : 0,
          },
        })
      }

      return {
        success: true,
        output,
        metadata: {
          filePath,
          mode: params.replaceAll ? 'replaceAll' : 'replace',
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
}
