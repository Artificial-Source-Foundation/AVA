/**
 * Skill Discovery
 * Find and load skills from configured directories
 */

import * as path from 'node:path'
import { getPlatform } from '../platform.js'
import { loadSkill, SKILL_FILE_NAME, validateSkill } from './loader.js'
import type { Skill, SkillDiscoveryConfig, SkillDiscoveryResult } from './types.js'

// ============================================================================
// Constants
// ============================================================================

/** Default project skill directory name */
const DEFAULT_PROJECT_SKILL_DIR = '.estela/skills'

/** Default user skill directory */
const DEFAULT_USER_SKILL_DIR = '.estela/skills'

// ============================================================================
// Discovery Functions
// ============================================================================

/**
 * Get default skill discovery configuration
 *
 * @param workingDirectory - Current working directory
 * @returns Discovery configuration
 */
export function getDefaultDiscoveryConfig(workingDirectory: string): SkillDiscoveryConfig {
  const homeDir = process.env.HOME || process.env.USERPROFILE || ''

  return {
    projectDir: path.join(workingDirectory, DEFAULT_PROJECT_SKILL_DIR),
    userDir: homeDir ? path.join(homeDir, DEFAULT_USER_SKILL_DIR) : undefined,
    customDirs: [],
  }
}

/**
 * Discover all skills from configured directories
 *
 * @param config - Discovery configuration
 * @returns Discovery result with all found skills
 */
export async function discoverSkills(config: SkillDiscoveryConfig): Promise<SkillDiscoveryResult> {
  const fs = getPlatform().fs
  const skills: Skill[] = []
  const searchedPaths: string[] = []
  const errors: Array<{ path: string; error: string }> = []

  // Collect all directories to search
  const directories: string[] = []

  if (config.projectDir) {
    directories.push(config.projectDir)
  }
  if (config.userDir) {
    directories.push(config.userDir)
  }
  if (config.customDirs) {
    directories.push(...config.customDirs)
  }

  // Search each directory
  for (const dir of directories) {
    searchedPaths.push(dir)

    try {
      const stat = await fs.stat(dir)
      if (!stat.isDirectory) {
        continue
      }

      // Find all SKILL.md files recursively
      const skillPaths = await findSkillFiles(dir)

      for (const skillPath of skillPaths) {
        try {
          const content = await fs.readFile(skillPath)
          const result = loadSkill(content, skillPath)

          if (result.error || !result.skill) {
            errors.push({ path: skillPath, error: result.error || 'Unknown error' })
            continue
          }

          const skill = result.skill

          // Validate the skill
          const validationErrors = validateSkill(skill)
          if (validationErrors.length > 0) {
            errors.push({ path: skillPath, error: validationErrors.join('; ') })
            continue
          }

          // Check for duplicate names
          const existingIndex = skills.findIndex((s) => s.name === skill.name)
          if (existingIndex !== -1) {
            // Project skills override user skills (project is searched first)
            // So only replace if the new one is from a "higher priority" location
            // For now, first one wins
            continue
          }

          skills.push(skill)
        } catch (err) {
          const message = err instanceof Error ? err.message : String(err)
          errors.push({ path: skillPath, error: message })
        }
      }
    } catch {
      // Directory doesn't exist or can't be accessed, skip silently
    }
  }

  return { skills, searchedPaths, errors }
}

/**
 * Find all SKILL.md files in a directory recursively
 *
 * @param dir - Directory to search
 * @returns Array of skill file paths
 */
async function findSkillFiles(dir: string): Promise<string[]> {
  const fs = getPlatform().fs
  const skillFiles: string[] = []

  try {
    const entries = await fs.readDir(dir)

    for (const entry of entries) {
      const fullPath = path.join(dir, entry)

      try {
        const stat = await fs.stat(fullPath)

        if (stat.isDirectory) {
          // Recursively search subdirectories
          const subFiles = await findSkillFiles(fullPath)
          skillFiles.push(...subFiles)
        } else if (entry === SKILL_FILE_NAME) {
          skillFiles.push(fullPath)
        }
      } catch {
        // Skip entries that can't be accessed
      }
    }
  } catch {
    // Directory can't be read, return empty
  }

  return skillFiles
}

/**
 * Find a skill by name
 *
 * @param name - Skill name to find
 * @param config - Discovery configuration
 * @returns Skill if found, undefined otherwise
 */
export async function findSkillByName(
  name: string,
  config: SkillDiscoveryConfig
): Promise<Skill | undefined> {
  const result = await discoverSkills(config)
  return result.skills.find((s) => s.name === name)
}

/**
 * Find skills that match a file path based on glob patterns
 *
 * @param filePath - File path to match
 * @param config - Discovery configuration
 * @returns Skills whose globs match the file path
 */
export async function findSkillsForFile(
  filePath: string,
  config: SkillDiscoveryConfig
): Promise<Skill[]> {
  const result = await discoverSkills(config)

  return result.skills.filter((skill) => {
    if (!skill.globs || skill.globs.length === 0) {
      return false
    }

    // Simple glob matching (basic patterns only)
    return skill.globs.some((pattern) => matchesGlobPattern(filePath, pattern))
  })
}

/**
 * Simple glob pattern matching
 * Supports: *, **, ?
 */
function matchesGlobPattern(filePath: string, pattern: string): boolean {
  // Normalize paths
  const normalizedPath = filePath.replace(/\\/g, '/')
  const normalizedPattern = pattern.replace(/\\/g, '/')

  // Convert glob to regex
  let regexPattern = normalizedPattern
    // Escape special regex chars (except * and ?)
    .replace(/[.+^${}()|[\]\\]/g, '\\$&')
    // ** matches any path
    .replace(/\*\*/g, '{{DOUBLE_STAR}}')
    // * matches any filename chars (not path separator)
    .replace(/\*/g, '[^/]*')
    // ? matches single char
    .replace(/\?/g, '.')
    // Restore **
    .replace(/{{DOUBLE_STAR}}/g, '.*')

  // Anchor the pattern
  regexPattern = `^${regexPattern}$`

  try {
    const regex = new RegExp(regexPattern)
    return regex.test(normalizedPath)
  } catch {
    return false
  }
}
