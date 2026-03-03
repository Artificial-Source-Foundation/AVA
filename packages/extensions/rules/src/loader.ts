/**
 * Rule file discovery and parsing.
 *
 * Scans known directories for .md rule files and parses them
 * into Rule objects with YAML frontmatter.
 * Rules are flat files (e.g. `.ava/rules/testing.md`), not subdirectories.
 */

import type { IFileSystem } from '@ava/core-v2/platform'
import { parseFrontmatter, parseGlobs } from '../../skills/src/frontmatter.js'
import type { Rule, RuleActivation } from './types.js'

/** Directories to scan for rule .md files (relative to project root). */
const RULE_DIRS = ['.ava/rules', '.claude/rules', '.cursor/rules']

const VALID_ACTIVATIONS = new Set<RuleActivation>(['always', 'auto', 'manual'])

/**
 * Discover and load rule files from known directories.
 */
export async function discoverRules(cwd: string, fs: IFileSystem): Promise<Rule[]> {
  const rules: Rule[] = []

  for (const dir of RULE_DIRS) {
    const dirPath = `${cwd}/${dir}`
    try {
      const entries = await fs.readDir(dirPath)
      for (const entry of entries) {
        if (!entry.endsWith('.md')) continue
        const rulePath = `${dirPath}/${entry}`
        try {
          const content = await fs.readFile(rulePath)
          const rule = parseRuleFile(content, rulePath)
          if (rule) rules.push(rule)
        } catch {
          // File read failed — skip
        }
      }
    } catch {
      // Directory doesn't exist — skip
    }
  }

  return rules
}

/**
 * Parse a rule .md file into a Rule object.
 *
 * Expected format:
 * ```
 * ---
 * description: Testing conventions
 * globs:
 *   - "**\/*.test.ts"
 * activation: auto
 * ---
 * Rule content goes here...
 * ```
 *
 * Cursor compatibility: `alwaysApply: true` maps to `activation: 'always'`.
 */
export function parseRuleFile(rawContent: string, sourcePath: string): Rule | null {
  const { frontmatter, content } = parseFrontmatter(rawContent)
  if (!content.trim()) return null

  // Derive name from filename (e.g. /path/to/testing.md → testing)
  const fileName = sourcePath.split('/').pop() ?? ''
  const name = fileName.replace(/\.md$/i, '')
  if (!name) return null

  const description = String(frontmatter.description ?? '')

  // Determine activation mode
  let activation: RuleActivation = 'auto'
  if (frontmatter.activation && VALID_ACTIVATIONS.has(frontmatter.activation as RuleActivation)) {
    activation = frontmatter.activation as RuleActivation
  }
  // Cursor compat: alwaysApply: true → activation: 'always'
  if (frontmatter.alwaysApply === 'true') {
    activation = 'always'
  }

  const globs = parseGlobs(frontmatter.globs)

  // Globs are required unless activation is 'always'
  if (activation !== 'always' && globs.length === 0) return null

  return {
    name,
    description,
    globs,
    activation,
    content: content.trim(),
    source: sourcePath,
  }
}
