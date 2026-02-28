import { afterEach, beforeEach, describe, expect, it } from 'vitest'
import { checkStoredOAuth, clearProviderCredentials } from './providers-tab-helpers'

function writeCredentials(payload: Record<string, unknown>) {
  localStorage.setItem('ava_credentials', JSON.stringify(payload))
}

function writeCoreAuth(providerId: string, payload: Record<string, unknown>) {
  localStorage.setItem(`ava_cred_auth-${providerId}`, JSON.stringify(payload))
}

describe('ProvidersTab helpers', () => {
  beforeEach(() => localStorage.clear())
  afterEach(() => localStorage.clear())

  it('checkStoredOAuth returns true for ava_credentials oauth-token', () => {
    writeCredentials({
      openai: { type: 'oauth-token' },
    })
    expect(checkStoredOAuth('openai')).toBe(true)
  })

  it('checkStoredOAuth falls back to legacy estela credentials', () => {
    localStorage.setItem('estela_credentials', JSON.stringify({ openai: { type: 'oauth-token' } }))
    expect(checkStoredOAuth('openai')).toBe(true)
  })

  it('checkStoredOAuth returns true for core-v2 oauth token key', () => {
    localStorage.setItem('ava_cred_ava:openai:oauth_token', 'some-token')
    expect(checkStoredOAuth('openai')).toBe(true)
  })

  it('checkStoredOAuth returns true for legacy core oauth auth', () => {
    writeCoreAuth('openai', { type: 'oauth' })
    expect(checkStoredOAuth('openai')).toBe(true)
  })

  it('checkStoredOAuth returns false when no oauth data', () => {
    writeCredentials({ openai: { type: 'api-key' } })
    expect(checkStoredOAuth('openai')).toBe(false)
  })

  it('clearProviderCredentials removes provider data and all key formats', () => {
    writeCredentials({
      openai: { type: 'oauth-token' },
      anthropic: { type: 'oauth-token' },
    })
    // Core-v2 keys
    localStorage.setItem('ava_cred_ava:openai:api_key', 'sk-openai')
    localStorage.setItem('ava_cred_ava:openai:oauth_token', 'oauth-tok')
    localStorage.setItem('ava_cred_ava:openai:account_id', 'acct-123')
    // Legacy keys
    localStorage.setItem('ava_cred_openai-api-key', 'sk-openai-legacy')
    localStorage.setItem('estela_cred_openai-api-key', 'legacy-openai')
    writeCoreAuth('openai', { type: 'oauth' })
    localStorage.setItem('estela_cred_auth-openai', JSON.stringify({ type: 'oauth' }))

    clearProviderCredentials('openai')

    const stored = JSON.parse(localStorage.getItem('ava_credentials') || '{}') as Record<
      string,
      unknown
    >
    expect(stored.openai).toBeUndefined()
    expect(stored.anthropic).toBeDefined()
    // Core-v2 keys cleared
    expect(localStorage.getItem('ava_cred_ava:openai:api_key')).toBeNull()
    expect(localStorage.getItem('ava_cred_ava:openai:oauth_token')).toBeNull()
    expect(localStorage.getItem('ava_cred_ava:openai:account_id')).toBeNull()
    // Legacy keys cleared
    expect(localStorage.getItem('ava_cred_openai-api-key')).toBeNull()
    expect(localStorage.getItem('ava_cred_auth-openai')).toBeNull()
    expect(localStorage.getItem('estela_cred_openai-api-key')).toBeNull()
    expect(localStorage.getItem('estela_cred_auth-openai')).toBeNull()
    expect(localStorage.getItem('estela_credentials')).toBe(localStorage.getItem('ava_credentials'))
  })
})
