/**
 * SKILL.md file discovery and parsing.
 *
 * Scans known directories for SKILL.md files and parses them
 * into Skill objects with YAML frontmatter.
 */

import type { IFileSystem } from '@ava/core-v2/platform'
import type { Skill } from './types.js'

/** Directories to scan for SKILL.md files (relative to project root). */
const SKILL_DIRS = ['.ava/skills', '.claude/skills', '.agents/skills']

/**
 * Discover and load SKILL.md files from known directories.
 */
export async function discoverSkills(cwd: string, fs: IFileSystem): Promise<Skill[]> {
  const skills: Skill[] = []

  for (const dir of SKILL_DIRS) {
    const dirPath = `${cwd}/${dir}`
    try {
      const entries = await fs.readDir(dirPath)
      for (const entry of entries) {
        const skillPath = `${dirPath}/${entry}/SKILL.md`
        try {
          const content = await fs.readFile(skillPath)
          const skill = parseSkillFile(content, skillPath)
          if (skill) skills.push(skill)
        } catch {
          // SKILL.md doesn't exist in this subdirectory — skip
        }
      }
    } catch {
      // Directory doesn't exist — skip
    }

    // Also check for a SKILL.md directly in the dir (no subdirectory)
    try {
      const directPath = `${cwd}/${dir}/SKILL.md`
      const content = await fs.readFile(directPath)
      const skill = parseSkillFile(content, directPath)
      if (skill) skills.push(skill)
    } catch {
      // File doesn't exist — skip
    }
  }

  return skills
}

/**
 * Parse a SKILL.md file into a Skill object.
 *
 * Expected format:
 * ```
 * ---
 * name: my-skill
 * description: Description here
 * globs:
 *   - "*.tsx"
 *   - "*.jsx"
 * ---
 * Content of the skill goes here...
 * ```
 */
export function parseSkillFile(rawContent: string, sourcePath: string): Skill | null {
  const { frontmatter, content } = parseFrontmatter(rawContent)
  if (!frontmatter.name || !content.trim()) return null

  const globs = parseGlobs(frontmatter.globs)
  if (globs.length === 0) return null

  return {
    name: String(frontmatter.name),
    description: String(frontmatter.description ?? ''),
    globs,
    projectTypes: parseStringArray(frontmatter.projectTypes),
    content: content.trim(),
    source: sourcePath,
  }
}

// ─── Frontmatter Parser ──────────────────────────────────────────────────────

interface Frontmatter {
  [key: string]: string | string[] | undefined
}

function parseFrontmatter(raw: string): { frontmatter: Frontmatter; content: string } {
  const match = raw.match(/^---\s*\n([\s\S]*?)\n---\s*\n([\s\S]*)$/)
  if (!match) return { frontmatter: {}, content: raw }

  const yamlBlock = match[1]!
  const content = match[2]!
  const frontmatter: Frontmatter = {}

  let currentKey = ''
  for (const line of yamlBlock.split('\n')) {
    const trimmed = line.trim()
    if (!trimmed || trimmed.startsWith('#')) continue

    // Array item: "  - value"
    if (trimmed.startsWith('- ') && currentKey) {
      const value = trimmed.slice(2).replace(/^["']|["']$/g, '')
      const existing = frontmatter[currentKey]
      if (Array.isArray(existing)) {
        existing.push(value)
      } else {
        frontmatter[currentKey] = [value]
      }
      continue
    }

    // Key-value: "key: value"
    const kvMatch = trimmed.match(/^(\w+)\s*:\s*(.*)$/)
    if (kvMatch) {
      currentKey = kvMatch[1]!
      const value = kvMatch[2]!.replace(/^["']|["']$/g, '')
      if (value) {
        frontmatter[currentKey] = value
      } else {
        // Empty value — likely an array follows
        frontmatter[currentKey] = []
      }
    }
  }

  return { frontmatter, content }
}

function parseGlobs(value: string | string[] | undefined): string[] {
  if (!value) return []
  if (typeof value === 'string') return [value]
  return value.filter(Boolean)
}

function parseStringArray(value: string | string[] | undefined): string[] | undefined {
  if (!value) return undefined
  if (typeof value === 'string') return [value]
  return value.length > 0 ? value : undefined
}
