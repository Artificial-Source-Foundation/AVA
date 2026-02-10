export function clearProviderCredentials(providerId: string): void {
  // Clear from frontend credentials store
  try {
    const stored = localStorage.getItem('estela_credentials')
    if (stored) {
      const all = JSON.parse(stored) as Record<string, unknown>
      delete all[providerId]
      localStorage.setItem('estela_credentials', JSON.stringify(all))
    }
  } catch {
    // Ignore
  }
  // Clear stale estela_cred_ prefixed keys (API key + auth)
  localStorage.removeItem(`estela_cred_${providerId}-api-key`)
  localStorage.removeItem(`estela_cred_auth-${providerId}`)
}

export function checkStoredOAuth(providerId: string): boolean {
  try {
    const stored = localStorage.getItem('estela_credentials')
    if (stored) {
      const all = JSON.parse(stored) as Record<string, { type?: string }>
      if (all[providerId]?.type === 'oauth-token') return true
    }
    const coreAuth = localStorage.getItem(`estela_cred_auth-${providerId}`)
    if (coreAuth) {
      const parsed = JSON.parse(coreAuth) as { type?: string }
      if (parsed.type === 'oauth') return true
    }
  } catch {
    // Ignore parse errors
  }
  return false
}
