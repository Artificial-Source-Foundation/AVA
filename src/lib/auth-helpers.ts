/**
 * Auth Helpers
 * Bridges desktop OAuth/API-key auth into the Rust credential store.
 * Replaces setStoredAuth/removeStoredAuth from @ava/core.
 *
 * Falls back to legacy localStorage keys only when the native bridge is
 * unavailable (tests or non-Tauri runtime). Native write/delete failures are
 * surfaced so the UI cannot claim success while the backend is stale.
 */

import { invoke, isTauri } from '@tauri-apps/api/core'

export interface StoredAuth {
  type: 'oauth' | 'api-key'
  accessToken: string
  refreshToken?: string
  expiresAt?: number
  accountId?: string
}

function formatBridgeError(command: string, error: unknown): Error {
  const message = error instanceof Error ? error.message : String(error)
  return new Error(`[auth-bridge:${command}] ${message}`)
}

function writeStoredAuthToLocalStorage(provider: string, auth: StoredAuth): void {
  if (auth.type === 'oauth') {
    localStorage.setItem(`ava_cred_ava:${provider}:oauth_token`, auth.accessToken)
    if (auth.refreshToken) {
      localStorage.setItem(`ava_cred_ava:${provider}:oauth_refresh_token`, auth.refreshToken)
    }
    if (auth.expiresAt) {
      localStorage.setItem(`ava_cred_ava:${provider}:oauth_expires_at`, String(auth.expiresAt))
    }
    if (auth.accountId) {
      localStorage.setItem(`ava_cred_ava:${provider}:account_id`, auth.accountId)
    }
    return
  }

  localStorage.setItem(`ava_cred_ava:${provider}:api_key`, auth.accessToken)
}

function clearStoredAuthFromLocalStorage(provider: string): void {
  localStorage.removeItem(`ava_cred_ava:${provider}:oauth_token`)
  localStorage.removeItem(`ava_cred_ava:${provider}:oauth_refresh_token`)
  localStorage.removeItem(`ava_cred_ava:${provider}:oauth_expires_at`)
  localStorage.removeItem(`ava_cred_ava:${provider}:account_id`)
  localStorage.removeItem(`ava_cred_ava:${provider}:api_key`)
}

/** Store auth credentials for a provider using core-v2 key format */
export async function setStoredAuth(provider: string, auth: StoredAuth): Promise<void> {
  if (!isTauri()) {
    writeStoredAuthToLocalStorage(provider, auth)
    return
  }

  try {
    await invoke('store_provider_auth', { provider, auth })
  } catch (error) {
    throw formatBridgeError('store_provider_auth', error)
  }
}

/** Remove stored auth credentials for a provider */
export async function removeStoredAuth(provider: string): Promise<void> {
  if (!isTauri()) {
    clearStoredAuthFromLocalStorage(provider)
    return
  }

  try {
    await invoke('delete_provider_auth', { provider })
  } catch (error) {
    throw formatBridgeError('delete_provider_auth', error)
  }
}
