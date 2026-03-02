/**
 * Instructions extension types.
 */

export interface InstructionFile {
  path: string
  content: string
  scope: 'project' | 'directory' | 'user' | 'remote'
  priority: number
}

export interface InstructionConfig {
  fileNames: string[]
  maxDepth: number
  maxSize: number
  urls?: string[]
}

export const DEFAULT_INSTRUCTION_CONFIG: InstructionConfig = {
  fileNames: ['AGENTS.md', 'CLAUDE.md', '.ava-instructions', '.ava-instructions.md'],
  maxDepth: 5,
  maxSize: 10_000,
}
