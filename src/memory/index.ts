/**
 * Delta9 Memory System
 *
 * Persistent memory blocks for cross-session learning.
 * Inspired by Letta's memory blocks and agent-memory plugin.
 *
 * Memory Scopes:
 * - Global: ~/.config/delta9/memory/*.md
 * - Project: .delta9/memory/*.md
 *
 * Block Format (Markdown with YAML frontmatter):
 * ---
 * label: block-name
 * description: What this block stores
 * limit: 5000
 * readOnly: false
 * ---
 * <content>
 */

import {
  existsSync,
  mkdirSync,
  readFileSync,
  readdirSync,
  unlinkSync,
  writeFileSync,
} from 'node:fs'
import { homedir } from 'node:os'
import { join } from 'node:path'

// =============================================================================
// Types
// =============================================================================

export interface MemoryBlockMeta {
  /** Unique identifier for the block */
  label: string
  /** Description of what this block stores */
  description: string
  /** Maximum characters allowed */
  limit: number
  /** Prevent modifications */
  readOnly: boolean
  /** Block scope */
  scope: 'global' | 'project'
}

export interface MemoryBlock extends MemoryBlockMeta {
  /** Block content */
  content: string
  /** File path */
  path: string
  /** Current content size */
  size: number
}

export interface MemoryConfig {
  /** Global memory directory */
  globalDir: string
  /** Project memory directory */
  projectDir: string
}

// =============================================================================
// Constants
// =============================================================================

const DEFAULT_LIMIT = 5000
const GLOBAL_MEMORY_DIR = join(homedir(), '.config', 'delta9', 'memory')

// =============================================================================
// Paths
// =============================================================================

/**
 * Get memory configuration for a project
 */
export function getMemoryConfig(cwd: string): MemoryConfig {
  return {
    globalDir: GLOBAL_MEMORY_DIR,
    projectDir: join(cwd, '.delta9', 'memory'),
  }
}

/**
 * Ensure memory directories exist
 */
export function ensureMemoryDirs(cwd: string): void {
  const config = getMemoryConfig(cwd)

  if (!existsSync(config.globalDir)) {
    mkdirSync(config.globalDir, { recursive: true })
  }

  if (!existsSync(config.projectDir)) {
    mkdirSync(config.projectDir, { recursive: true })
  }
}

// =============================================================================
// Frontmatter Parsing
// =============================================================================

/**
 * Parse YAML frontmatter from markdown content
 */
function parseFrontmatter(content: string): { meta: Record<string, unknown>; body: string } {
  const frontmatterRegex = /^---\n([\s\S]*?)\n---\n?([\s\S]*)$/
  const match = content.match(frontmatterRegex)

  if (!match) {
    return { meta: {}, body: content }
  }

  const yamlContent = match[1]
  const body = match[2]

  // Simple YAML parsing (key: value)
  const meta: Record<string, unknown> = {}
  const lines = yamlContent.split('\n')

  for (const line of lines) {
    const colonIndex = line.indexOf(':')
    if (colonIndex > 0) {
      const key = line.substring(0, colonIndex).trim()
      let value: unknown = line.substring(colonIndex + 1).trim()

      // Parse booleans and numbers
      if (value === 'true') value = true
      else if (value === 'false') value = false
      else if (/^\d+$/.test(value as string)) value = parseInt(value as string, 10)

      meta[key] = value
    }
  }

  return { meta, body }
}

/**
 * Serialize frontmatter to string
 */
function serializeFrontmatter(meta: MemoryBlockMeta, content: string): string {
  const lines = [
    '---',
    `label: ${meta.label}`,
    `description: ${meta.description}`,
    `limit: ${meta.limit}`,
    `readOnly: ${meta.readOnly}`,
    '---',
    '',
    content,
  ]

  return lines.join('\n')
}

// =============================================================================
// Block Operations
// =============================================================================

/**
 * Read a memory block from file
 */
function readBlock(filePath: string, scope: 'global' | 'project'): MemoryBlock | null {
  if (!existsSync(filePath)) {
    return null
  }

  try {
    const rawContent = readFileSync(filePath, 'utf-8')
    const { meta, body } = parseFrontmatter(rawContent)

    const label =
      (meta.label as string) || filePath.split('/').pop()?.replace('.md', '') || 'unknown'
    const content = body.trim()

    return {
      label,
      description: (meta.description as string) || 'Memory block',
      limit: (meta.limit as number) || DEFAULT_LIMIT,
      readOnly: (meta.readOnly as boolean) || false,
      scope,
      content,
      path: filePath,
      size: content.length,
    }
  } catch (error) {
    console.error(`Failed to read memory block ${filePath}:`, error)
    return null
  }
}

/**
 * Write a memory block to file
 */
function writeBlock(block: MemoryBlock): boolean {
  if (block.readOnly) {
    return false
  }

  // Enforce limit
  const content =
    block.content.length > block.limit ? block.content.substring(0, block.limit) : block.content

  try {
    const serialized = serializeFrontmatter(
      {
        label: block.label,
        description: block.description,
        limit: block.limit,
        readOnly: block.readOnly,
        scope: block.scope,
      },
      content
    )

    writeFileSync(block.path, serialized, 'utf-8')
    return true
  } catch (error) {
    console.error(`Failed to write memory block ${block.path}:`, error)
    return false
  }
}

/**
 * List all memory blocks for a scope
 */
function listBlocksInDir(dir: string, scope: 'global' | 'project'): MemoryBlock[] {
  if (!existsSync(dir)) {
    return []
  }

  const blocks: MemoryBlock[] = []
  const files = readdirSync(dir).filter((f) => f.endsWith('.md'))

  for (const file of files) {
    const block = readBlock(join(dir, file), scope)
    if (block) {
      blocks.push(block)
    }
  }

  return blocks
}

