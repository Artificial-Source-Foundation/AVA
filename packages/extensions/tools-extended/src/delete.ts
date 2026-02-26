/**
 * delete_file tool — removes a single file.
 */

import { getPlatform } from '@ava/core-v2/platform'
import { defineTool } from '@ava/core-v2/tools'
import * as z from 'zod'

export const deleteFileTool = defineTool({
  name: 'delete_file',
  description: 'Delete a single file. Does not delete directories.',
  schema: z.object({
    path: z.string().describe('Absolute path to the file to delete'),
  }),
  permissions: ['delete'],
  locations: (input) => [{ path: input.path, type: 'write' }],
  async execute(input, ctx) {
    if (ctx.signal?.aborted) {
      return { success: false, output: '', error: 'Aborted' }
    }

    const fs = getPlatform().fs
    const path = input.path

    try {
      const stat = await fs.stat(path)
      if (stat.isDirectory) {
        return {
          success: false,
          output: '',
          error: `${path} is a directory. Only files can be deleted.`,
        }
      }
    } catch {
      return { success: false, output: '', error: `File not found: ${path}` }
    }

    await fs.remove(path)
    return { success: true, output: `Deleted ${path}` }
  },
})
