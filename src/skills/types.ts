/**
 * Delta9 Skills System - Type Definitions
 *
 * Skills are reusable prompt templates that can be loaded from:
 * - Project: .delta9/skills/ (highest priority)
 * - User: ~/.config/delta9/skills/
 * - Global: ~/.delta9/skills/
 * - Builtin: Bundled with the plugin
 *
 * First match wins - project skills override user/global/builtin.
 */

import { z } from 'zod'

// =============================================================================
// Enums & Constants
// =============================================================================

/** Skill source label indicating where the skill was loaded from */
export type SkillLabel = 'project' | 'user' | 'global' | 'builtin'

/** Render format for skill content */
export type RenderFormat = 'xml' | 'json' | 'md'

/** Discovery path configuration */
export interface DiscoveryPath {
  /** Path pattern (may include ~) */
  path: string
  /** Label for skills found at this path */
  label: SkillLabel
  /** Maximum directory depth to search */
  maxDepth: number
}

// =============================================================================
// Skill Frontmatter Schema
// =============================================================================

/**
 * SKILL.md frontmatter schema following Anthropic Agent Skills Spec
 * @see https://github.com/anthropics/skills/blob/main/agent_skills_spec.md
 */
export const SkillFrontmatterSchema = z.object({
  /** Unique skill name (lowercase alphanumeric with hyphens) */
  name: z
    .string()
    .regex(/^[\p{Ll}\p{N}-]+$/u, { message: 'Name must be lowercase alphanumeric with hyphens' })
    .min(1, { message: 'Name cannot be empty' }),

  /** Human-readable description (min 20 chars for discoverability) */
  description: z.string().min(1, { message: 'Description cannot be empty' }),

  /** When to use this skill (for auto-activation) */
  use_when: z.string().optional(),

  /** License (e.g., MIT) */
  license: z.string().optional(),

  /** Compatibility requirements */
  compatibility: z.string().optional(),

  /** Tools this skill is allowed to use */
  'allowed-tools': z.array(z.string()).optional(),

  /** Additional metadata */
  metadata: z.record(z.string(), z.string()).optional(),

  /** MCP server configuration (skill-specific) */
  mcp: z
    .object({
      /** MCP server command */
      command: z.string(),
      /** Command arguments */
      args: z.array(z.string()).optional(),
      /** Environment variables */
      env: z.record(z.string(), z.string()).optional(),
    })
    .optional(),
})

export type SkillFrontmatter = z.infer<typeof SkillFrontmatterSchema>

// =============================================================================
// Skill Types
// =============================================================================

/** Script metadata with paths */
export interface SkillScript {
  /** Path relative to skill directory */
  relativePath: string
  /** Absolute path to script */
  absolutePath: string
}

/** Resource file metadata */
export interface SkillResource {
  /** Path relative to skill directory */
  relativePath: string
  /** Absolute path to resource */
  absolutePath: string
  /** File type (extension) */
  type: string
}

/** Complete metadata for a discovered skill */
export interface Skill {
  /** Unique skill name */
  name: string
  /** Human-readable description */
  description: string
  /** When to use this skill (auto-activation hint) */
  useWhen?: string
  /** Source label */
  label: SkillLabel
  /** Absolute path to skill directory */
  path: string
  /** Relative path from discovery root */
  relativePath: string
  /** Skill template content (body of SKILL.md) */
  template: string
  /** Optional namespace from metadata */
  namespace?: string
  /** Allowed tools (if restricted) */
  allowedTools?: string[]
  /** MCP configuration */
  mcp?: {
    command: string
    args?: string[]
    env?: Record<string, string>
  }
  /** Executable scripts in skill directory */
  scripts: SkillScript[]
  /** Resource files (references, templates, etc.) */
  resources: SkillResource[]
}

/** Summary for skill listing */
export interface SkillSummary {
  name: string
  description: string
  label: SkillLabel
  useWhen?: string
}

// =============================================================================
// Discovery Types
// =============================================================================

/** Result from file discovery */
export interface FileDiscoveryResult {
  /** Absolute path to discovered file */
  filePath: string
  /** Relative path from search root */
  relativePath: string
  /** Source label */
  label: SkillLabel
}

// =============================================================================
// Injection Types
// =============================================================================

/** Options for skill injection */
export interface SkillInjectionOptions {
  /** Render format (auto-detected from model if not specified) */
  format?: RenderFormat
  /** Include scripts listing */
  includeScripts?: boolean
  /** Include resources listing */
  includeResources?: boolean
}

/** Result of skill injection */
export interface SkillInjectionResult {
  /** Skill name */
  name: string
  /** Formatted content that was injected */
  content: string
  /** Whether injection succeeded */
  success: boolean
  /** Error message if failed */
  error?: string
}

// =============================================================================
// Skill Store State
// =============================================================================

/** Skills store state */
export interface SkillStoreState {
  /** Discovered skills by name (first match wins) */
  skills: Map<string, Skill>
  /** Last discovery timestamp */
  lastDiscovery: Date | null
  /** Discovery paths used */
  discoveryPaths: DiscoveryPath[]
  /** Active (loaded) skills per session */
  activeSessions: Map<string, Set<string>>
}