// =============================================================================
// Memory Manager
// =============================================================================

export class MemoryManager {
  private cwd: string
  private config: MemoryConfig

  constructor(cwd: string) {
    this.cwd = cwd
    this.config = getMemoryConfig(cwd)
    ensureMemoryDirs(cwd)
  }

  /**
   * List all available memory blocks
   */
  list(): MemoryBlock[] {
    const globalBlocks = listBlocksInDir(this.config.globalDir, 'global')
    const projectBlocks = listBlocksInDir(this.config.projectDir, 'project')
    return [...globalBlocks, ...projectBlocks]
  }

  /**
   * Get a specific memory block by label
   */
  get(label: string): MemoryBlock | null {
    // Check project first (higher priority)
    const projectPath = join(this.config.projectDir, `${label}.md`)
    const projectBlock = readBlock(projectPath, 'project')
    if (projectBlock) return projectBlock

    // Check global
    const globalPath = join(this.config.globalDir, `${label}.md`)
    return readBlock(globalPath, 'global')
  }

  /**
   * Set (create or update) a memory block
   */
  set(
    label: string,
    content: string,
    options: {
      scope?: 'global' | 'project'
      description?: string
      limit?: number
      readOnly?: boolean
    } = {}
  ): boolean {
    const scope = options.scope || 'project'
    const dir = scope === 'global' ? this.config.globalDir : this.config.projectDir
    const path = join(dir, `${label}.md`)

    // Check if block exists
    const existing = readBlock(path, scope)

    if (existing?.readOnly) {
      return false
    }

    const block: MemoryBlock = {
      label,
      description: options.description || existing?.description || 'Memory block',
      limit: options.limit || existing?.limit || DEFAULT_LIMIT,
      readOnly: options.readOnly ?? existing?.readOnly ?? false,
      scope,
      content,
      path,
      size: content.length,
    }

    return writeBlock(block)
  }

  /**
   * Replace a substring within a memory block
   */
  replace(label: string, oldText: string, newText: string): boolean {
    const block = this.get(label)

    if (!block || block.readOnly) {
      return false
    }

    const newContent = block.content.replace(oldText, newText)
    return this.set(label, newContent, { scope: block.scope })
  }

  /**
   * Append content to a memory block
   */
  append(label: string, content: string, separator: string = '\n\n'): boolean {
    const block = this.get(label)

    if (block?.readOnly) {
      return false
    }

    const existingContent = block?.content || ''
    const newContent = existingContent ? existingContent + separator + content : content

    return this.set(label, newContent, { scope: block?.scope || 'project' })
  }

  /**
   * Delete a memory block
   */
  delete(label: string, scope?: 'global' | 'project'): boolean {
    const block = this.get(label)

    if (!block || block.readOnly) {
      return false
    }

    // If scope specified, only delete from that scope
    if (scope && block.scope !== scope) {
      return false
    }

    try {
      unlinkSync(block.path)
      return true
    } catch {
      return false
    }
  }

  /**
   * Get summary of all blocks
   */
  getSummary(): {
    total: number
    global: number
    project: number
    totalSize: number
  } {
    const blocks = this.list()

    return {
      total: blocks.length,
      global: blocks.filter((b) => b.scope === 'global').length,
      project: blocks.filter((b) => b.scope === 'project').length,
      totalSize: blocks.reduce((sum, b) => sum + b.size, 0),
    }
  }
}

// =============================================================================
// Default Blocks
// =============================================================================

/**
 * Default memory blocks to seed on first use
 */
export const DEFAULT_BLOCKS: Array<{
  label: string
  description: string
  scope: 'global' | 'project'
  content: string
}> = [
  {
    label: 'patterns',
    description: 'Successful patterns and approaches learned during missions',
    scope: 'project',
    content: `# Patterns

Successful patterns and approaches for this project.

## Code Patterns
(Add patterns that work well in this codebase)

## Testing Patterns
(Add testing approaches that work)

## Debugging Patterns
(Add debugging techniques that helped)
`,
  },
  {
    label: 'failures',
    description: 'Things that failed and why - avoid repeating mistakes',
    scope: 'project',
    content: `# Failures

Track failures to avoid repeating them.

## Failed Approaches
(Document approaches that didn't work and why)

## Common Pitfalls
(Note common issues to watch for)
`,
  },
  {
    label: 'context',
    description: 'Important project context and decisions',
    scope: 'project',
    content: `# Project Context

Important context for understanding this project.

## Architecture Decisions
(Key architectural choices and reasoning)

## Constraints
(Technical or business constraints to respect)

## Dependencies
(Key dependencies and their purposes)
`,
  },
]

/**
 * Seed default blocks if they don't exist
 */
export function seedDefaultBlocks(cwd: string): void {
  const manager = new MemoryManager(cwd)

  for (const block of DEFAULT_BLOCKS) {
    if (!manager.get(block.label)) {
      manager.set(block.label, block.content, {
        scope: block.scope,
        description: block.description,
      })
    }
  }
}

// =============================================================================
// Singleton
// =============================================================================

let memoryManagerInstance: MemoryManager | null = null

/**
 * Get or create memory manager instance
 */
export function getMemoryManager(cwd: string): MemoryManager {
  if (!memoryManagerInstance || memoryManagerInstance['cwd'] !== cwd) {
    memoryManagerInstance = new MemoryManager(cwd)
  }
  return memoryManagerInstance
}

/**
 * Clear memory manager instance (for testing)
 */
export function clearMemoryManager(): void {
  memoryManagerInstance = null
}
