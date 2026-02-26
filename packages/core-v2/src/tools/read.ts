/**
 * read_file tool — read file contents with line numbers.
 */

import * as z from 'zod'
import { getPlatform } from '../platform.js'
import { defineTool } from './define.js'
import { ToolError, ToolErrorType } from './errors.js'
import { formatLineNumber, isBinaryFile, LIMITS, resolvePathSafe, truncate } from './utils.js'

const schema = z.object({
  path: z.string().describe('File path (absolute or relative to working directory)'),
  offset: z.number().int().min(0).optional().describe('0-based line offset to start reading from'),
  limit: z.number().int().min(1).optional().describe('Maximum number of lines to read'),
})

export const readFileTool = defineTool({
  name: 'read_file',
  description: 'Read the contents of a file. Returns numbered lines.',
  schema,
  permissions: ['read'],
  locations: (input) => [{ path: input.path, type: 'read' as const }],
  async execute(input, ctx) {
    const fs = getPlatform().fs
    const filePath = await resolvePathSafe(input.path, ctx.workingDirectory)
    const offset = input.offset ?? 0
    const limit = input.limit ?? LIMITS.MAX_LINES

    // Check file exists and is not a directory
    try {
      const stat = await fs.stat(filePath)
      if (stat.isDirectory) {
        throw new ToolError(
          `Path is a directory: ${filePath}`,
          ToolErrorType.PATH_IS_DIRECTORY,
          'read_file'
        )
      }
    } catch (err) {
      if (err instanceof ToolError) throw err
      throw new ToolError(`File not found: ${filePath}`, ToolErrorType.FILE_NOT_FOUND, 'read_file')
    }

    // Binary check
    if (await isBinaryFile(filePath)) {
      throw new ToolError(`Binary file: ${filePath}`, ToolErrorType.BINARY_FILE, 'read_file')
    }

    if (ctx.signal.aborted) {
      throw new ToolError('Aborted', ToolErrorType.EXECUTION_ABORTED, 'read_file')
    }

    const content = await fs.readFile(filePath)
    const allLines = content.split('\n')
    const totalLines = allLines.length
    const lines = allLines.slice(offset, offset + limit)

    let byteCount = 0
    let bytesTruncated = false
    const formatted: string[] = []

    for (let i = 0; i < lines.length; i++) {
      const lineNum = offset + i + 1
      const line = truncate(lines[i], LIMITS.MAX_LINE_LENGTH)
      const lineBytes = Buffer.byteLength(line, 'utf8')

      if (byteCount + lineBytes > LIMITS.MAX_BYTES) {
        bytesTruncated = true
        break
      }

      byteCount += lineBytes
      formatted.push(`${formatLineNumber(lineNum, totalLines)}| ${line}`)
    }

    const linesRead = formatted.length
    const endLine = offset + linesRead
    const hasMore = endLine < totalLines

    let note: string
    if (hasMore) {
      note = `(Showing lines ${offset + 1}-${endLine} of ${totalLines}. Use offset=${endLine} to continue.)`
    } else {
      note = '(End of file)'
    }

    const output = `<file path="${filePath}">\n${formatted.join('\n')}\n</file>\n${note}`

    return {
      success: true,
      output,
      metadata: { filePath, totalLines, linesRead, offset, truncated: bytesTruncated || hasMore },
      locations: [{ path: filePath, type: 'read', lines: [offset + 1, endLine] }],
    }
  },
})
