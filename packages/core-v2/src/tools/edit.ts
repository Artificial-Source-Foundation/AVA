/**
 * edit tool — fuzzy text replacement with 5 strategies.
 */

import * as z from 'zod'
import { getPlatform } from '../platform.js'
import { defineTool } from './define.js'
import { replace } from './edit-replacers.js'
import { ToolError, ToolErrorType } from './errors.js'
import { sanitizeContent } from './sanitize.js'
import { isFeatureEnabled, resolvePath } from './utils.js'

const schema = z.object({
  filePath: z.string().describe('Absolute path to the file to edit'),
  oldString: z.string().describe('Text to find and replace'),
  newString: z.string().describe('Replacement text'),
  replaceAll: z.boolean().optional().describe('Replace all occurrences (default: false)'),
})

export const editTool = defineTool({
  name: 'edit',
  description: 'Find and replace text in a file. Uses fuzzy matching strategies.',
  schema,
  permissions: ['write'],
  locations: (input) => [{ path: input.filePath, type: 'write' as const }],

  validate(params) {
    const parsed = z.parse(schema, params)
    if (parsed.oldString === parsed.newString) {
      throw new ToolError(
        'oldString and newString are identical — no change needed',
        ToolErrorType.INVALID_PARAMS,
        'edit'
      )
    }
    return parsed
  },

  async execute(input, ctx) {
    const platform = getPlatform()
    const fs = platform.fs
    const filePath = resolvePath(input.filePath, ctx.workingDirectory)

    if (ctx.signal.aborted) {
      throw new ToolError('Aborted', ToolErrorType.EXECUTION_ABORTED, 'edit')
    }

    // Special case: empty oldString = full file replace or create
    if (input.oldString === '') {
      const sanitized = sanitizeContent(input.newString)
      await fs.writeFile(filePath, sanitized)
      const lines = sanitized.split('\n').length
      return {
        success: true,
        output: `File written: ${filePath} (${lines} lines)`,
        metadata: { filePath, mode: 'create', newLines: lines },
        locations: [{ path: filePath, type: 'write' }],
      }
    }

    // Read existing file
    let content: string
    try {
      const stat = await fs.stat(filePath)
      if (stat.isDirectory) {
        throw new ToolError(
          `Path is a directory: ${filePath}`,
          ToolErrorType.PATH_IS_DIRECTORY,
          'edit'
        )
      }
      if (stat.size > 5 * 1024 * 1024) {
        throw new ToolError(
          `File too large (>5MB): ${filePath}`,
          ToolErrorType.CONTENT_TOO_LARGE,
          'edit'
        )
      }
      content = await fs.readFile(filePath)
    } catch (err) {
      if (err instanceof ToolError) throw err
      throw new ToolError(`File not found: ${filePath}`, ToolErrorType.FILE_NOT_FOUND, 'edit')
    }

    // Perform replacement
    const sanitizedNew = sanitizeContent(input.newString, { ensureTrailingNewline: false })
    const replaceAll = input.replaceAll ?? false

    let newContent = ''
    let engine: 'rust' | 'typescript' = 'typescript'
    if (platform.compute && isFeatureEnabled('AVA_RUST_FUZZY_EDIT', true)) {
      try {
        const nativeResult = await platform.compute.fuzzyReplace({
          content,
          oldString: input.oldString,
          newString: sanitizedNew,
          replaceAll,
        })
        newContent = nativeResult.content
        engine = 'rust'
      } catch {
        newContent = replace(content, input.oldString, sanitizedNew, replaceAll)
      }
    } else {
      newContent = replace(content, input.oldString, sanitizedNew, replaceAll)
    }

    if (ctx.signal.aborted) {
      throw new ToolError('Aborted', ToolErrorType.EXECUTION_ABORTED, 'edit')
    }

    await fs.writeFile(filePath, newContent)

    const oldLines = content.split('\n').length
    const newLines = newContent.split('\n').length
    const delta = newLines - oldLines
    const deltaStr = delta >= 0 ? `+${delta}` : `${delta}`

    return {
      success: true,
      output: `Edit applied successfully. (${deltaStr} lines)\nFile: ${filePath}`,
      metadata: {
        filePath,
        mode: replaceAll ? 'replaceAll' : 'replace',
        oldLines,
        newLines,
        linesDelta: delta,
        engine,
      },
      locations: [{ path: filePath, type: 'write' }],
    }
  },
})
