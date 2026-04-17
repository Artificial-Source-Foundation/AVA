import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'

import { DEFAULT_SETTINGS } from './settings-defaults'
import {
  getProviderCredentialInfo,
  refreshAllProviderModels,
  updateProvider,
} from './settings-mutators'
import { setSettingsRaw } from './settings-signal'

const { fetchModelsMock } = vi.hoisted(() => ({
  fetchModelsMock: vi.fn(async () => []),
}))

vi.mock('../../services/providers/model-fetcher', () => ({
  enrichWithCatalog: <T>(models: T[]) => models,
  fetchModels: fetchModelsMock,
}))

function buildSettings(openAiApiKey?: string) {
  return {
    ...DEFAULT_SETTINGS,
    providers: DEFAULT_SETTINGS.providers.map((provider) =>
      provider.id === 'openai'
        ? { ...provider, ...(openAiApiKey ? { apiKey: openAiApiKey } : {}) }
        : { ...provider }
    ),
  }
}

describe('settings-mutators provider credentials', () => {
  beforeEach(() => {
    localStorage.clear()
    fetchModelsMock.mockClear()
    setSettingsRaw(buildSettings())
  })

  afterEach(() => {
    localStorage.clear()
    setSettingsRaw(buildSettings())
  })

  it('prefers stored OAuth credentials over a stale saved API key for OAuth-capable providers', () => {
    setSettingsRaw(buildSettings('sk-stale-openai'))
    localStorage.setItem('ava_cred_ava:openai:oauth_token', 'oauth-token')

    expect(getProviderCredentialInfo('openai')).toEqual({
      value: 'oauth-token',
      type: 'oauth-token',
    })
  })

  it('prefers a startup-loaded configured API key over stale cached OAuth for OAuth-capable providers', () => {
    localStorage.setItem('ava_cred_ava:openai:api_key', 'sk-startup-openai')
    localStorage.setItem('ava_cred_ava:openai:oauth_token', 'oauth-token')
    setSettingsRaw(buildSettings('sk-startup-openai'))

    expect(getProviderCredentialInfo('openai')).toEqual({
      value: 'sk-startup-openai',
      type: 'api-key',
    })

    refreshAllProviderModels()

    expect(fetchModelsMock).toHaveBeenCalledWith('openai', {
      apiKey: 'sk-startup-openai',
      authType: 'api-key',
      baseUrl: undefined,
    })
  })

  it('clears stored OAuth when an API key is explicitly saved for an OAuth-capable provider', () => {
    localStorage.setItem(
      'ava_credentials',
      JSON.stringify({ openai: { type: 'oauth-token', value: 'oauth-token' } })
    )
    localStorage.setItem(
      'estela_credentials',
      JSON.stringify({ openai: { type: 'oauth-token', value: 'oauth-token' } })
    )
    localStorage.setItem('ava_cred_ava:openai:oauth_token', 'oauth-token')
    localStorage.setItem('ava_cred_ava:openai:account_id', 'acct-1')

    updateProvider('openai', { apiKey: 'sk-manual-openai', status: 'connected', enabled: true })

    expect(getProviderCredentialInfo('openai')).toEqual({
      value: 'sk-manual-openai',
      type: 'api-key',
    })
    expect(localStorage.getItem('ava_cred_ava:openai:oauth_token')).toBeNull()
    expect(localStorage.getItem('ava_cred_ava:openai:account_id')).toBeNull()
    expect(localStorage.getItem('ava_credentials')).toBe('{}')
    expect(localStorage.getItem('estela_credentials')).toBe('{}')
  })

  it('uses legacy core auth records as OAuth credentials when present', () => {
    localStorage.setItem(
      'ava_cred_auth-openai',
      JSON.stringify({ type: 'oauth', accessToken: 'legacy-auth-token' })
    )

    expect(getProviderCredentialInfo('openai')).toEqual({
      value: 'legacy-auth-token',
      type: 'oauth-token',
    })
  })
})
