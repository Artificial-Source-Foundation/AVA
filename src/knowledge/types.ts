/**
 * Delta9 Knowledge System Types
 *
 * Letta-style memory blocks for persistent learning.
 * Pattern: agent-memory plugin
 */

import { z } from 'zod'

// =============================================================================
// Knowledge Block Types
// =============================================================================

/**
 * Knowledge block scope
 * - project: Stored in .delta9/knowledge/ (project-specific)
 * - global: Stored in ~/.delta9/knowledge/ (cross-project)
 */
export type KnowledgeScope = 'project' | 'global'

/**
 * Knowledge block frontmatter schema
 */
export const knowledgeFrontmatterSchema = z.object({
  label: z.string().min(1).optional(),
  description: z.string().optional(),
  limit: z.number().int().positive().optional(),
  read_only: z.boolean().optional(),
  category: z.enum(['patterns', 'conventions', 'gotchas', 'decisions', 'custom']).optional(),
  updated_at: z.string().optional(),
})

export type KnowledgeFrontmatter = z.infer<typeof knowledgeFrontmatterSchema>

/**
 * Knowledge block with full metadata
 */
export interface KnowledgeBlock {
  /** Block scope (project or global) */
  scope: KnowledgeScope
  /** Unique label for the block */
  label: string
  /** Description of what this block contains */
  description: string
  /** Maximum character limit */
  limit: number
  /** Whether the block is read-only */
  readOnly: boolean
  /** Category for organization */
  category: 'patterns' | 'conventions' | 'gotchas' | 'decisions' | 'custom'
  /** Block content (markdown) */
  value: string
  /** Full file path */
  filePath: string
  /** Last modification time */
  lastModified: Date
  /** Current character count */
  charCount: number
}

/**
 * Knowledge store interface
 */
export interface KnowledgeStore {
  /** Ensure seed blocks exist */
  ensureSeed(): Promise<void>
  /** List all blocks */
  listBlocks(scope: KnowledgeScope | 'all'): Promise<KnowledgeBlock[]>
  /** Get a specific block */
  getBlock(scope: KnowledgeScope, label: string): Promise<KnowledgeBlock>
  /** Set a block (full overwrite) */
  setBlock(
    scope: KnowledgeScope,
    label: string,
    value: string,
    opts?: {
      description?: string
      limit?: number
      category?: KnowledgeBlock['category']
    }
  ): Promise<void>
  /** Append to a block */
  appendBlock(
    scope: KnowledgeScope,
    label: string,
    content: string,
    opts?: { separator?: string }
  ): Promise<void>
  /** Replace text in a block */
  replaceInBlock(
    scope: KnowledgeScope,
    label: string,
    oldText: string,
    newText: string
  ): Promise<void>
  /** Delete a block */
  deleteBlock(scope: KnowledgeScope, label: string): Promise<void>
}

// =============================================================================
// Seed Block Definitions
// =============================================================================

export const SEED_BLOCKS: Array<{
  scope: KnowledgeScope
  label: string
  description: string
  category: KnowledgeBlock['category']
}> = [
  {
    scope: 'project',
    label: 'patterns',
    description: 'Learned code patterns, import conventions, architectural decisions',
    category: 'patterns',
  },
  {
    scope: 'project',
    label: 'conventions',
    description: 'Project conventions: naming, structure, style preferences',
    category: 'conventions',
  },
  {
    scope: 'project',
    label: 'gotchas',
    description: 'Known issues, pitfalls, and things to avoid',
    category: 'gotchas',
  },
  {
    scope: 'project',
    label: 'decisions',
    description: 'Important decisions and their rationale',
    category: 'decisions',
  },
]

// =============================================================================
// Constants
// =============================================================================

export const DEFAULT_LIMIT = 10000
export const MAX_LIMIT = 50000
export const LABEL_REGEX = /^[a-z0-9][a-z0-9-_]{1,60}$/i
