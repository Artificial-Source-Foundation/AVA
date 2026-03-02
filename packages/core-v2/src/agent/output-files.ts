/**
 * Save overflow tool output to disk when results exceed truncation limits.
 *
 * Files go to ~/.ava/tool-output/<timestamp>.txt with lazy 7-day cleanup.
 */

import { homedir } from 'node:os'
import { join } from 'node:path'
import { getPlatform } from '../platform.js'

const OUTPUT_DIR = join(homedir(), '.ava', 'tool-output')
const SEVEN_DAYS_MS = 7 * 24 * 60 * 60 * 1000

/**
 * Save full tool output to a file when it was truncated.
 * Returns the file path on success, null on failure.
 */
export async function saveOverflowOutput(content: string): Promise<string | null> {
  try {
    const fs = getPlatform().fs

    // Ensure output directory exists
    const dirExists = await fs.exists(OUTPUT_DIR).catch(() => false)
    if (!dirExists) {
      await fs.mkdir(OUTPUT_DIR)
    }

    // Lazy cleanup: remove files older than 7 days
    await cleanupOldFiles(fs).catch(() => {
      // Cleanup failure is non-fatal
    })

    // Generate unique filename using timestamp + random suffix
    const timestamp = Date.now()
    const suffix = Math.random().toString(36).slice(2, 8)
    const filename = `${timestamp}-${suffix}.txt`
    const filePath = join(OUTPUT_DIR, filename)

    await fs.writeFile(filePath, content)
    return filePath
  } catch {
    return null
  }
}

/** Remove files in the output directory older than 7 days. */
async function cleanupOldFiles(fs: ReturnType<typeof getPlatform>['fs']): Promise<void> {
  const entries = await fs.readDir(OUTPUT_DIR).catch(() => [] as string[])
  const now = Date.now()

  for (const entry of entries) {
    if (!entry.endsWith('.txt')) continue
    const fullPath = join(OUTPUT_DIR, entry)
    try {
      const info = await fs.stat(fullPath)
      if (info.isFile && now - info.mtime > SEVEN_DAYS_MS) {
        await fs.remove(fullPath)
      }
    } catch {
      // Skip files we can't stat
    }
  }
}

/** Exposed for testing. */
export const _internals = { OUTPUT_DIR, SEVEN_DAYS_MS, cleanupOldFiles }
