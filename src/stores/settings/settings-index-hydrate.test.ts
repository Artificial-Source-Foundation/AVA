import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'

import { STORAGE_KEYS } from '../../config/constants'
import type { LLMProviderConfig } from '../../config/defaults/provider-defaults'
import { DEFAULT_SETTINGS } from './settings-defaults'
import { setSettingsRaw, settings } from './settings-signal'

const {
  applyAppearanceToDOMMock,
  hydrateFromFSMock,
  importSettingsFromFileMock,
  loadSharedSettingsFromCoreMock,
} = vi.hoisted(() => ({
  applyAppearanceToDOMMock: vi.fn(),
  hydrateFromFSMock: vi.fn(),
  importSettingsFromFileMock: vi.fn(),
  loadSharedSettingsFromCoreMock: vi.fn(),
}))

vi.mock('./settings-appearance', async () => {
  const actual =
    await vi.importActual<typeof import('./settings-appearance')>('./settings-appearance')
  return {
    ...actual,
    applyAppearanceToDOM: applyAppearanceToDOMMock,
  }
})

vi.mock('./settings-io', async () => {
  const actual = await vi.importActual<typeof import('./settings-io')>('./settings-io')
  return {
    ...actual,
    hydrateFromFS: hydrateFromFSMock,
    importSettingsFromFile: importSettingsFromFileMock,
  }
})

vi.mock('./settings-persistence', async () => {
  const actual =
    await vi.importActual<typeof import('./settings-persistence')>('./settings-persistence')
  return {
    ...actual,
    loadSharedSettingsFromCore: loadSharedSettingsFromCoreMock,
  }
})

import { hydrateSettingsFromFS, useSettings } from './index'

function cloneDefaultSettings() {
  return {
    ...DEFAULT_SETTINGS,
    providers: DEFAULT_SETTINGS.providers.map((provider) => ({ ...provider })),
    agents: DEFAULT_SETTINGS.agents.map((agent) => ({ ...agent })),
    autoApprovedTools: [...DEFAULT_SETTINGS.autoApprovedTools],
    mcpServers: DEFAULT_SETTINGS.mcpServers.map((server) => ({ ...server })),
  }
}

describe('settings index hydration', () => {
  beforeEach(() => {
    vi.useFakeTimers()
    localStorage.clear()
    applyAppearanceToDOMMock.mockReset()
    hydrateFromFSMock.mockReset()
    importSettingsFromFileMock.mockReset()
    loadSharedSettingsFromCoreMock.mockReset()
    setSettingsRaw(cloneDefaultSettings())
  })

  afterEach(() => {
    setSettingsRaw(cloneDefaultSettings())
    localStorage.clear()
    vi.useRealTimers()
  })

  it('hydrates from FS, deep-merges shared patch, applies appearance, and persists merged result', async () => {
    hydrateFromFSMock.mockImplementation(async (current, onHydrated) => {
      onHydrated({
        ...current,
        mode: 'light',
        appearance: { ...current.appearance, fontSize: 'large' },
        generation: {
          ...current.generation,
          maxTokens: 1234,
          customInstructions: 'from-fs',
        },
        git: {
          ...current.git,
          autoCommit: true,
          commitPrefix: 'fs-prefix',
        },
      })
    })

    loadSharedSettingsFromCoreMock.mockImplementation(
      async (currentProviders?: LLMProviderConfig[]) => ({
        generation: { temperature: 0.91 },
        git: { enabled: false },
        providers:
          currentProviders?.map((provider: LLMProviderConfig, index: number) =>
            index === 0 ? { ...provider, defaultModel: 'shared-model' } : provider
          ) ?? [],
      })
    )

    await hydrateSettingsFromFS()

    expect(settings().mode).toBe('light')
    expect(settings().generation.maxTokens).toBe(1234)
    expect(settings().generation.temperature).toBe(0.91)
    expect(settings().generation.customInstructions).toBe('from-fs')
    expect(settings().git.enabled).toBe(false)
    expect(settings().git.autoCommit).toBe(true)
    expect(settings().git.commitPrefix).toBe('fs-prefix')
    expect(settings().providers[0]?.defaultModel).toBe('shared-model')

    expect(applyAppearanceToDOMMock).toHaveBeenCalledTimes(1)

    vi.advanceTimersByTime(200)

    const persistedRaw = localStorage.getItem(STORAGE_KEYS.SETTINGS)
    expect(persistedRaw).not.toBeNull()

    const persisted = JSON.parse(persistedRaw as string)
    expect(persisted.mode).toBe('light')
    expect(persisted.generation.maxTokens).toBe(1234)
    expect(persisted.generation.temperature).toBe(0.91)
    expect(persisted.generation.customInstructions).toBe('from-fs')
    expect(persisted.git.enabled).toBe(false)
    expect(persisted.git.autoCommit).toBe(true)
    expect(persisted.git.commitPrefix).toBe('fs-prefix')
    expect(persisted.providers[0]?.defaultModel).toBe('shared-model')
  })

  it('imports settings via useSettings.importSettings and persists through commitSettings', async () => {
    const imported = {
      ...cloneDefaultSettings(),
      mode: 'light' as const,
      appearance: {
        ...cloneDefaultSettings().appearance,
        accentColor: 'emerald' as const,
      },
      generation: {
        ...cloneDefaultSettings().generation,
        temperature: 0.37,
      },
    }

    importSettingsFromFileMock.mockImplementation(
      async (onImported: (merged: typeof imported) => void) => {
        await Promise.resolve()
        onImported(imported)
      }
    )

    const { importSettings } = useSettings()
    await importSettings()

    expect(settings().mode).toBe('light')
    expect(settings().appearance.accentColor).toBe('emerald')
    expect(settings().generation.temperature).toBe(0.37)

    expect(applyAppearanceToDOMMock).toHaveBeenCalledTimes(1)

    vi.advanceTimersByTime(200)

    const persistedRaw = localStorage.getItem(STORAGE_KEYS.SETTINGS)
    expect(persistedRaw).not.toBeNull()

    const persisted = JSON.parse(persistedRaw as string)
    expect(persisted.mode).toBe('light')
    expect(persisted.appearance.accentColor).toBe('emerald')
    expect(persisted.generation.temperature).toBe(0.37)
  })
})
