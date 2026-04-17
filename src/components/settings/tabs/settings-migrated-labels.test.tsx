import { render } from 'solid-js/web'
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'

const setSearchMock = vi.fn()

vi.mock('../../../services/context-compaction', () => ({
  getCompactionModelOptions: () => [{ value: 'current', label: 'Current chat model' }],
}))

vi.mock('../../../stores/settings', () => ({
  useSettings: () => ({
    settings: () => ({
      generation: {
        maxTokens: 4096,
        temperature: 0.7,
        topP: 1,
        weakModel: 'gpt-4.1-mini',
        customInstructions: 'Be concise.',
        autoCompact: true,
        compactionThreshold: 80,
        compactionModel: 'current',
      },
      agentLimits: {
        agentMaxTurns: 10,
        agentMaxTimeMinutes: 5,
      },
    }),
    updateSettings: vi.fn(),
    updateGeneration: vi.fn(),
    updateAgentLimits: vi.fn(),
  }),
}))

vi.mock('./llm/llm-config', () => ({
  MODEL_PAIRS: [],
  WEAK_MODEL_OPTIONS: [{ value: 'gpt-4.1-mini', label: 'GPT-4.1 Mini' }],
}))

vi.mock('./model-aliases-section', () => ({
  ModelAliasesSection: () => null,
}))

vi.mock('../../ui/Toggle', () => ({
  Toggle: () => <button type="button">toggle</button>,
}))

vi.mock('../../../services/plugin-loader', () => ({
  watchPluginDirectory: () => () => {},
}))

vi.mock('../../../services/rust-bridge', () => ({
  rustBackend: {
    listPluginMounts: vi.fn().mockResolvedValue([]),
  },
}))

vi.mock('../../../stores/plugins', () => ({
  usePlugins: () => ({
    plugins: [],
    search: () => '',
    setSearch: setSearchMock,
    catalogStatus: () => 'idle',
    lastCatalogSyncAt: () => null,
    catalogError: () => null,
    filteredPlugins: () => [],
    pluginState: () => ({}),
    categories: () => ['all'],
    showInstalledOnly: () => false,
    setShowInstalledOnly: vi.fn(),
    categoryFilter: () => 'all',
    setCategoryFilter: vi.fn(),
    clearError: vi.fn(),
    install: vi.fn().mockResolvedValue(undefined),
    refresh: vi.fn().mockResolvedValue(undefined),
  }),
}))

vi.mock('../../../stores/plugins-catalog', () => ({
  sortPlugins: (plugins: unknown[]) => plugins,
}))

vi.mock('../../plugins', () => ({
  PluginDetailPanel: () => null,
}))

vi.mock('../../plugins/PluginWizard', () => ({
  PluginWizard: () => null,
}))

vi.mock('../../plugins/PublishDialog', () => ({
  PublishDialog: () => null,
}))

vi.mock('./plugins-tab', () => ({
  formatSyncTime: () => 'Never',
  GitInstallDialog: () => null,
  LinkLocalDialog: () => null,
  PermissionConfirmDialog: () => null,
  PluginDevMode: () => null,
}))

import { LLMTab } from './LLMTab'
import { PluginsTab } from './PluginsTab'

describe('migrated settings labeling', () => {
  let container: HTMLDivElement
  let dispose: (() => void) | undefined

  beforeEach(() => {
    setSearchMock.mockClear()
    container = document.createElement('div')
    document.body.appendChild(container)
  })

  afterEach(() => {
    dispose?.()
    dispose = undefined
    document.body.innerHTML = ''
  })

  it('exposes accessible names for migrated LLM selects and textarea', () => {
    dispose = render(() => <LLMTab />, container)

    expect(container.querySelector('select[aria-label="Secondary model"]')).toBeTruthy()
    expect(container.querySelector('textarea[aria-label="Custom instructions"]')).toBeTruthy()
    expect(container.querySelector('select[aria-label="Compaction model"]')).toBeTruthy()
  })

  it('exposes an accessible name for the plugins search input', () => {
    dispose = render(() => <PluginsTab />, container)

    expect(container.querySelector('input[aria-label="Search plugins"]')).toBeTruthy()
  })
})
