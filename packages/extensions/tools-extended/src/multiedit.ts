/**
 * multiedit tool — apply multiple edits to a single file atomically.
 */

import { getPlatform } from '@ava/core-v2/platform'
import { defineTool, resolvePathSafe } from '@ava/core-v2/tools'
import * as z from 'zod'

export const multieditTool = defineTool({
  name: 'multiedit',
  description: 'Apply multiple text replacements to a single file atomically. Max 50 edits.',
  schema: z.object({
    filePath: z.string().describe('Path to the file (absolute or relative to working directory)'),
    edits: z
      .array(
        z.object({
          oldString: z.string().describe('Text to find'),
          newString: z.string().describe('Text to replace with'),
        })
      )
      .max(50),
  }),
  permissions: ['write'],
  locations: (input) => [{ path: input.filePath, type: 'write' }],
  async execute(input, ctx) {
    if (ctx.signal?.aborted) return { success: false, output: '', error: 'Aborted' }

    const fs = getPlatform().fs
    const filePath = await resolvePathSafe(input.filePath, ctx.workingDirectory)
    let content: string
    try {
      content = await fs.readFile(filePath)
    } catch {
      return { success: false, output: '', error: `File not found: ${filePath}` }
    }

    let modified = content
    for (let i = 0; i < input.edits.length; i++) {
      const edit = input.edits[i]!
      if (!modified.includes(edit.oldString)) {
        return { success: false, output: '', error: `Edit ${i + 1}: oldString not found in file` }
      }
      modified = modified.replace(edit.oldString, edit.newString)
    }

    await fs.writeFile(filePath, modified)
    return {
      success: true,
      output: `Applied ${input.edits.length} edit(s) to ${filePath}`,
    }
  },
})
