/**
 * Tauri Credential Store Implementation
 *
 * Multi-layer credential storage with the following priority:
 * 1. Environment variables (via env-bridge for Tauri)
 * 2. localStorage (fast in-memory access)
 * 3. ~/.ava/credentials.json (file-based persistence, shared with CLI)
 *
 * This ensures credentials work consistently between Desktop and CLI,
 * while allowing env var overrides for development.
 */

import type { ICredentialStore } from '@ava/core-v2'

const STORAGE_PREFIX = 'ava_cred_'
const CRED_FILE = '.ava/credentials.json'

/** Environment variable prefixes to check */
const ENV_PREFIXES = ['AVA_', 'ESTELA_']

/** Get environment variable (works in both Tauri and test environments) */
async function getEnvVar(name: string): Promise<string | undefined> {
  // Try global process.env first (polyfilled in Tauri)
  if (typeof process !== 'undefined' && process.env?.[name]) {
    return process.env[name]
  }

  // In Tauri context, try window.__TAURI__
  if (typeof window !== 'undefined' && '__TAURI__' in window) {
    try {
      const { invoke } = await import('@tauri-apps/api/core')
      const result = await invoke<string | null>('get_env_var', { name })
      return result ?? undefined
    } catch {
      return undefined
    }
  }

  return undefined
}
async function getTauriFs() {
  try {
    return await import('@tauri-apps/plugin-fs')
  } catch {
    return null
  }
}

/** Convert credential key to env var name (e.g., 'anthropic_api_key' -> 'ANTHROPIC_API_KEY') */
function keyToEnvVar(key: string): string {
  return key.toUpperCase().replace(/-/g, '_')
}

/** Read ~/.ava/credentials.json, returning {} on any failure */
async function readDiskCredentials(): Promise<Record<string, string>> {
  try {
    const fs = await getTauriFs()
    if (!fs) return {}
    if (!(await fs.exists(CRED_FILE, { baseDir: fs.BaseDirectory.Home }))) return {}
    const text = await fs.readTextFile(CRED_FILE, { baseDir: fs.BaseDirectory.Home })
    return JSON.parse(text) as Record<string, string>
  } catch {
    return {}
  }
}

/** Merge a key/value into ~/.ava/credentials.json */
async function writeDiskCredential(key: string, value: string | null): Promise<void> {
  try {
    const fs = await getTauriFs()
    if (!fs) return
    const creds = await readDiskCredentials()
    if (value !== null) {
      creds[key] = value
    } else {
      delete creds[key]
    }
    await fs.mkdir('.ava', { baseDir: fs.BaseDirectory.Home, recursive: true })
    await fs.writeTextFile(CRED_FILE, JSON.stringify(creds, null, 2), {
      baseDir: fs.BaseDirectory.Home,
    })
  } catch (err) {
    console.warn('Failed to write credential to disk:', err)
  }
}

export class TauriCredentialStore implements ICredentialStore {
  private diskCache: Record<string, string> | null = null
  private diskCacheTime = 0
  private readonly diskCacheTtl = 5000 // 5 seconds

  async get(key: string): Promise<string | null> {
    // Priority 1: Check environment variables
    const envKey = keyToEnvVar(key)

    // Try standard env var names
    const envValue = await getEnvVar(envKey)
    if (envValue) {
      return envValue
    }

    // Try AVA_/ESTELA_ prefixed env vars
    for (const prefix of ENV_PREFIXES) {
      const prefixedValue = await getEnvVar(`${prefix}${envKey}`)
      if (prefixedValue) {
        return prefixedValue
      }
    }

    // Priority 2: Check localStorage
    const localValue = localStorage.getItem(STORAGE_PREFIX + key)
    if (localValue) {
      return localValue
    }

    // Priority 3: Check disk cache (with TTL)
    const diskCreds = await this.getDiskCredentials()
    if (diskCreds[key]) {
      // Also cache in localStorage for faster access next time
      localStorage.setItem(STORAGE_PREFIX + key, diskCreds[key])
      return diskCreds[key]
    }

    return null
  }

  async set(key: string, value: string): Promise<void> {
    // Always write to localStorage (fast)
    localStorage.setItem(STORAGE_PREFIX + key, value)

    // Also write to disk (for CLI compatibility)
    await writeDiskCredential(key, value)

    // Update in-memory cache
    if (this.diskCache) {
      this.diskCache[key] = value
    }
  }

  async delete(key: string): Promise<void> {
    // Remove from localStorage
    localStorage.removeItem(STORAGE_PREFIX + key)

    // Remove from disk
    await writeDiskCredential(key, null)

    // Update in-memory cache
    if (this.diskCache) {
      delete this.diskCache[key]
    }
  }

  async has(key: string): Promise<boolean> {
    const value = await this.get(key)
    return value !== null
  }

  /**
   * Get disk credentials with caching
   */
  private async getDiskCredentials(): Promise<Record<string, string>> {
    const now = Date.now()
    if (this.diskCache && now - this.diskCacheTime < this.diskCacheTtl) {
      return this.diskCache
    }

    this.diskCache = await readDiskCredentials()
    this.diskCacheTime = now
    return this.diskCache
  }

  /**
   * Bulk-sync all localStorage credentials to ~/.ava/credentials.json.
   * Call once at startup to migrate credentials stored before disk sync existed.
   */
  async syncAllToDisk(): Promise<void> {
    const fs = await getTauriFs()
    if (!fs) return

    const existing = await readDiskCredentials()
    let changed = false

    for (let i = 0; i < localStorage.length; i++) {
      const fullKey = localStorage.key(i)
      if (!fullKey?.startsWith(STORAGE_PREFIX)) continue
      const key = fullKey.slice(STORAGE_PREFIX.length)
      const value = localStorage.getItem(fullKey)
      if (value && existing[key] !== value) {
        existing[key] = value
        changed = true
      }
    }

    if (!changed) return

    await fs.mkdir('.ava', { baseDir: fs.BaseDirectory.Home, recursive: true })
    await fs.writeTextFile(CRED_FILE, JSON.stringify(existing, null, 2), {
      baseDir: fs.BaseDirectory.Home,
    })
  }

  /**
   * Clear the disk cache (useful after external credential changes)
   */
  clearCache(): void {
    this.diskCache = null
    this.diskCacheTime = 0
  }
}
