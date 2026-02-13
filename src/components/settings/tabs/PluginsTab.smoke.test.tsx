import { describe, expect, it } from 'vitest'
import settingsModalSource from '../SettingsModal.tsx?raw'

describe('plugins settings smoke', () => {
  it('keeps plugin tab wiring and placeholder copy in settings modal', () => {
    expect(settingsModalSource).toContain("activeTab() === 'plugins'")
    expect(settingsModalSource).toContain('Obsidian-style plugin ecosystem coming in Phase 2.')
    expect(settingsModalSource).toContain('Built-in Skills')
  })
})
