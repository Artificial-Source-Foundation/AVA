/**
 * Skill Loader
 * Parse skill files with YAML frontmatter
 */

import { type Skill, type SkillFrontmatter, SkillFrontmatterSchema } from './types.js'

// ============================================================================
// Simple YAML Parser (for frontmatter only)
// ============================================================================

/**
 * Parse simple YAML (key-value pairs and arrays)
 * This is a minimal parser for frontmatter, not a full YAML parser
 */
function parseSimpleYaml(content: string): Record<string, unknown> {
  const result: Record<string, unknown> = {}
  const lines = content.split('\n')
  let currentKey = ''
  let currentArray: string[] | null = null

  for (const line of lines) {
    // Skip empty lines and comments
    if (line.trim() === '' || line.trim().startsWith('#')) {
      continue
    }

    // Check for array item
    if (line.match(/^\s+-\s+/)) {
      const value = line.replace(/^\s+-\s+/, '').trim()
      if (currentArray !== null) {
        // Remove quotes if present
        currentArray.push(value.replace(/^["']|["']$/g, ''))
      }
      continue
    }

    // Save current array if we're moving to a new key
    if (currentArray !== null && currentKey) {
      result[currentKey] = currentArray
      currentArray = null
    }

    // Check for key-value pair
    const kvMatch = line.match(/^(\w+):\s*(.*)$/)
    if (kvMatch) {
      const [, key, rawValue] = kvMatch
      const value = rawValue.trim()

      if (value === '' || value === '[]') {
        // Empty value or empty array - start collecting array items
        currentKey = key
        currentArray = []
      } else if (value.startsWith('[') && value.endsWith(']')) {
        // Inline array
        const arrayContent = value.slice(1, -1)
        const items = arrayContent.split(',').map((s) => s.trim().replace(/^["']|["']$/g, ''))
        result[key] = items.filter((s) => s.length > 0)
      } else {
        // Simple value - remove quotes if present
        result[key] = value.replace(/^["']|["']$/g, '')
      }
    }
  }

  // Save final array if present
  if (currentArray !== null && currentKey) {
    result[currentKey] = currentArray
  }

  return result
}

// ============================================================================
// Constants
// ============================================================================

/** YAML frontmatter delimiter */
const FRONTMATTER_DELIMITER = '---'

/** Skill file name pattern */
export const SKILL_FILE_NAME = 'SKILL.md'

// ============================================================================
// Parsing Functions
// ============================================================================

/**
 * Parse YAML frontmatter from markdown content
 *
 * @param content - Raw file content
 * @returns Parsed frontmatter and remaining content
 */
export function parseFrontmatter(content: string): {
  frontmatter: SkillFrontmatter | null
  content: string
  error?: string
} {
  const lines = content.split('\n')

  // Check for opening delimiter
  if (lines[0]?.trim() !== FRONTMATTER_DELIMITER) {
    return {
      frontmatter: null,
      content,
      error: 'No frontmatter found (file must start with ---)',
    }
  }

  // Find closing delimiter
  let closingIndex = -1
  for (let i = 1; i < lines.length; i++) {
    if (lines[i]?.trim() === FRONTMATTER_DELIMITER) {
      closingIndex = i
      break
    }
  }

  if (closingIndex === -1) {
    return {
      frontmatter: null,
      content,
      error: 'Unclosed frontmatter (missing closing ---)',
    }
  }

  // Extract and parse YAML
  const yamlContent = lines.slice(1, closingIndex).join('\n')
  const markdownContent = lines
    .slice(closingIndex + 1)
    .join('\n')
    .trim()

  try {
    const parsed = parseSimpleYaml(yamlContent)
    const result = SkillFrontmatterSchema.safeParse(parsed)

    if (!result.success) {
      return {
        frontmatter: null,
        content: markdownContent,
        error: `Invalid frontmatter: ${result.error.message}`,
      }
    }

    return {
      frontmatter: result.data,
      content: markdownContent,
    }
  } catch (err) {
    const message = err instanceof Error ? err.message : String(err)
    return {
      frontmatter: null,
      content: markdownContent,
      error: `YAML parse error: ${message}`,
    }
  }
}

/**
 * Load a skill from file content
 *
 * @param content - Raw file content
 * @param path - Path to the skill file
 * @returns Parsed skill or error
 */
export function loadSkill(
  content: string,
  path: string
): { skill: Skill; error?: undefined } | { skill?: undefined; error: string } {
  const { frontmatter, content: markdownContent, error } = parseFrontmatter(content)

  if (!frontmatter) {
    return { error: error || 'Failed to parse frontmatter' }
  }

  const skill: Skill = {
    name: frontmatter.name,
    description: frontmatter.description,
    globs: frontmatter.globs,
    content: markdownContent,
    path,
    version: frontmatter.version,
    author: frontmatter.author,
    tags: frontmatter.tags,
  }

  return { skill }
}

/**
 * Validate a skill's content
 *
 * @param skill - Skill to validate
 * @returns Validation errors, if any
 */
export function validateSkill(skill: Skill): string[] {
  const errors: string[] = []

  if (!skill.name || skill.name.trim() === '') {
    errors.push('Skill must have a name')
  }

  if (!skill.content || skill.content.trim() === '') {
    errors.push('Skill must have content')
  }

  if (skill.name && !/^[a-z0-9-]+$/.test(skill.name)) {
    errors.push('Skill name must be lowercase alphanumeric with hyphens only')
  }

  return errors
}
