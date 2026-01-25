/**
 * Delta9 Skills System - Loader
 *
 * Discovers and loads skills from multiple paths:
 * - Project: .delta9/skills/ (highest priority)
 * - User: ~/.config/delta9/skills/
 * - Global: ~/.delta9/skills/
 * - Builtin: Bundled with plugin (lowest priority)
 *
 * First match wins - project skills override user/global/builtin.
 */

import * as fs from 'node:fs/promises'
import * as path from 'node:path'
import { homedir } from 'node:os'
import yaml from 'js-yaml'
import {
  SkillFrontmatterSchema,
  type Skill,
  type SkillLabel,
  type SkillScript,
  type SkillResource,
  type DiscoveryPath,
  type FileDiscoveryResult,
  type SkillSummary,
} from './types.js'

// =============================================================================
// Constants
// =============================================================================

/** Default discovery paths in priority order */
export const DEFAULT_DISCOVERY_PATHS: DiscoveryPath[] = [
  // Project-level (highest priority)
  { path: '.delta9/skills', label: 'project', maxDepth: 3 },
  { path: '.opencode/skills', label: 'project', maxDepth: 3 },
  { path: '.claude/skills', label: 'project', maxDepth: 1 },

  // User-level
  { path: '~/.config/delta9/skills', label: 'user', maxDepth: 3 },
  { path: '~/.config/opencode/skills', label: 'user', maxDepth: 3 },
  { path: '~/.claude/skills', label: 'user', maxDepth: 1 },

  // Global-level
  { path: '~/.delta9/skills', label: 'global', maxDepth: 3 },
]

/** Directories to skip during discovery */
const SKIP_DIRS = new Set(['node_modules', '__pycache__', '.git', '.venv', 'venv', '.tox', '.nox', 'dist', 'build'])

// =============================================================================
// Utilities
// =============================================================================

/**
 * Expand ~ to home directory
 */
function expandPath(p: string): string {
  if (p.startsWith('~')) {
    return path.join(homedir(), p.slice(1))
  }
  return p
}

/**
 * Check if a path exists
 */
async function pathExists(p: string): Promise<boolean> {
  try {
    await fs.access(p)
    return true
  } catch {
    return false
  }
}

/**
 * Parse YAML frontmatter from markdown content
 */
export function parseFrontmatter<T>(content: string): { data: T; body: string } {
  const match = content.match(/^---\r?\n([\s\S]*?)(?:\r?\n)?---(?:\r?\n)?([\s\S]*)$/)
  if (!match) {
    return { data: {} as T, body: content }
  }

  try {
    const yamlContent = match[1].trim()
    const parsed = yamlContent ? yaml.load(yamlContent) : null
    return { data: (parsed as T) ?? ({} as T), body: match[2].trim() }
  } catch {
    return { data: {} as T, body: content }
  }
}

// =============================================================================
// Script & Resource Discovery
// =============================================================================

/**
 * Find executable scripts in a skill directory
 */
async function findScripts(skillPath: string, maxDepth: number = 10): Promise<SkillScript[]> {
  const scripts: SkillScript[] = []

  async function recurse(dir: string, depth: number, relPath: string): Promise<void> {
    if (depth > maxDepth) return

    try {
      const entries = await fs.readdir(dir, { withFileTypes: true })

      for (const entry of entries) {
        if (entry.name.startsWith('.')) continue
        if (SKIP_DIRS.has(entry.name)) continue

        const fullPath = path.join(dir, entry.name)
        const newRelPath = relPath ? `${relPath}/${entry.name}` : entry.name

        try {
          const stats = await fs.stat(fullPath)

          if (stats.isDirectory()) {
            await recurse(fullPath, depth + 1, newRelPath)
          } else if (stats.isFile()) {
            // Check if executable (any execute bit set)
            if (stats.mode & 0o111) {
              scripts.push({
                relativePath: newRelPath,
                absolutePath: fullPath,
              })
            }
          }
        } catch {
          continue
        }
      }
    } catch {
      // Directory not accessible
    }
  }

  await recurse(skillPath, 0, '')
  return scripts.sort((a, b) => a.relativePath.localeCompare(b.relativePath))
}

/**
 * Find resource files in a skill directory
 */
async function findResources(skillPath: string, maxDepth: number = 3): Promise<SkillResource[]> {
  const resources: SkillResource[] = []

  async function recurse(dir: string, depth: number, relPath: string): Promise<void> {
    if (depth > maxDepth) return

    try {
      const entries = await fs.readdir(dir, { withFileTypes: true })

      for (const entry of entries) {
        if (entry.name.startsWith('.')) continue
        if (SKIP_DIRS.has(entry.name)) continue

        const fullPath = path.join(dir, entry.name)
        const newRelPath = relPath ? `${relPath}/${entry.name}` : entry.name

        try {
          const stats = await fs.stat(fullPath)

          if (stats.isDirectory()) {
            await recurse(fullPath, depth + 1, newRelPath)
          } else if (stats.isFile() && entry.name !== 'SKILL.md') {
            resources.push({
              relativePath: newRelPath,
              absolutePath: fullPath,
              type: path.extname(entry.name).slice(1) || 'unknown',
            })
          }
        } catch {
          continue
        }
      }
    } catch {
      // Directory not accessible
    }
  }

  await recurse(skillPath, 0, '')
  return resources.sort((a, b) => a.relativePath.localeCompare(b.relativePath))
}

// =============================================================================
// Skill Discovery
// =============================================================================

/**
 * Find SKILL.md files recursively in a directory
 */
