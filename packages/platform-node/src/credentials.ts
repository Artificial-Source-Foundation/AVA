/**
 * Node.js Credential Store Implementation
 * Uses environment variables and a local config file
 */

import * as fs from 'node:fs/promises'
import * as os from 'node:os'
import * as path from 'node:path'
import type { ICredentialStore } from '@estela/core'

const CONFIG_DIR = path.join(os.homedir(), '.estela')
const CREDS_FILE = path.join(CONFIG_DIR, 'credentials.json')

export class NodeCredentialStore implements ICredentialStore {
  private cache: Record<string, string> | null = null

  private async loadCredentials(): Promise<Record<string, string>> {
    if (this.cache) return this.cache

    try {
      const content = await fs.readFile(CREDS_FILE, 'utf-8')
      this.cache = JSON.parse(content)
      return this.cache!
    } catch {
      this.cache = {}
      return this.cache
    }
  }

  private async saveCredentials(creds: Record<string, string>): Promise<void> {
    await fs.mkdir(CONFIG_DIR, { recursive: true })
    await fs.writeFile(CREDS_FILE, JSON.stringify(creds, null, 2), {
      mode: 0o600, // User read/write only
    })
    this.cache = creds
  }

  async get(key: string): Promise<string | null> {
    // Check environment variable first
    const envKey = `ESTELA_${key.toUpperCase().replace(/-/g, '_')}`
    if (process.env[envKey]) {
      return process.env[envKey]!
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
