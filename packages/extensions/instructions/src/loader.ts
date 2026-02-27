/**
 * Instruction file loader — discovers and merges instruction files.
 */

import type { IFileSystem } from '@ava/core-v2/platform'
import type { InstructionConfig, InstructionFile } from './types.js'
import { DEFAULT_INSTRUCTION_CONFIG } from './types.js'

/**
 * Load instruction files from the working directory upward.
 * Files closer to the working directory have higher priority.
 */
export async function loadInstructions(
  cwd: string,
  fs: IFileSystem,
  config: InstructionConfig = DEFAULT_INSTRUCTION_CONFIG
): Promise<InstructionFile[]> {
  const results: InstructionFile[] = []
  let currentDir = cwd
  let depth = 0

  while (depth < config.maxDepth) {
    for (const fileName of config.fileNames) {
      const filePath = `${currentDir}/${fileName}`
      try {
        const content = await fs.readFile(filePath)
        if (content.length <= config.maxSize) {
          results.push({
            path: filePath,
            content,
            scope: depth === 0 ? 'project' : 'directory',
            priority: config.maxDepth - depth, // closer = higher priority
          })
        }
      } catch {
        // File doesn't exist — skip
      }
    }

    // Move up one directory
    const parentDir = currentDir.replace(/\/[^/]+$/, '')
    if (parentDir === currentDir || parentDir === '') break
    currentDir = parentDir
    depth++
  }

  // Sort by priority descending (highest first)
  results.sort((a, b) => b.priority - a.priority)
  return results
}

/**
 * Merge instruction files into a single string, separated by headers.
 */
export function mergeInstructions(files: InstructionFile[]): string {
  if (files.length === 0) return ''
  return files.map((f) => `# Instructions from ${f.path}\n\n${f.content}`).join('\n\n---\n\n')
}
