/**
 * Auth Helpers
 * Thin wrappers around getPlatform().credentials for OAuth storage.
 * Replaces setStoredAuth/removeStoredAuth from @ava/core.
 *
 * Key format must match what core-v2 getAuth()/getApiKey() reads:
 *   OAuth token:  ava:{provider}:oauth_token
 *   Account ID:   ava:{provider}:account_id
 *   API key:      ava:{provider}:api_key
 */

import { invoke } from '@tauri-apps/api/core'

/** Minimal credential store interface (replaces @ava/core-v2/platform import) */
const credentials = {
  async set(key: string, value: string): Promise<void> {
    try {
      await invoke('set_credential', { key, value })
    } catch {
      // Fallback to localStorage
      localStorage.setItem(`ava_cred_${key}`, value)
    }
  },
  async delete(key: string): Promise<void> {
    try {
      await invoke('delete_credential', { key })
    } catch {
      localStorage.removeItem(`ava_cred_${key}`)
    }
  },
}

function getPlatform() {
  return { credentials }
}

export interface StoredAuth {
  type: 'oauth' | 'api-key'
  accessToken: string
  refreshToken?: string
  expiresAt?: number
  accountId?: string
}

/** Store auth credentials for a provider using core-v2 key format */
export async function setStoredAuth(provider: string, auth: StoredAuth): Promise<void> {
  const creds = getPlatform().credentials
  if (auth.type === 'oauth') {
    await creds.set(`ava:${provider}:oauth_token`, auth.accessToken)
    if (auth.accountId) {
      await creds.set(`ava:${provider}:account_id`, auth.accountId)
    }
  } else {
    await creds.set(`ava:${provider}:api_key`, auth.accessToken)
  }
}

/** Remove stored auth credentials for a provider */
export async function removeStoredAuth(provider: string): Promise<void> {
  const creds = getPlatform().credentials
  await creds.delete(`ava:${provider}:oauth_token`)
  await creds.delete(`ava:${provider}:account_id`)
  await creds.delete(`ava:${provider}:api_key`)
}
