/**
 * Subdirectory AGENTS.md walking — resolves instruction files
 * by walking UP from the file being edited to the project root.
 *
 * 3-layer dedup:
 *   1. System: skip paths in `alreadyLoaded` set
 *   2. Per-turn: track paths loaded this invocation
 *   3. Content hash: skip if content matches a previously loaded file
 */

import type { IFileSystem } from '@ava/core-v2/platform'
import type { InstructionConfig, InstructionFile } from './types.js'

/**
 * Simple string hash for content dedup (djb2).
 */
function hashContent(content: string): number {
  let hash = 5381
  for (let i = 0; i < content.length; i++) {
    hash = ((hash << 5) + hash + content.charCodeAt(i)) | 0
  }
  return hash
}

/**
 * Get the parent directory of a path.
 * Returns null if we're already at root.
 */
function parentDir(dir: string): string | null {
  if (dir === '/') return null
  const parent = dir.replace(/\/[^/]+$/, '') || '/'
  return parent
}

/**
 * Get the directory portion of a file path.
 */
function dirname(filePath: string): string {
  const idx = filePath.lastIndexOf('/')
  if (idx <= 0) return '/'
  return filePath.slice(0, idx)
}

/**
 * Resolve instruction files by walking UP from the directory containing
 * `filePath` to `cwd` (inclusive). Does not walk beyond cwd.
 *
 * Uses 3-layer dedup to avoid redundant instruction loading.
 */
export async function resolveSubdirectoryInstructions(
  filePath: string,
  cwd: string,
  fs: IFileSystem,
  config: InstructionConfig,
  alreadyLoaded: Set<string>
): Promise<InstructionFile[]> {
  const results: InstructionFile[] = []
  const turnLoaded = new Set<string>()
  const seenHashes = new Set<number>()

  // Collect content hashes from alreadyLoaded for dedup layer 3.
  // We can't access content from paths alone, so we'll hash new content
  // and track within this invocation + across invocations via the set.

  let currentDir = dirname(filePath)

  // Normalize: ensure cwd doesn't have a trailing slash (unless root)
  const normalizedCwd = cwd === '/' ? '/' : cwd.replace(/\/+$/, '')

  // Walk from the file's directory up to (and including) cwd
  while (true) {
    // Check: are we still within or at cwd?
    if (!currentDir.startsWith(normalizedCwd)) break

    for (const fileName of config.fileNames) {
      const candidatePath = currentDir === '/' ? `/${fileName}` : `${currentDir}/${fileName}`

      // Layer 1: system dedup — skip if already loaded at session level
      if (alreadyLoaded.has(candidatePath)) continue

      // Layer 2: per-turn dedup — skip if already found this invocation
      if (turnLoaded.has(candidatePath)) continue

      try {
        const content = await fs.readFile(candidatePath)
        if (content.length > config.maxSize) continue

        // Layer 3: content hash dedup
        const hash = hashContent(content)
        if (seenHashes.has(hash)) continue

        seenHashes.add(hash)
        turnLoaded.add(candidatePath)

        results.push({
          path: candidatePath,
          content,
          scope: 'directory',
          priority: 0,
        })
      } catch {
        // File doesn't exist — skip
      }
    }

    // Stop if we've reached cwd
    if (currentDir === normalizedCwd) break

    // Move up
    const parent = parentDir(currentDir)
    if (parent === null) break
    currentDir = parent
  }

  return results
}
