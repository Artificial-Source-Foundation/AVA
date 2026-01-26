/**
 * Delta9 Knowledge Store
 *
 * Letta-style memory blocks for persistent learning.
 * Stores markdown files with YAML frontmatter.
 *
 * Pattern: agent-memory plugin
 */

import * as fs from 'node:fs/promises'
import * as os from 'node:os'
import * as path from 'node:path'
import yaml from 'js-yaml'

import {
  type KnowledgeBlock,
  type KnowledgeFrontmatter,
  type KnowledgeScope,
  type KnowledgeStore,
  knowledgeFrontmatterSchema,
  SEED_BLOCKS,
  DEFAULT_LIMIT,
  MAX_LIMIT,
  LABEL_REGEX,
} from './types.js'

// =============================================================================
// Helpers
// =============================================================================

/**
 * Check if a file exists
 */
async function exists(filePath: string): Promise<boolean> {
  try {
    await fs.access(filePath)
    return true
  } catch {
    return false
  }
}

/**
 * Validate and sanitize label
 */
function validateLabel(label: string): string {
  const trimmed = label.trim().toLowerCase()
  if (!LABEL_REGEX.test(trimmed)) {
    throw new Error(
      `Invalid label "${label}". Use lowercase letters/numbers/dash/underscore (2-61 chars).`
    )
  }
  return trimmed
}

/**
 * Get directory for a scope
 */
function scopeDir(projectDirectory: string, scope: KnowledgeScope): string {
  return scope === 'global'
    ? path.join(os.homedir(), '.delta9', 'knowledge')
    : path.join(projectDirectory, '.delta9', 'knowledge')
}

/**
 * Split frontmatter from content
 */
function splitFrontmatter(text: string): {
  frontmatterText: string | undefined
  body: string
} {
  if (!text.startsWith('---\n')) {
    return { frontmatterText: undefined, body: text }
  }

  const endIndex = text.indexOf('\n---\n', 4)
  if (endIndex === -1) {
    return { frontmatterText: undefined, body: text }
  }

  const frontmatterText = text.slice(4, endIndex)
  const body = text.slice(endIndex + '\n---\n'.length)
  return { frontmatterText, body }
}

/**
 * Parse frontmatter YAML
 */
function parseFrontmatter(frontmatterText: string | undefined): KnowledgeFrontmatter {
  if (!frontmatterText) {
    return {}
  }

  const loaded = yaml.load(frontmatterText)
  const parsed = knowledgeFrontmatterSchema.safeParse(loaded)
  if (!parsed.success) {
    // Log warning but don't fail - return defaults
    console.warn(`Invalid frontmatter: ${parsed.error.message}`)
    return {}
  }

  return parsed.data
}

/**
 * Read a knowledge block file
 */
async function readBlockFile(scope: KnowledgeScope, filePath: string): Promise<KnowledgeBlock> {
  const [raw, stats] = await Promise.all([fs.readFile(filePath, 'utf-8'), fs.stat(filePath)])

  const { frontmatterText, body } = splitFrontmatter(raw)
  const fm = parseFrontmatter(frontmatterText)

  const label = (fm.label ?? path.basename(filePath, path.extname(filePath))).trim()
  const description = (fm.description ?? `Knowledge block: ${label}`).trim()
  const limit = fm.limit ?? DEFAULT_LIMIT
  const readOnly = fm.read_only ?? false
  const category = fm.category ?? 'custom'
  const value = body.trim()

  return {
    scope,
    label,
    description,
    limit,
    readOnly,
    category,
    value,
    filePath,
    lastModified: stats.mtime,
    charCount: value.length,
  }
}

/**
 * Write a knowledge block file
 */
async function writeBlockFile(
  filePath: string,
  block: Pick<KnowledgeBlock, 'label' | 'description' | 'limit' | 'readOnly' | 'category' | 'value'>
): Promise<void> {
  const frontmatter = {
    label: block.label,
    description: block.description,
    limit: block.limit,
    read_only: block.readOnly,
    category: block.category,
    updated_at: new Date().toISOString(),
  }

  const frontmatterYaml = yaml.dump(frontmatter, {
    lineWidth: 120,
    noRefs: true,
    sortKeys: true,
  })

  const content = `---\n${frontmatterYaml}---\n${block.value.trim()}\n`

  // Atomic write: temp file then rename
  const tempPath = path.join(path.dirname(filePath), `.${path.basename(filePath)}.tmp`)
  await fs.writeFile(tempPath, content, 'utf-8')
  await fs.rename(tempPath, filePath)
}

/**
 * Ensure gitignore exists for knowledge directory
 */
async function ensureGitignore(knowledgeDir: string): Promise<void> {
  const gitignorePath = path.join(knowledgeDir, '.gitignore')

  await fs.mkdir(knowledgeDir, { recursive: true })

  if (await exists(gitignorePath)) {
    return
  }

  // Ignore all knowledge files by default (they may contain sensitive info)
  await fs.writeFile(gitignorePath, '# Delta9 knowledge files\n*.md\n', 'utf-8')
}

/**
 * Stable sort for blocks (consistent ordering)
 */
function stableSortBlocks(blocks: KnowledgeBlock[]): KnowledgeBlock[] {
  const categoryOrder: Record<KnowledgeBlock['category'], number> = {
    patterns: 0,
    conventions: 1,
    gotchas: 2,
    decisions: 3,
    custom: 4,
  }

  blocks.sort((a, b) => {
    // First by scope (project before global)
    if (a.scope !== b.scope) {
      return a.scope === 'project' ? -1 : 1
    }
    // Then by category
    const catA = categoryOrder[a.category]
    const catB = categoryOrder[b.category]
    if (catA !== catB) {
      return catA - catB
    }
    // Finally by label
    return a.label.localeCompare(b.label)
  })

  return blocks
}

