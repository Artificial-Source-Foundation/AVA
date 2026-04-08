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

    expect(toolsGroup?.tabs.map((tab) => tab.id)).toEqual(['skills'])
    expect(advancedGroup?.tabs.map((tab) => tab.id)).toContain('plugins')
    expect(advancedGroup?.tabs.map((tab) => tab.id)).toContain('mcp')
  })
})
