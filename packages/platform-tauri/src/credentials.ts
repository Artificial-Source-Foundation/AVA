/**
 * Tauri Credential Store Implementation
 * Uses localStorage for now - can be enhanced with secure storage later
 */

import type { ICredentialStore } from '@ava/core-v2'

const STORAGE_PREFIX = 'ava_cred_'

export class TauriCredentialStore implements ICredentialStore {
  async get(key: string): Promise<string | null> {
    return localStorage.getItem(STORAGE_PREFIX + key)
  }

  async set(key: string, value: string): Promise<void> {
    localStorage.setItem(STORAGE_PREFIX + key, value)
  }

  async delete(key: string): Promise<void> {
    localStorage.removeItem(STORAGE_PREFIX + key)
  }

  async has(key: string): Promise<boolean> {
    return localStorage.getItem(STORAGE_PREFIX + key) !== null
  }
}
