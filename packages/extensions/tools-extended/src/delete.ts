/**
 * delete_file tool — removes a single file.
 */

import { getPlatform } from '@ava/core-v2/platform'
import { defineTool, resolvePathSafe } from '@ava/core-v2/tools'
import * as z from 'zod'

export const deleteFileTool = defineTool({
  name: 'delete_file',
  description: 'Delete a single file. Does not delete directories.',
  schema: z.object({
    path: z
      .string()
      .describe('Path to the file to delete (absolute or relative to working directory)'),
  }),
  permissions: ['delete'],
  locations: (input) => [{ path: input.path, type: 'write' }],
  async execute(input, ctx) {
    if (ctx.signal?.aborted) {
      return { success: false, output: '', error: 'Aborted' }
    }

    const fs = getPlatform().fs
    const filePath = await resolvePathSafe(input.path, ctx.workingDirectory)

    try {
      const stat = await fs.stat(filePath)
      if (stat.isDirectory) {
        return {
          success: false,
          output: '',
          error: `${filePath} is a directory. Only files can be deleted.`,
        }
      }
    } catch {
      return { success: false, output: '', error: `File not found: ${filePath}` }
    }

    await fs.remove(filePath)
    return { success: true, output: `Deleted ${filePath}` }
  },
})
