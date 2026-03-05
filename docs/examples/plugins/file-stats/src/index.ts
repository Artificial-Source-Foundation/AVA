/**
 * File Stats Plugin
 *
 * Demonstrates: registerTool(), platform.fs access
 * Returns line count, byte size, and detected language for a file.
 */

import type { Disposable, ExtensionAPI } from '@ava/core-v2/extensions'
import { defineTool } from '@ava/core-v2/tools'
import * as z from 'zod'

const EXTENSIONS: Record<string, string> = {
  '.ts': 'TypeScript',
  '.tsx': 'TypeScript (JSX)',
  '.js': 'JavaScript',
  '.jsx': 'JavaScript (JSX)',
  '.py': 'Python',
  '.rs': 'Rust',
  '.go': 'Go',
  '.java': 'Java',
  '.css': 'CSS',
  '.html': 'HTML',
  '.json': 'JSON',
  '.md': 'Markdown',
}

function detectLanguage(filePath: string): string {
  const ext = filePath.slice(filePath.lastIndexOf('.'))
  return EXTENSIONS[ext] ?? 'Unknown'
}

export const fileStatsTool = defineTool({
  name: 'file_stats',
  description: 'Get line count, byte size, and language for a file.',
  schema: z.object({
    path: z.string().describe('Absolute path to the file'),
  }),

  async execute(input) {
    // Use the tool context approach — read via the globally registered platform
    const { getPlatform } = await import('@ava/core-v2/platform')
    const content = await getPlatform().fs.readFile(input.path)

    const lines = content.split('\n').length
    const bytes = new TextEncoder().encode(content).byteLength
    const language = detectLanguage(input.path)

    return {
      success: true,
      output: `File: ${input.path}\nLines: ${lines}\nSize: ${bytes} bytes\nLanguage: ${language}`,
      metadata: { lines, bytes, language, path: input.path },
    }
  },
})

export function activate(api: ExtensionAPI): Disposable {
  const disposable = api.registerTool(fileStatsTool)
  api.log.info('File stats tool registered')
  return disposable
}
