const AVA_CREDENTIALS_KEY = 'ava_credentials'
const AVA_CREDENTIAL_PREFIX = 'ava_cred_'
const LEGACY_CREDENTIAL_PREFIX = 'estela_cred_'

function readCredentialsMap(): Record<string, unknown> {
  const raw =
    localStorage.getItem(AVA_CREDENTIALS_KEY) || localStorage.getItem('estela_credentials')
  if (!raw) {
    return {}
  }

  try {
    return JSON.parse(raw) as Record<string, unknown>
  } catch {
    return {}
  }
}

function writeCredentialsMap(map: Record<string, unknown>): void {
  const serialized = JSON.stringify(map)
  localStorage.setItem(AVA_CREDENTIALS_KEY, serialized)
  localStorage.setItem('estela_credentials', serialized)
}

export function clearProviderCredentials(providerId: string): void {
  // Clear from frontend credentials store
  const all = readCredentialsMap()
  delete all[providerId]
  writeCredentialsMap(all)

  // Clear core-v2 credential keys (ava:{provider}:* format, with ava_cred_ prefix from TauriCredentialStore)
  localStorage.removeItem(`${AVA_CREDENTIAL_PREFIX}ava:${providerId}:api_key`)
  localStorage.removeItem(`${AVA_CREDENTIAL_PREFIX}ava:${providerId}:oauth_token`)
  localStorage.removeItem(`${AVA_CREDENTIAL_PREFIX}ava:${providerId}:account_id`)

  // Clear legacy keys (old format cleanup)
  localStorage.removeItem(`${AVA_CREDENTIAL_PREFIX}${providerId}-api-key`)
  localStorage.removeItem(`${AVA_CREDENTIAL_PREFIX}auth-${providerId}`)
  localStorage.removeItem(`${LEGACY_CREDENTIAL_PREFIX}${providerId}-api-key`)
  localStorage.removeItem(`${LEGACY_CREDENTIAL_PREFIX}auth-${providerId}`)
}

export function checkStoredOAuth(providerId: string): boolean {
  try {
    const all = readCredentialsMap() as Record<string, { type?: string }>
    if (all[providerId]?.type === 'oauth-token') {
      return true
    }

    // Check core-v2 key format (TauriCredentialStore stores at ava_cred_ + key)
    const oauthToken = localStorage.getItem(`${AVA_CREDENTIAL_PREFIX}ava:${providerId}:oauth_token`)
    if (oauthToken) {
      return true
    }

    // Legacy key format
    const coreAuth =
      localStorage.getItem(`${AVA_CREDENTIAL_PREFIX}auth-${providerId}`) ||
      localStorage.getItem(`${LEGACY_CREDENTIAL_PREFIX}auth-${providerId}`)
    if (coreAuth) {
      const parsed = JSON.parse(coreAuth) as { type?: string }
      if (parsed.type === 'oauth') {
        return true
      }
    }
  } catch {
    // Ignore parse errors
  }
  return false
}
