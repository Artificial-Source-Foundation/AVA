/**
 * create_file tool — creates a new file (fails if it exists).
 */

import { getPlatform } from '@ava/core-v2/platform'
import { defineTool } from '@ava/core-v2/tools'
import * as z from 'zod'

export const createFileTool = defineTool({
  name: 'create_file',
  description: 'Create a new file with the given content. Fails if the file already exists.',
  schema: z.object({
    path: z.string().describe('Absolute path for the new file'),
    content: z.string().describe('File content'),
  }),
  permissions: ['write'],
  locations: (input) => [{ path: input.path, type: 'write' }],
  async execute(input, ctx) {
    if (ctx.signal?.aborted) {
      return { success: false, output: '', error: 'Aborted' }
    }

    const fs = getPlatform().fs
    const path = input.path

    // Check if file already exists
    const exists = await fs.exists(path)
    if (exists) {
      return {
        success: false,
        output: '',
        error: `File already exists: ${path}. Use write_file to overwrite.`,
      }
    }

    // Ensure parent directory
    const parentDir = path.substring(0, path.lastIndexOf('/'))
    if (parentDir) {
      try {
        await fs.mkdir(parentDir)
      } catch {
        // Directory might already exist
      }
    }

    await fs.writeFile(path, input.content)
    return { success: true, output: `Created ${path} (${input.content.length} chars)` }
  },
})
