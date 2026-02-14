import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'
import { checkStoredOAuth, clearProviderCredentials } from './providers-tab-helpers'

vi.mock('@ava/core', () => ({
  removeStoredAuth: vi.fn(),
}))

function writeCredentials(payload: Record<string, unknown>) {
  localStorage.setItem('estela_credentials', JSON.stringify(payload))
}

function writeCoreAuth(providerId: string, payload: Record<string, unknown>) {
  localStorage.setItem(`estela_cred_auth-${providerId}`, JSON.stringify(payload))
}

describe('ProvidersTab helpers', () => {
  beforeEach(() => localStorage.clear())
  afterEach(() => localStorage.clear())

  it('checkStoredOAuth returns true for estela_credentials oauth-token', () => {
    writeCredentials({
      openai: { type: 'oauth-token' },
    })
    expect(checkStoredOAuth('openai')).toBe(true)
  })

  it('checkStoredOAuth returns true for core oauth auth', () => {
    writeCoreAuth('openai', { type: 'oauth' })
    expect(checkStoredOAuth('openai')).toBe(true)
  })

  it('checkStoredOAuth returns false when no oauth data', () => {
    writeCredentials({ openai: { type: 'api-key' } })
    expect(checkStoredOAuth('openai')).toBe(false)
  })

  it('clearProviderCredentials removes provider data and core keys', () => {
    writeCredentials({
      openai: { type: 'oauth-token' },
      anthropic: { type: 'oauth-token' },
    })
    localStorage.setItem('estela_cred_openai-api-key', 'sk-openai')
    writeCoreAuth('openai', { type: 'oauth' })

    clearProviderCredentials('openai')

    const stored = JSON.parse(localStorage.getItem('estela_credentials') || '{}') as Record<
      string,
      unknown
    >
    expect(stored.openai).toBeUndefined()
    expect(stored.anthropic).toBeDefined()
    expect(localStorage.getItem('estela_cred_openai-api-key')).toBeNull()
    expect(localStorage.getItem('estela_cred_auth-openai')).toBeNull()
  })
})