// =============================================================================
// Store Factory
// =============================================================================

/**
 * Create a knowledge store for a project
 */
export function createKnowledgeStore(projectDirectory: string): KnowledgeStore {
  return {
    async ensureSeed() {
      // Ensure directories exist
      const projectDir = scopeDir(projectDirectory, 'project')
      const globalDir = scopeDir(projectDirectory, 'global')

      await ensureGitignore(projectDir)
      await fs.mkdir(globalDir, { recursive: true })

      // Create seed blocks if they don't exist
      for (const seed of SEED_BLOCKS) {
        const dir = scopeDir(projectDirectory, seed.scope)
        const filePath = path.join(dir, `${seed.label}.md`)

        if (await exists(filePath)) {
          continue
        }

        await writeBlockFile(filePath, {
          label: seed.label,
          description: seed.description,
          limit: DEFAULT_LIMIT,
          readOnly: false,
          category: seed.category,
          value: `# ${seed.label.charAt(0).toUpperCase() + seed.label.slice(1)}\n\n_No entries yet._`,
        })
      }
    },

    async listBlocks(scope) {
      const scopes: KnowledgeScope[] = scope === 'all' ? ['project', 'global'] : [scope]
      const blocks: KnowledgeBlock[] = []

      for (const s of scopes) {
        const dir = scopeDir(projectDirectory, s)

        if (!(await exists(dir))) {
          continue
        }

        const entries = await fs.readdir(dir, { withFileTypes: true })

        for (const entry of entries) {
          if (!entry.isFile()) continue
          if (!entry.name.endsWith('.md')) continue
          if (entry.name.startsWith('.')) continue // Skip hidden/temp files

          const filePath = path.join(dir, entry.name)
          try {
            blocks.push(await readBlockFile(s, filePath))
          } catch (_err) {
            // Skip invalid files
            console.warn(`Skipping invalid knowledge block: ${filePath}`)
          }
        }
      }

      return stableSortBlocks(blocks)
    },

    async getBlock(scope, label) {
      const safeLabel = validateLabel(label)
      const dir = scopeDir(projectDirectory, scope)
      const filePath = path.join(dir, `${safeLabel}.md`)

      if (!(await exists(filePath))) {
        throw new Error(`Knowledge block not found: ${scope}:${safeLabel}`)
      }

      return readBlockFile(scope, filePath)
    },

    async setBlock(scope, label, value, opts) {
      const safeLabel = validateLabel(label)
      const dir = scopeDir(projectDirectory, scope)
      await fs.mkdir(dir, { recursive: true })

      const filePath = path.join(dir, `${safeLabel}.md`)
      const existing = (await exists(filePath)) ? await readBlockFile(scope, filePath) : undefined

      if (existing?.readOnly) {
        throw new Error(`Knowledge block is read-only: ${scope}:${safeLabel}`)
      }

      const description =
        opts?.description ?? existing?.description ?? `Knowledge block: ${safeLabel}`
      const limit = Math.min(opts?.limit ?? existing?.limit ?? DEFAULT_LIMIT, MAX_LIMIT)
      const category = opts?.category ?? existing?.category ?? 'custom'

      if (value.length > limit) {
        throw new Error(
          `Value too large for ${scope}:${safeLabel} (chars=${value.length}, limit=${limit})`
        )
      }

      await writeBlockFile(filePath, {
        label: safeLabel,
        description,
        limit,
        readOnly: existing?.readOnly ?? false,
        category,
        value,
      })
    },

    async appendBlock(scope, label, content, opts) {
      const block = await this.getBlock(scope, label)

      if (block.readOnly) {
        throw new Error(`Knowledge block is read-only: ${scope}:${block.label}`)
      }

      const separator = opts?.separator ?? '\n\n'
      const newValue = block.value + separator + content.trim()

      if (newValue.length > block.limit) {
        throw new Error(
          `Value too large for ${scope}:${block.label} after append (chars=${newValue.length}, limit=${block.limit})`
        )
      }

      await writeBlockFile(block.filePath, {
        label: block.label,
        description: block.description,
        limit: block.limit,
        readOnly: block.readOnly,
        category: block.category,
        value: newValue,
      })
    },

    async replaceInBlock(scope, label, oldText, newText) {
      const block = await this.getBlock(scope, label)

      if (block.readOnly) {
        throw new Error(`Knowledge block is read-only: ${scope}:${block.label}`)
      }

      if (!block.value.includes(oldText)) {
        throw new Error(`Old text not found in ${scope}:${block.label}`)
      }

      const newValue = block.value.replace(oldText, newText)

      if (newValue.length > block.limit) {
        throw new Error(
          `Value too large for ${scope}:${block.label} after replace (chars=${newValue.length}, limit=${block.limit})`
        )
      }

      await writeBlockFile(block.filePath, {
        label: block.label,
        description: block.description,
        limit: block.limit,
        readOnly: block.readOnly,
        category: block.category,
        value: newValue,
      })
    },

    async deleteBlock(scope, label) {
      const safeLabel = validateLabel(label)
      const dir = scopeDir(projectDirectory, scope)
      const filePath = path.join(dir, `${safeLabel}.md`)

      if (!(await exists(filePath))) {
        throw new Error(`Knowledge block not found: ${scope}:${safeLabel}`)
      }

      const block = await readBlockFile(scope, filePath)
      if (block.readOnly) {
        throw new Error(`Knowledge block is read-only: ${scope}:${safeLabel}`)
      }

      await fs.unlink(filePath)
    },
  }
}