async function findSkillsRecursive(
  baseDir: string,
  label: SkillLabel,
  maxDepth: number = 3
): Promise<FileDiscoveryResult[]> {
  const results: FileDiscoveryResult[] = []

  async function recurse(dir: string, depth: number, relPath: string): Promise<void> {
    if (depth > maxDepth) return

    try {
      const entries = await fs.readdir(dir, { withFileTypes: true })

      for (const entry of entries) {
        if (entry.name.startsWith('.')) continue
        if (SKIP_DIRS.has(entry.name)) continue
        if (!entry.isDirectory()) continue

        const fullPath = path.join(dir, entry.name)
        const newRelPath = relPath ? `${relPath}/${entry.name}` : entry.name

        // Check for SKILL.md in this directory
        const skillFilePath = path.join(fullPath, 'SKILL.md')
        if (await pathExists(skillFilePath)) {
          results.push({
            filePath: skillFilePath,
            relativePath: newRelPath,
            label,
          })
        } else {
          // Continue searching deeper
          await recurse(fullPath, depth + 1, newRelPath)
        }
      }
    } catch {
      // Directory not accessible
    }
  }

  if (await pathExists(baseDir)) {
    await recurse(baseDir, 0, '')
  }

  return results
}

/**
 * Parse a SKILL.md file and validate its frontmatter
 */
async function parseSkillFile(
  skillPath: string,
  relativePath: string,
  label: SkillLabel
): Promise<Skill | null> {
  try {
    const content = await fs.readFile(skillPath, 'utf-8')
    const { data, body } = parseFrontmatter<Record<string, unknown>>(content)

    // Validate frontmatter
    const parseResult = SkillFrontmatterSchema.safeParse(data)
    if (!parseResult.success) {
      return null
    }

    const frontmatter = parseResult.data
    const skillDirPath = path.dirname(skillPath)

    // Discover scripts and resources
    const [scripts, resources] = await Promise.all([findScripts(skillDirPath), findResources(skillDirPath)])

    return {
      name: frontmatter.name,
      description: frontmatter.description,
      useWhen: frontmatter.use_when,
      label,
      path: skillDirPath,
      relativePath,
      template: body,
      namespace: frontmatter.metadata?.namespace,
      allowedTools: frontmatter['allowed-tools'],
      mcp: frontmatter.mcp,
      scripts,
      resources,
    }
  } catch {
    return null
  }
}

// =============================================================================
// Public API
// =============================================================================

/**
 * Discover all skills from configured paths
 *
 * Discovery order (first found wins):
 * 1. .delta9/skills/      (project)
 * 2. .opencode/skills/    (project)
 * 3. .claude/skills/      (project)
 * 4. ~/.config/delta9/skills/  (user)
 * 5. ~/.config/opencode/skills/ (user)
 * 6. ~/.claude/skills/    (user)
 * 7. ~/.delta9/skills/    (global)
 */
export async function discoverSkills(
  projectDir: string,
  customPaths?: DiscoveryPath[]
): Promise<Map<string, Skill>> {
  const paths = customPaths ?? DEFAULT_DISCOVERY_PATHS
  const allResults: FileDiscoveryResult[] = []

  for (const { path: basePath, label, maxDepth } of paths) {
    // Expand ~ and resolve relative paths
    let fullPath: string
    if (basePath.startsWith('~')) {
      fullPath = expandPath(basePath)
    } else if (path.isAbsolute(basePath)) {
      fullPath = basePath
    } else {
      fullPath = path.join(projectDir, basePath)
    }

    const found = await findSkillsRecursive(fullPath, label, maxDepth)
    allResults.push(...found)
  }

  // Parse skills, first match wins
  const skillsByName = new Map<string, Skill>()

  for (const result of allResults) {
    const skill = await parseSkillFile(result.filePath, result.relativePath, result.label)
    if (!skill) continue

    // First match wins - don't override
    if (!skillsByName.has(skill.name)) {
      skillsByName.set(skill.name, skill)
    }
  }

  return skillsByName
}

/**
 * Load a single skill by name
 */
export async function loadSkill(
  skillName: string,
  projectDir: string,
  customPaths?: DiscoveryPath[]
): Promise<Skill | null> {
  const skills = await discoverSkills(projectDir, customPaths)
  return skills.get(skillName) ?? null
}

/**
 * Resolve a skill by name, handling namespace prefixes
 * Supports: "skill-name", "project:skill-name", "user:skill-name"
 */
export function resolveSkill(skillName: string, skillsByName: Map<string, Skill>): Skill | null {
  if (skillName.includes(':')) {
    const [namespace, name] = skillName.split(':')

    for (const skill of skillsByName.values()) {
      if (skill.name === name && (skill.label === namespace || skill.namespace === namespace)) {
        return skill
      }
    }
    return null
  }

  return skillsByName.get(skillName) ?? null
}

/**
 * Get summaries of all available skills
 */
export async function getSkillSummaries(
  projectDir: string,
  customPaths?: DiscoveryPath[]
): Promise<SkillSummary[]> {
  const skills = await discoverSkills(projectDir, customPaths)

  return Array.from(skills.values()).map((skill) => ({
    name: skill.name,
    description: skill.description,
    label: skill.label,
    useWhen: skill.useWhen,
  }))
}

/**
 * Read a resource file from a skill
 */
export async function readSkillResource(skill: Skill, relativePath: string): Promise<string | null> {
  const resource = skill.resources.find((r) => r.relativePath === relativePath)
  if (!resource) return null

  try {
    return await fs.readFile(resource.absolutePath, 'utf-8')
  } catch {
    return null
  }
}

/**
 * List all files in a skill directory
 */
export async function listSkillFiles(skill: Skill): Promise<string[]> {
  const files: string[] = []

  // Add scripts
  for (const script of skill.scripts) {
    files.push(script.relativePath)
  }

  // Add resources
  for (const resource of skill.resources) {
    files.push(resource.relativePath)
  }

  return files.sort()
}
