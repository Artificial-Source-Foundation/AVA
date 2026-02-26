/**
 * multiedit tool — apply multiple edits to a single file atomically.
 */

import { getPlatform } from '@ava/core-v2/platform'
import { defineTool } from '@ava/core-v2/tools'
import * as z from 'zod'

export const multieditTool = defineTool({
  name: 'multiedit',
  description: 'Apply multiple text replacements to a single file atomically. Max 50 edits.',
  schema: z.object({
    filePath: z.string().describe('Absolute path to the file'),
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
    let content: string
    try {
      content = await fs.readFile(input.filePath)
    } catch {
      return { success: false, output: '', error: `File not found: ${input.filePath}` }
    }

    let modified = content
    for (let i = 0; i < input.edits.length; i++) {
      const edit = input.edits[i]!
      if (!modified.includes(edit.oldString)) {
        return { success: false, output: '', error: `Edit ${i + 1}: oldString not found in file` }
      }
      modified = modified.replace(edit.oldString, edit.newString)
    }

    await fs.writeFile(input.filePath, modified)
    return {
      success: true,
      output: `Applied ${input.edits.length} edit(s) to ${input.filePath}`,
    }
  },
})
