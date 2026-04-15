import { render } from 'solid-js/web'
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'
import { DEFAULT_SETTINGS } from '../../stores/settings/settings-defaults'
import type { AppSettings } from '../../stores/settings/settings-types'

vi.mock('./settings-general-section', () => ({
  GeneralSection: () => null,
}))

vi.mock('./tabs/AdvancedTab', () => ({
  AdvancedTab: () => null,
}))

vi.mock('./tabs/AppearanceTab', () => ({
  AppearanceTab: () => null,
}))

vi.mock('./tabs/LLMTab', () => ({
  LLMTab: () => null,
}))

vi.mock('./tabs/MCPServersTab', () => ({
  MCPServersTab: () => null,
}))

vi.mock('./tabs/PermissionsAndTrustTab', () => ({
  PermissionsAndTrustTab: () => null,
}))

vi.mock('./tabs/PluginsTab', () => ({
  PluginsTab: () => null,
}))

vi.mock('./tabs/SkillsSettingsTab', () => ({
  SkillsSettingsTab: () => null,
}))

vi.mock('./tabs/UsageTab', () => ({
  UsageTab: () => null,
}))

vi.mock('./tabs/providers/providers-tab', () => ({
  ProvidersTab: (props: {
    onOAuthConnected?: (providerId: string) => void
    onClearApiKey?: (providerId: string) => void
  }) => (
    <div>
      <button type="button" onClick={() => props.onOAuthConnected?.('openai')}>
        Mock OAuth success
      </button>
      <button type="button" onClick={() => props.onClearApiKey?.('openai')}>
        Mock Clear API Key
      </button>
    </div>
  ),
}))

import { SettingsModalContent } from './settings-modal-content'

function buildSettings(): AppSettings {
  return {
    ...DEFAULT_SETTINGS,
    providers: DEFAULT_SETTINGS.providers.map((provider) =>
      provider.id === 'openai'
        ? {
            ...provider,
            apiKey: 'sk-stale',
            status: 'disconnected',
            enabled: false,
          }
        : provider
    ),
  }
}

describe('SettingsModalContent provider OAuth sync', () => {
  let container: HTMLDivElement
  let dispose: (() => void) | undefined

  beforeEach(() => {
    container = document.createElement('div')
    document.body.appendChild(container)
  })

  afterEach(() => {
    dispose?.()
    dispose = undefined
    document.body.innerHTML = ''
  })

  it('updates provider settings after provider OAuth succeeds', () => {
    const onUpdateProvider = vi.fn()

    dispose = render(
      () => (
        <SettingsModalContent
          activeTab={() => 'providers'}
          onSelectTab={vi.fn()}
          settings={() => buildSettings()}
          keybindings={() => []}
          mcpServers={() => []}
          onEditKeybinding={vi.fn()}
          onResetKeybinding={vi.fn()}
          onResetAllKeybindings={vi.fn()}
          onUpdateProvider={onUpdateProvider}
          onUpdateAgent={vi.fn()}
          onTestProvider={vi.fn().mockResolvedValue(undefined)}
          onRemoveMcpServer={vi.fn()}
          onAddMcpServer={vi.fn()}
        />
      ),
      container
    )

    const button = container.querySelector('button')
    if (!(button instanceof HTMLButtonElement)) {
      throw new Error('Mock OAuth success button was not rendered')
    }
    button.click()

    expect(onUpdateProvider).toHaveBeenCalledWith('openai', {
      apiKey: undefined,
      status: 'connected',
      enabled: true,
      error: undefined,
    })
  })

  it('clears enabled state when provider credentials are cleared', () => {
    const onUpdateProvider = vi.fn()

    dispose = render(
      () => (
        <SettingsModalContent
          activeTab={() => 'providers'}
          onSelectTab={vi.fn()}
          settings={() => buildSettings()}
          keybindings={() => []}
          mcpServers={() => []}
          onEditKeybinding={vi.fn()}
          onResetKeybinding={vi.fn()}
          onResetAllKeybindings={vi.fn()}
          onUpdateProvider={onUpdateProvider}
          onUpdateAgent={vi.fn()}
          onTestProvider={vi.fn().mockResolvedValue(undefined)}
          onRemoveMcpServer={vi.fn()}
          onAddMcpServer={vi.fn()}
        />
      ),
      container
    )

    const clearButton = Array.from(container.querySelectorAll('button')).find((b) =>
      b.textContent?.includes('Clear API Key')
    )
    if (!(clearButton instanceof HTMLButtonElement)) {
      throw new Error('Mock Clear API Key button was not rendered')
    }
    clearButton.click()

    // Regression test: clearing credentials should also clear enabled state
    expect(onUpdateProvider).toHaveBeenCalledWith('openai', {
      apiKey: undefined,
      status: 'disconnected',
      enabled: false,
    })
  })
})
