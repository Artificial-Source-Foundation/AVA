import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'

import { STORAGE_KEYS } from '../../config/constants'
import { useSettings } from './index'
import { DEFAULT_SETTINGS } from './settings-defaults'
import { commitSettings, setSettingsRaw, settings } from './settings-signal'

function cloneDefaultSettings() {
  return {
    ...DEFAULT_SETTINGS,
    providers: DEFAULT_SETTINGS.providers.map((provider) => ({ ...provider })),
    agents: DEFAULT_SETTINGS.agents.map((agent) => ({ ...agent })),
    autoApprovedTools: [...DEFAULT_SETTINGS.autoApprovedTools],
    mcpServers: DEFAULT_SETTINGS.mcpServers.map((server) => ({ ...server })),
  }
}

describe('settings-signal commit helper', () => {
  beforeEach(() => {
    vi.useFakeTimers()
    localStorage.clear()
    setSettingsRaw(cloneDefaultSettings())
  })

  afterEach(() => {
    setSettingsRaw(cloneDefaultSettings())
    localStorage.clear()
    vi.useRealTimers()
  })

  it('persists when commitSettings updater returns a new settings object', () => {
    commitSettings((prev) => ({ ...prev, mode: 'light' }))

    expect(settings().mode).toBe('light')
    expect(localStorage.getItem(STORAGE_KEYS.SETTINGS)).toBeNull()

    vi.advanceTimersByTime(200)

    const saved = localStorage.getItem(STORAGE_KEYS.SETTINGS)
    expect(saved).not.toBeNull()
    expect(JSON.parse(saved as string).mode).toBe('light')
  })

  it('does not persist when commitSettings updater returns the previous object identity', () => {
    commitSettings((prev) => prev)

    vi.advanceTimersByTime(200)

    expect(localStorage.getItem(STORAGE_KEYS.SETTINGS)).toBeNull()
  })

  it('resetSettings persists defaults via the index-level commit path', () => {
    setSettingsRaw({ ...cloneDefaultSettings(), mode: 'light' })

    const { resetSettings } = useSettings()
    resetSettings()

    expect(settings().mode).toBe(DEFAULT_SETTINGS.mode)

    vi.advanceTimersByTime(200)

    const saved = localStorage.getItem(STORAGE_KEYS.SETTINGS)
    expect(saved).not.toBeNull()
    expect(JSON.parse(saved as string).mode).toBe(DEFAULT_SETTINGS.mode)
  })
})
