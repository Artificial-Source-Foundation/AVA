/**
 * SKILL.md file discovery and parsing.
 *
 * Scans known directories for SKILL.md files and parses them
 * into Skill objects with YAML frontmatter.
 */

import type { IFileSystem } from '@ava/core-v2/platform'
import { parseFrontmatter, parseGlobs, parseStringArray } from './frontmatter.js'
import type { Skill, SkillActivation } from './types.js'

const VALID_ACTIVATIONS = new Set<SkillActivation>(['auto', 'agent', 'always', 'manual'])

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

  // Parse activation mode (default: 'auto' for backward compat)
  let activation: SkillActivation = 'auto'
  if (frontmatter.activation && VALID_ACTIVATIONS.has(frontmatter.activation as SkillActivation)) {
    activation = frontmatter.activation as SkillActivation
  }

  return {
    name: String(frontmatter.name),
    description: String(frontmatter.description ?? ''),
    globs,
    activation,
    projectTypes: parseStringArray(frontmatter.projectTypes),
    content: content.trim(),
    source: sourcePath,
  }
}
