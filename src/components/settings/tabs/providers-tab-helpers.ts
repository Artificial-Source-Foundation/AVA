export function clearProviderCredentials(providerId: string): void {
  // Clear from frontend credentials store
  try {
    const stored = localStorage.getItem('ava_credentials')
    if (stored) {
      const all = JSON.parse(stored) as Record<string, unknown>
      delete all[providerId]
      localStorage.setItem('ava_credentials', JSON.stringify(all))
    }
  } catch {
    // Ignore
  }
  // Clear stale ava_cred_ prefixed keys (API key + auth)
  localStorage.removeItem(`ava_cred_${providerId}-api-key`)
  localStorage.removeItem(`ava_cred_auth-${providerId}`)
}

export function checkStoredOAuth(providerId: string): boolean {
  try {
    const stored = localStorage.getItem('ava_credentials')
    if (stored) {
      const all = JSON.parse(stored) as Record<string, { type?: string }>
      if (all[providerId]?.type === 'oauth-token') return true
    }
    const coreAuth = localStorage.getItem(`ava_cred_auth-${providerId}`)
    if (coreAuth) {
      const parsed = JSON.parse(coreAuth) as { type?: string }
      if (parsed.type === 'oauth') return true
    }
  } catch {
    // Ignore parse errors
  }
  return false
}
