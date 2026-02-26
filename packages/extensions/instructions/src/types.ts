/**
 * Instructions extension types.
 */

export interface InstructionFile {
  path: string
  content: string
  scope: 'project' | 'directory' | 'user'
  priority: number
}

export interface InstructionConfig {
  fileNames: string[]
  maxDepth: number
  maxSize: number
}

export const DEFAULT_INSTRUCTION_CONFIG: InstructionConfig = {
  fileNames: ['.ava-instructions', '.ava-instructions.md', 'CLAUDE.md'],
  maxDepth: 5,
  maxSize: 10_000,
}
