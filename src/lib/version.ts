/**
 * Delta9 Version Utility
 *
 * Read version from package.json with caching for performance.
 */

import { readFileSync } from 'node:fs'
import { join, dirname } from 'node:path'
import { fileURLToPath } from 'node:url'

// =============================================================================
// State
// =============================================================================

let cachedVersion: string | null = null

// =============================================================================
// Helpers
// =============================================================================

/**
 * Get the Delta9 package version
 *
 * Reads from package.json and caches the result for subsequent calls.
 * Falls back to '0.0.0' if package.json cannot be read.
 *
 * @returns Version string (e.g., '1.0.0')
 */
export function getVersion(): string {
  if (cachedVersion !== null) {
    return cachedVersion
  }

  let version = '0.0.0'

  try {
    // Try to find package.json relative to this module
    const currentDir = dirname(fileURLToPath(import.meta.url))
    const packagePath = join(currentDir, '..', '..', 'package.json')
    const packageJson = JSON.parse(readFileSync(packagePath, 'utf-8'))
    version = packageJson.version || '0.0.0'
  } catch {
    // Fallback: try current working directory
    try {
      const packageJson = JSON.parse(readFileSync(join(process.cwd(), 'package.json'), 'utf-8'))
      version = packageJson.version || '0.0.0'
    } catch {
      // Keep default '0.0.0'
    }
  }

  cachedVersion = version
  return version
}

/**
 * Reset the cached version (useful for testing)
 */
export function resetVersionCache(): void {
  cachedVersion = null
}

/**
 * Get version with prefix
 *
 * @param prefix - Prefix to add (default: 'v')
 * @returns Prefixed version string (e.g., 'v1.0.0')
 */
export function getVersionWithPrefix(prefix = 'v'): string {
  return `${prefix}${getVersion()}`
}
