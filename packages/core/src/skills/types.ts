/**
 * Skill System Types
 * Type definitions for reusable knowledge modules
 */

import { z } from 'zod'

// ============================================================================
// Schemas
// ============================================================================

/**
 * Skill YAML frontmatter schema
 */
export const SkillFrontmatterSchema = z.object({
  name: z.string().describe('Unique skill identifier'),
  description: z.string().optional().describe('Short description of the skill'),
  globs: z.array(z.string()).optional().describe('File patterns that auto-activate this skill'),
  version: z.string().optional().describe('Skill version'),
  author: z.string().optional().describe('Skill author'),
  tags: z.array(z.string()).optional().describe('Tags for categorization'),
})

export type SkillFrontmatter = z.infer<typeof SkillFrontmatterSchema>

// ============================================================================
// Types
// ============================================================================

/**
 * A loaded skill with parsed content
 */
export interface Skill {
  /** Unique skill identifier */
  name: string
  /** Short description of the skill */
  description?: string
  /** File patterns that auto-activate this skill */
  globs?: string[]
  /** The skill's markdown content (without frontmatter) */
  content: string
  /** Path to the skill file */
  path: string
  /** Skill version if specified */
  version?: string
  /** Skill author if specified */
  author?: string
  /** Tags for categorization */
  tags?: string[]
}

/**
 * Skill discovery locations
 */
export interface SkillDiscoveryConfig {
  /** Project-local skill directory */
  projectDir?: string
  /** User-global skill directory */
  userDir?: string
  /** Additional custom directories */
  customDirs?: string[]
}

/**
 * Result of skill discovery
 */
export interface SkillDiscoveryResult {
  /** All discovered skills */
  skills: Skill[]
  /** Paths that were searched */
  searchedPaths: string[]
  /** Errors encountered during discovery */
  errors: Array<{ path: string; error: string }>
}
