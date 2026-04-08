import { beforeEach, describe, expect, it } from 'vitest'
import { DEFAULT_SETTINGS } from './settings-defaults'
import { loadSettings, serializeSettings } from './settings-persistence'

describe('settings persistence', () => {
  beforeEach(() => {
    localStorage.clear()
  })

  it('does not serialize raw provider API keys into settings storage', () => {
    const settings = {
      ...DEFAULT_SETTINGS,
      providers: DEFAULT_SETTINGS.providers.map((provider) =>
        provider.id === 'openai' ? { ...provider, apiKey: 'sk-test-openai' } : provider
      ),
    }

    const serialized = serializeSettings(settings)
    const openai = (serialized.providers as Array<Record<string, unknown>>).find(
      (provider) => provider.id === 'openai'
    )

    expect(openai).toBeDefined()
    expect(openai).not.toHaveProperty('apiKey')
  })

  it('restores cached API keys for non-Tauri runtimes', () => {
    localStorage.setItem('ava_settings', JSON.stringify(serializeSettings(DEFAULT_SETTINGS)))
    localStorage.setItem('ava_cred_ava:openai:api_key', 'sk-cached-openai')

    const loaded = loadSettings()
    const openai = loaded.providers.find((provider) => provider.id === 'openai')

    expect(openai?.apiKey).toBe('sk-cached-openai')
  })
})
