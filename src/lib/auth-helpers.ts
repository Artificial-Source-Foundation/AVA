/**
 * Auth Helpers
 * Thin wrappers around getPlatform().credentials for OAuth storage.
 * Replaces setStoredAuth/removeStoredAuth from @ava/core.
 */

import { getPlatform } from '@ava/core-v2/platform'

const AUTH_KEY_PREFIX = 'auth-'

export interface StoredAuth {
  type: 'oauth' | 'api-key'
  accessToken: string
  refreshToken?: string
  expiresAt?: number
  accountId?: string
}

/** Store auth credentials for a provider */
export async function setStoredAuth(provider: string, auth: StoredAuth): Promise<void> {
  await getPlatform().credentials.set(`${AUTH_KEY_PREFIX}${provider}`, JSON.stringify(auth))
}

/** Remove stored auth credentials for a provider */
export async function removeStoredAuth(provider: string): Promise<void> {
  await getPlatform().credentials.delete(`${AUTH_KEY_PREFIX}${provider}`)
}
