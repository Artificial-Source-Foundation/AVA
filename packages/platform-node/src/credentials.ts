/**
 * Node.js Credential Store Implementation
 * Uses environment variables and a local config file
 */

import * as fs from 'node:fs/promises'
import * as os from 'node:os'
import * as path from 'node:path'
import type { ICredentialStore } from '@ava/core-v2'

const PRIMARY_CONFIG_DIR = path.join(os.homedir(), '.ava')
const LEGACY_CONFIG_DIR = path.join(os.homedir(), '.estela')
const PRIMARY_CREDS_FILE = path.join(PRIMARY_CONFIG_DIR, 'credentials.json')
const LEGACY_CREDS_FILE = path.join(LEGACY_CONFIG_DIR, 'credentials.json')

async function readCredentialsFile(filePath: string): Promise<Record<string, string> | null> {
  try {
    const content = await fs.readFile(filePath, 'utf-8')
    return JSON.parse(content) as Record<string, string>
  } catch {
    return null
  }
}

export class NodeCredentialStore implements ICredentialStore {
  private cache: Record<string, string> | null = null

  private async loadCredentials(): Promise<Record<string, string>> {
    if (this.cache) return this.cache

    const primaryCredentials = await readCredentialsFile(PRIMARY_CREDS_FILE)
    if (primaryCredentials) {
      this.cache = primaryCredentials
      return primaryCredentials
    }

    const legacyCredentials = await readCredentialsFile(LEGACY_CREDS_FILE)
    if (legacyCredentials) {
      this.cache = legacyCredentials
      try {
        await this.saveCredentials(legacyCredentials)
      } catch {
        // Non-fatal migration write failure; keep legacy credentials in memory.
      }
      return this.cache
    }

    this.cache = {}
    return this.cache
  }

  private async saveCredentials(creds: Record<string, string>): Promise<void> {
    await fs.mkdir(PRIMARY_CONFIG_DIR, { recursive: true })
    await fs.writeFile(PRIMARY_CREDS_FILE, JSON.stringify(creds, null, 2), {
      mode: 0o600, // User read/write only
    })
    this.cache = creds
  }

  async get(key: string): Promise<string | null> {
    // Check environment variable first (AVA_ prefix, with ESTELA_ legacy fallback)
    const normalizedKey = key.toUpperCase().replace(/-/g, '_')
    const avaEnvKey = `AVA_${normalizedKey}`
    const legacyEnvKey = `ESTELA_${normalizedKey}`

    if (process.env[avaEnvKey]) {
      return process.env[avaEnvKey]!
    }

    if (process.env[legacyEnvKey]) {
      return process.env[legacyEnvKey]!
    }

    // Fall back to stored credentials
    const creds = await this.loadCredentials()
    return creds[key] ?? null
  }

  async set(key: string, value: string): Promise<void> {
    const creds = await this.loadCredentials()
    creds[key] = value
    await this.saveCredentials(creds)
  }

  async delete(key: string): Promise<void> {
    const creds = await this.loadCredentials()
    delete creds[key]
    await this.saveCredentials(creds)
  }

  async has(key: string): Promise<boolean> {
    return (await this.get(key)) !== null
  }
}
