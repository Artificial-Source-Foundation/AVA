import { describe, expect, it } from 'vitest'
import { settingsSearchIndex, tabGroups } from './settings-modal-config'

describe('settingsSearchIndex', () => {
  it('covers every configured settings tab', () => {
    const indexedTabs = new Set(settingsSearchIndex.map((entry) => entry.tab))
    const allTabs = tabGroups.flatMap((group) => group.tabs.map((tab) => tab.id))

    for (const tab of allTabs) {
      expect(indexedTabs.has(tab), `Missing search entries for tab: ${tab}`).toBe(true)
    }
  })

  it('keeps plugin and MCP surfaces under Advanced', () => {
    const toolsGroup = tabGroups.find((group) => group.label === 'Tools')
    const advancedGroup = tabGroups.find((group) => group.label === 'Advanced')

    expect(toolsGroup?.tabs.map((tab) => tab.id)).toEqual(['agents', 'skills'])
    expect(advancedGroup?.tabs.map((tab) => tab.id)).toContain('plugins')
    expect(advancedGroup?.tabs.map((tab) => tab.id)).toContain('mcp')
  })

  it('drops stale search entries that no longer map to the current UI', () => {
    const labels = new Set(settingsSearchIndex.map((entry) => entry.label))

    expect(labels.has('Show model in title bar')).toBe(false)
    expect(labels.has('Auto-fix lint errors')).toBe(false)
    expect(labels.has('Watch for AI comments')).toBe(false)
    expect(labels.has('Clipboard watcher')).toBe(false)
    expect(labels.has('Clear all data')).toBe(false)
    expect(labels.has('Editor model')).toBe(false)
    expect(labels.has('Always-approved tools')).toBe(false)
    expect(labels.has('Allowed directories')).toBe(false)
    expect(labels.has('Denied directories')).toBe(false)
    expect(labels.has('File logs')).toBe(false)

    expect(labels.has('Show memory panel')).toBe(true)
    expect(labels.has('Agents')).toBe(true)
    expect(labels.has('Reopen onboarding guide')).toBe(true)
    expect(labels.has('Active Skills')).toBe(true)
    expect(labels.has('Test connection')).toBe(true)
    expect(labels.has('OAuth sign-in')).toBe(true)
    expect(labels.has('Backend log file')).toBe(true)
  })

  it('keeps agents reachable and provider auth searchable', () => {
    const toolsGroup = tabGroups.find((group) => group.label === 'Tools')
    const providersTab = tabGroups
      .flatMap((group) => group.tabs)
      .find((tab) => tab.id === 'providers')

    expect(toolsGroup?.tabs.some((tab) => tab.id === 'agents')).toBe(true)
    expect(providersTab?.keywords).toEqual(expect.arrayContaining(['oauth', 'auth', 'credentials']))
  })
})
