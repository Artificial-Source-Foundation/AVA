import { describe, expect, it } from 'vitest'
import settingsModalSource from '../SettingsModal.tsx?raw'

describe('plugins settings smoke', () => {
  it('keeps plugin tab wiring in settings modal', () => {
    expect(settingsModalSource).toContain("activeTab() === 'plugins'")
    expect(settingsModalSource).toContain("import { PluginsTab } from './tabs/PluginsTab'")
    expect(settingsModalSource).toContain('<PluginsTab />')
  })
})
