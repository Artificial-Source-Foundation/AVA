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

import { getPlatform } from '@ava/core-v2/platform'

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
