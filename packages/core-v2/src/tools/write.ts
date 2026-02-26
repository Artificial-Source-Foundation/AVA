/**
 * write_file tool — create or overwrite a file.
 */

import * as nodePath from 'node:path'
import * as z from 'zod'
import { getPlatform } from '../platform.js'
import { defineTool } from './define.js'
import { ToolError, ToolErrorType } from './errors.js'
import { sanitizeContent } from './sanitize.js'
import { LIMITS, resolvePathSafe } from './utils.js'

const schema = z.object({
  path: z.string().describe('File path (absolute or relative)'),
  content: z.string().describe('File content to write'),
})

export const writeFileTool = defineTool({
  name: 'write_file',
  description: 'Create a new file or overwrite an existing file.',
  schema,
  permissions: ['write'],
  locations: (input) => [{ path: input.path, type: 'write' as const }],

  validate(params) {
    const parsed = z.parse(schema, params)
    if (Buffer.byteLength(parsed.content, 'utf8') > LIMITS.MAX_BYTES) {
      throw new ToolError(
        `Content exceeds ${LIMITS.MAX_BYTES} bytes`,
        ToolErrorType.CONTENT_TOO_LARGE,
        'write_file'
      )
    }
    return parsed
  },

  async execute(input, ctx) {
    const fs = getPlatform().fs
    const filePath = await resolvePathSafe(input.path, ctx.workingDirectory)

    if (ctx.signal.aborted) {
      throw new ToolError('Aborted', ToolErrorType.EXECUTION_ABORTED, 'write_file')
    }

    // Check for directory conflict
    let fileExisted = false
    try {
      const stat = await fs.stat(filePath)
      if (stat.isDirectory) {
        throw new ToolError(
          `Path is a directory: ${filePath}`,
          ToolErrorType.PATH_IS_DIRECTORY,
          'write_file'
        )
      }
      fileExisted = true
    } catch (err) {
      if (err instanceof ToolError) throw err
      // File doesn't exist — OK
    }

    // Ensure parent directory exists
    const parentDir = nodePath.dirname(filePath)
    try {
      await fs.mkdir(parentDir)
    } catch {
      // May already exist
    }

    if (ctx.signal.aborted) {
      throw new ToolError('Aborted', ToolErrorType.EXECUTION_ABORTED, 'write_file')
    }

    const sanitized = sanitizeContent(input.content)
    await fs.writeFile(filePath, sanitized)

    const lines = sanitized.split('\n').length
    const bytes = Buffer.byteLength(sanitized, 'utf8')
    const action = fileExisted ? 'Updated' : 'Created'

    return {
      success: true,
      output: `${action} file: ${filePath}\n${lines} lines, ${bytes} bytes`,
      metadata: { filePath, lines, bytes, overwritten: fileExisted },
      locations: [{ path: filePath, type: 'write' }],
    }
  },
})
