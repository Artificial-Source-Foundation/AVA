/**
 * Example: Word Count Tool Extension
 *
 * Demonstrates how to register a custom tool via the Extension API.
 * This tool counts words, lines, and characters in a file.
 */

import type { Disposable, ExtensionAPI } from '@ava/core-v2/extensions'
import { getPlatform } from '@ava/core-v2/platform'
import { defineTool } from '@ava/core-v2/tools'
import * as z from 'zod'

const wordCountTool = defineTool({
  name: 'word_count',
  description: 'Count words, lines, and characters in a file.',
  schema: z.object({
    path: z.string().describe('Path to the file to count'),
  }),

  async execute(input, _ctx) {
    const content = await getPlatform().fs.readFile(input.path)

    const lines = content.split('\n').length
    const words = content.split(/\s+/).filter(Boolean).length
    const chars = content.length

    return {
      success: true,
      output: `File: ${input.path}\nLines: ${lines}\nWords: ${words}\nCharacters: ${chars}`,
    }
  },
})

export function activate(api: ExtensionAPI): Disposable {
  const disposable = api.registerTool(wordCountTool)
  api.log.info('Word count tool registered')
  return disposable
}
