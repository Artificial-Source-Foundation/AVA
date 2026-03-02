/**
 * create_file tool — creates a new file (fails if it exists).
 */

import { getPlatform } from '@ava/core-v2/platform'
import { defineTool, resolvePathSafe } from '@ava/core-v2/tools'
import * as z from 'zod'

export const createFileTool = defineTool({
  name: 'create_file',
  description: 'Create a new file with the given content. Fails if the file already exists.',
  schema: z.object({
    path: z.string().describe('Path for the new file (absolute or relative to working directory)'),
    content: z.string().describe('File content'),
  }),
  permissions: ['write'],
  locations: (input) => [{ path: input.path, type: 'write' }],
  async execute(input, ctx) {
    if (ctx.signal?.aborted) {
      return { success: false, output: '', error: 'Aborted' }
    }

    const fs = getPlatform().fs
    const filePath = await resolvePathSafe(input.path, ctx.workingDirectory)

    // Check if file already exists
    const exists = await fs.exists(filePath)
    if (exists) {
      return {
        success: false,
        output: '',
        error: `File already exists: ${filePath}. Use write_file to overwrite.`,
      }
    }

    // Ensure parent directory
    const parentDir = filePath.substring(0, filePath.lastIndexOf('/'))
    if (parentDir) {
      try {
        await fs.mkdir(parentDir)
      } catch {
        // Directory might already exist
      }
    }

    await fs.writeFile(filePath, input.content)
    return { success: true, output: `Created ${filePath} (${input.content.length} chars)` }
  },
})
