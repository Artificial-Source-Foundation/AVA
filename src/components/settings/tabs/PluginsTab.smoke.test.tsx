import { describe, expect, it } from 'vitest'
import settingsModalSource from '../SettingsModal.tsx?raw'

describe('plugins settings smoke', () => {
  it('keeps plugin tab wiring and settings-only plugin manager', () => {
    expect(settingsModalSource).toContain("activeTab() === 'plugins'")
    expect(settingsModalSource).toContain('<PluginsTab />')
    expect(settingsModalSource).not.toContain('Obsidian-style plugin ecosystem coming in Phase 2.')
  })
})
