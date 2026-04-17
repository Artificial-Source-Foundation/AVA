import { createSignal } from 'solid-js'
import { render } from 'solid-js/web'
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'
import { DEFAULT_SETTINGS } from '../../stores/settings/settings-defaults'
import type { AppSettings } from '../../stores/settings/settings-types'
import type { McpServerInfo } from '../../types/rust-ipc'

const listMcpServersMock = vi.fn<() => Promise<McpServerInfo[]>>()
const reloadMcpServersMock = vi.fn<() => Promise<{ serverCount: number; toolCount: number }>>()
const enableMcpServerMock = vi.fn<(name: string) => Promise<void>>()
const disableMcpServerMock = vi.fn<(name: string) => Promise<void>>()
const fetchModelsMock = vi.fn()
const notificationErrorMock = vi.fn()
const notificationSuccessMock = vi.fn()
const closeSettingsMock = vi.fn()
const updateProviderMock = vi.fn()
const updateAgentMock = vi.fn()
const addMcpServerMock = vi.fn()
const removeMcpServerMock = vi.fn()
const updateShortcutMock = vi.fn()
const resetShortcutMock = vi.fn()
const resetAllShortcutsMock = vi.fn()

const [mockSettingsOpen] = createSignal(true)
const [mockSettings, setMockSettings] = createSignal<AppSettings>(DEFAULT_SETTINGS)
const [mockShortcuts] = createSignal([])

vi.mock('../../contexts/notification', () => ({
  useNotification: () => ({
    toast: vi.fn(),
    info: vi.fn(),
    success: notificationSuccessMock,
    warning: vi.fn(),
    error: notificationErrorMock,
    dismiss: vi.fn(),
    dismissAll: vi.fn(),
  }),
}))

vi.mock('../../services/rust-bridge', () => ({
  rustBackend: {
    listMcpServers: () => listMcpServersMock(),
    reloadMcpServers: () => reloadMcpServersMock(),
    enableMcpServer: (name: string) => enableMcpServerMock(name),
    disableMcpServer: (name: string) => disableMcpServerMock(name),
  },
}))

vi.mock('../../services/providers/model-fetcher', () => ({
  enrichWithCatalog: (_provider: string, models: unknown[]) => models,
  fetchModels: (...args: unknown[]) => fetchModelsMock(...args),
}))

vi.mock('../../stores/layout', () => ({
  useLayout: () => ({
    settingsOpen: mockSettingsOpen,
    closeSettings: closeSettingsMock,
  }),
}))

vi.mock('../../stores/settings', () => ({
  useSettings: () => ({
    settings: mockSettings,
    updateProvider: updateProviderMock,
    updateAgent: updateAgentMock,
    addMcpServer: addMcpServerMock,
    removeMcpServer: removeMcpServerMock,
  }),
}))

vi.mock('../../stores/shortcuts', () => ({
  useShortcuts: () => ({
    shortcuts: mockShortcuts,
    updateShortcut: updateShortcutMock,
    resetShortcut: resetShortcutMock,
    resetAll: resetAllShortcutsMock,
  }),
}))

vi.mock('../dialogs/AddMCPServerDialog', () => ({
  AddMCPServerDialog: (props: {
    open: boolean
    onClose: () => void
    onSave: (config: {
      name: string
      type: 'stdio' | 'sse' | 'http'
      command?: string
      args?: string[]
      url?: string
    }) => void
  }) => (
    <>
      <button
        type="button"
        aria-label="Mock save MCP server"
        data-open={props.open ? 'true' : 'false'}
        onClick={() => {
          props.onSave({
            name: 'added-local',
            type: 'stdio',
            command: 'added-server',
          })
          props.onClose()
        }}
      >
        Save MCP Server
      </button>
      <button
        type="button"
        aria-label="Mock save live-server MCP config"
        onClick={() => {
          props.onSave({
            name: 'live-server',
            type: 'stdio',
            command: 'live-server-local',
          })
          props.onClose()
        }}
      >
        Save matching MCP config
      </button>
    </>
  ),
}))

vi.mock('./settings-keybinding-edit-modal', () => ({
  KeybindingEditModal: () => null,
}))

vi.mock('./settings-modal-header', () => ({
  SettingsModalHeader: () => <div>Settings</div>,
}))

vi.mock('./settings-modal-sidebar', () => ({
  SettingsModalSidebar: () => null,
}))

vi.mock('./settings-modal-content', async () => {
  const { Show } = await import('solid-js')
  const { MCPServersTab } = await import('./tabs/MCPServersTab')

  return {
    SettingsModalContent: (props: {
      activeTab: () => string
      mcpServers: () => import('./tabs/MCPServersTab').MCPServer[]
      onTestProvider: (id: string) => Promise<void>
      onRemoveMcpServer: (id: string) => void
      onAddMcpServer: () => void
      onRefreshMcpServers?: () => void
      onToggleMcpServer?: (name: string, enabled: boolean) => void
      isMcpLoading?: () => boolean
    }) => (
      <>
        <Show when={props.activeTab() === 'providers'}>
          <button
            type="button"
            aria-label="Mock test OpenAI provider"
            onClick={() => void props.onTestProvider('openai')}
          >
            Test OpenAI provider
          </button>
        </Show>
        <Show when={props.activeTab() === 'mcp'}>
          <button
            type="button"
            aria-label="Mock remove fallback-local MCP server"
            onClick={() => props.onRemoveMcpServer('fallback-local')}
          >
            Remove local MCP server
          </button>
          <MCPServersTab
            servers={props.mcpServers()}
            isLoading={props.isMcpLoading?.()}
            onRemove={props.onRemoveMcpServer}
            onAdd={props.onAddMcpServer}
            onRefresh={props.onRefreshMcpServers}
            onToggle={props.onToggleMcpServer}
          />
        </Show>
      </>
    ),
  }
})

import { SettingsModal } from './SettingsModal'

function buildSettings(): AppSettings {
  return {
    ...DEFAULT_SETTINGS,
    mcpServers: [
      {
        name: 'fallback-local',
        type: 'stdio',
        command: 'fallback-server',
      },
    ],
  }
}

function buildMcpServer(name: string, command: string): AppSettings['mcpServers'][number] {
  return {
    name,
    type: 'stdio',
    command,
  }
}

function liveServer(overrides: Partial<McpServerInfo> = {}): McpServerInfo {
  return {
    name: 'live-server',
    toolCount: 2,
    scope: 'Local',
    enabled: true,
    canToggle: true,
    status: 'connected',
    ...overrides,
  }
}

function deferred<T>() {
  let resolve!: (value: T | PromiseLike<T>) => void
  let reject!: (reason?: unknown) => void
  const promise = new Promise<T>((res, rej) => {
    resolve = res
    reject = rej
  })
  return { promise, resolve, reject }
}

function click(element: Element): void {
  element.dispatchEvent(new MouseEvent('click', { bubbles: true, cancelable: true }))
}

function getButtonByLabel(label: string): HTMLButtonElement {
  const button = Array.from(document.querySelectorAll('button')).find(
    (candidate) => candidate.getAttribute('aria-label') === label
  )
  if (!(button instanceof HTMLButtonElement)) {
    throw new Error(`Could not find button with aria-label: ${label}`)
  }
  return button
}

function queryButtonByLabel(label: string): HTMLButtonElement | null {
  const button = Array.from(document.querySelectorAll('button')).find(
    (candidate) => candidate.getAttribute('aria-label') === label
  )
  return button instanceof HTMLButtonElement ? button : null
}

async function flush(): Promise<void> {
  await Promise.resolve()
  await Promise.resolve()
  await new Promise((resolve) => setTimeout(resolve, 0))
}

async function openMcpTab(): Promise<void> {
  await flush()
  window.dispatchEvent(new CustomEvent('ava:settings-tab', { detail: { tab: 'mcp' } }))
  await flush()
}

async function openProvidersTab(): Promise<void> {
  await flush()
  window.dispatchEvent(new CustomEvent('ava:settings-tab', { detail: { tab: 'providers' } }))
  await flush()
}

describe('SettingsModal MCP live state', () => {
  let container: HTMLDivElement
  let dispose: (() => void) | undefined

  beforeEach(() => {
    localStorage.clear()
    listMcpServersMock.mockReset()
    reloadMcpServersMock.mockReset()
    enableMcpServerMock.mockReset()
    disableMcpServerMock.mockReset()
    fetchModelsMock.mockReset()
    notificationErrorMock.mockReset()
    notificationSuccessMock.mockReset()
    closeSettingsMock.mockReset()
    updateProviderMock.mockReset()
    updateAgentMock.mockReset()
    addMcpServerMock.mockReset()
    removeMcpServerMock.mockReset()
    updateShortcutMock.mockReset()
    resetShortcutMock.mockReset()
    resetAllShortcutsMock.mockReset()

    setMockSettings(buildSettings())
    addMcpServerMock.mockImplementation((config: AppSettings['mcpServers'][number]) => {
      setMockSettings((prev) => ({
        ...prev,
        mcpServers: [...prev.mcpServers, config],
      }))
    })
    removeMcpServerMock.mockImplementation((name: string) => {
      setMockSettings((prev) => ({
        ...prev,
        mcpServers: prev.mcpServers.filter((server) => server.name !== name),
      }))
    })
    listMcpServersMock.mockResolvedValue([liveServer()])
    reloadMcpServersMock.mockResolvedValue({ serverCount: 1, toolCount: 2 })
    enableMcpServerMock.mockResolvedValue()
    disableMcpServerMock.mockResolvedValue()

    container = document.createElement('div')
    document.body.appendChild(container)
  })

  afterEach(() => {
    dispose?.()
    dispose = undefined
    localStorage.clear()
    document.body.innerHTML = ''
  })

  it('fetches live MCP servers when the MCP tab opens', async () => {
    dispose = render(() => <SettingsModal />, container)

    await openMcpTab()

    expect(listMcpServersMock).toHaveBeenCalledTimes(1)
    expect(container.textContent).toContain('live-server')
    expect(container.textContent).toContain('fallback-local')
    expect(container.textContent).toContain('2 tools')
  })

  it('keeps connected zero-tool MCP servers visible in the live list', async () => {
    listMcpServersMock.mockResolvedValueOnce([liveServer({ name: 'zero-tools', toolCount: 0 })])

    dispose = render(() => <SettingsModal />, container)
    await openMcpTab()

    expect(container.textContent).toContain('zero-tools')
    expect(container.textContent).toContain('Connected')
    expect(container.textContent).not.toContain('No MCP servers configured.')
  })

  it('surfaces failed MCP initialization status details from the backend', async () => {
    listMcpServersMock.mockResolvedValueOnce([
      liveServer({
        name: 'broken-server',
        toolCount: 0,
        status: 'failed',
        error: 'spawn failed: missing binary',
      }),
    ])

    dispose = render(() => <SettingsModal />, container)
    await openMcpTab()

    expect(container.textContent).toContain('broken-server')
    expect(container.textContent).toContain('Error')
    expect(container.textContent).toContain('spawn failed: missing binary')
  })

  it('keeps saved MCP entries visible while the initial live fetch is in flight', async () => {
    const pendingFetch = deferred<McpServerInfo[]>()
    listMcpServersMock.mockReturnValueOnce(pendingFetch.promise)

    dispose = render(() => <SettingsModal />, container)
    await openMcpTab()

    expect(listMcpServersMock).toHaveBeenCalledTimes(1)
    expect(container.textContent).toContain('fallback-local')
    expect(container.textContent).not.toContain('No MCP servers configured.')

    pendingFetch.resolve([liveServer()])
    await flush()

    expect(container.textContent).toContain('live-server')
  })

  it('toggles an MCP server from live backend state and refetches the list', async () => {
    listMcpServersMock
      .mockResolvedValueOnce([liveServer({ enabled: true, status: 'connected' })])
      .mockResolvedValueOnce([liveServer({ enabled: false, status: 'disabled' })])

    dispose = render(() => <SettingsModal />, container)
    await openMcpTab()

    click(getButtonByLabel('Disable live-server MCP server'))
    await flush()

    expect(disableMcpServerMock).toHaveBeenCalledWith('live-server')
    expect(listMcpServersMock).toHaveBeenCalledTimes(2)
    expect(container.textContent).toContain('Disabled')
    expect(() => getButtonByLabel('Enable live-server MCP server')).not.toThrow()
  })

  it('disables the enable toggle for config-disabled MCP servers that cannot be re-enabled', async () => {
    listMcpServersMock.mockResolvedValueOnce([
      liveServer({
        name: 'config-disabled',
        enabled: false,
        canToggle: false,
        status: 'disabled',
      }),
    ])

    dispose = render(() => <SettingsModal />, container)
    await openMcpTab()

    const toggle = getButtonByLabel('Enable config-disabled MCP server')
    expect(toggle.disabled).toBe(true)

    click(toggle)
    await flush()

    expect(enableMcpServerMock).not.toHaveBeenCalled()
    expect(disableMcpServerMock).not.toHaveBeenCalled()
  })

  it('reloads and refetches live MCP state from the refresh control', async () => {
    listMcpServersMock
      .mockResolvedValueOnce([liveServer({ toolCount: 2 })])
      .mockResolvedValueOnce([liveServer({ toolCount: 4 })])

    dispose = render(() => <SettingsModal />, container)
    await openMcpTab()

    click(getButtonByLabel('Refresh MCP servers'))
    await flush()

    expect(reloadMcpServersMock).toHaveBeenCalledTimes(1)
    expect(listMcpServersMock).toHaveBeenCalledTimes(2)
    expect(container.textContent).toContain('4 tools')
  })

  it('falls back to saved MCP entries after an initial live fetch failure', async () => {
    listMcpServersMock.mockRejectedValueOnce(new Error('live status unavailable'))

    dispose = render(() => <SettingsModal />, container)
    await openMcpTab()

    expect(notificationErrorMock).toHaveBeenCalledWith(
      'MCP status unavailable',
      'live status unavailable'
    )
    expect(container.textContent).not.toContain('No MCP servers configured.')
    expect(container.textContent).toContain('fallback-local')
    expect(queryButtonByLabel('Enable fallback-local MCP server')).toBeNull()
  })

  it('keeps saved MCP entries visible while retrying a failed live fetch', async () => {
    const retryFetch = deferred<McpServerInfo[]>()
    listMcpServersMock
      .mockRejectedValueOnce(new Error('live status unavailable'))
      .mockReturnValueOnce(retryFetch.promise)

    dispose = render(() => <SettingsModal />, container)
    await openMcpTab()

    expect(container.textContent).toContain('fallback-local')
    expect(container.textContent).not.toContain('No MCP servers configured.')

    click(getButtonByLabel('Refresh MCP servers'))
    await flush()

    expect(reloadMcpServersMock).toHaveBeenCalledTimes(1)
    expect(container.textContent).toContain('fallback-local')
    expect(container.textContent).not.toContain('No MCP servers configured.')

    retryFetch.resolve([liveServer({ toolCount: 3 })])
    await flush()

    expect(container.textContent).toContain('3 tools')
  })

  it('preserves the last live MCP snapshot when a later refresh fails', async () => {
    listMcpServersMock
      .mockResolvedValueOnce([liveServer({ toolCount: 2 })])
      .mockRejectedValueOnce(new Error('refresh failed'))

    dispose = render(() => <SettingsModal />, container)
    await openMcpTab()

    click(getButtonByLabel('Refresh MCP servers'))
    await flush()

    expect(notificationErrorMock).toHaveBeenCalledWith(
      'Failed to refresh MCP server status',
      'refresh failed'
    )
    expect(container.textContent).toContain('live-server')
    expect(container.textContent).toContain('fallback-local')
    expect(container.textContent).toContain('2 tools')
  })

  it('merges local additions into the existing live MCP view', async () => {
    dispose = render(() => <SettingsModal />, container)
    await openMcpTab()

    expect(container.textContent).toContain('live-server')
    expect(container.textContent).toContain('fallback-local')

    click(getButtonByLabel('Mock save MCP server'))
    await flush()

    expect(addMcpServerMock).toHaveBeenCalledWith({
      name: 'added-local',
      type: 'stdio',
      command: 'added-server',
    })
    expect(container.textContent).toContain('fallback-local')
    expect(container.textContent).toContain('added-local')
    expect(container.textContent).toContain('live-server')
  })

  it('recomputes saved-config flags when adding local config for an existing live server', async () => {
    dispose = render(() => <SettingsModal />, container)
    await openMcpTab()

    expect(queryButtonByLabel('Remove live-server MCP server')).toBeNull()

    click(getButtonByLabel('Mock save live-server MCP config'))
    await flush()

    expect(addMcpServerMock).toHaveBeenCalledWith(
      buildMcpServer('live-server', 'live-server-local')
    )
    expect(queryButtonByLabel('Remove live-server MCP server')).not.toBeNull()
    expect(container.textContent).toContain('live-server-local')
  })

  it('ignores stale in-flight MCP fetches after a local add invalidates the live cache', async () => {
    const pendingFetch = deferred<McpServerInfo[]>()
    listMcpServersMock.mockReturnValueOnce(pendingFetch.promise)

    dispose = render(() => <SettingsModal />, container)
    await openMcpTab()

    expect(container.textContent).toContain('fallback-local')

    click(getButtonByLabel('Mock save MCP server'))
    await flush()

    expect(container.textContent).toContain('fallback-local')
    expect(container.textContent).toContain('added-local')
    expect(container.textContent).not.toContain('live-server')

    pendingFetch.resolve([liveServer()])
    await flush()

    expect(container.textContent).toContain('fallback-local')
    expect(container.textContent).toContain('added-local')
    expect(container.textContent).not.toContain('live-server')
  })

  it('keeps unrelated live MCP servers visible after a local remove', async () => {
    dispose = render(() => <SettingsModal />, container)
    await openMcpTab()

    expect(container.textContent).toContain('live-server')
    expect(container.textContent).toContain('fallback-local')

    click(getButtonByLabel('Mock remove fallback-local MCP server'))
    await flush()

    expect(removeMcpServerMock).toHaveBeenCalledWith('fallback-local')
    expect(container.textContent).not.toContain('fallback-local')
    expect(container.textContent).toContain('live-server')
    expect(container.textContent).not.toContain('No MCP servers configured.')
  })

  it('recomputes saved-config flags when removing local config from an existing live server', async () => {
    setMockSettings({
      ...buildSettings(),
      mcpServers: [
        buildMcpServer('fallback-local', 'fallback-server'),
        buildMcpServer('live-server', 'live-server-local'),
      ],
    })

    dispose = render(() => <SettingsModal />, container)
    await openMcpTab()

    expect(queryButtonByLabel('Remove live-server MCP server')).not.toBeNull()
    expect(container.textContent).toContain('live-server-local')

    click(getButtonByLabel('Remove live-server MCP server'))
    await flush()

    expect(removeMcpServerMock).toHaveBeenCalledWith('live-server')
    expect(container.textContent).toContain('live-server')
    expect(queryButtonByLabel('Remove live-server MCP server')).toBeNull()
    expect(container.textContent).not.toContain('live-server-local')
  })

  it('does not show remove actions for backend-only live MCP rows', async () => {
    listMcpServersMock.mockResolvedValueOnce([liveServer({ name: 'live-only' })])

    dispose = render(() => <SettingsModal />, container)
    await openMcpTab()

    expect(container.textContent).toContain('live-only')
    expect(queryButtonByLabel('Remove live-only MCP server')).toBeNull()
  })

  it('closes Settings on Escape when no nested settings dialog owns the keypress', async () => {
    dispose = render(() => <SettingsModal />, container)
    await flush()

    window.dispatchEvent(new KeyboardEvent('keydown', { key: 'Escape', bubbles: true }))

    expect(closeSettingsMock).toHaveBeenCalledTimes(1)
  })

  it('ignores Escape while any nested settings dialog is open', async () => {
    dispose = render(() => <SettingsModal />, container)
    await flush()

    const nestedDialog = document.createElement('div')
    nestedDialog.setAttribute('data-settings-nested-dialog', 'true')
    document.body.appendChild(nestedDialog)

    window.dispatchEvent(new KeyboardEvent('keydown', { key: 'Escape', bubbles: true }))

    expect(closeSettingsMock).not.toHaveBeenCalled()
  })

  it('tests OAuth-backed providers using stored credentials', async () => {
    fetchModelsMock.mockResolvedValueOnce([
      { id: 'gpt-4.1', name: 'GPT-4.1', contextWindow: 128000 },
    ])
    localStorage.setItem(
      'ava_credentials',
      JSON.stringify({ openai: { type: 'oauth-token', value: 'oauth-token' } })
    )

    dispose = render(() => <SettingsModal />, container)
    await openProvidersTab()

    click(getButtonByLabel('Mock test OpenAI provider'))
    await flush()

    expect(fetchModelsMock).toHaveBeenCalledWith('openai', {
      apiKey: 'oauth-token',
      authType: 'oauth-token',
      baseUrl: undefined,
    })
    expect(updateProviderMock).toHaveBeenCalledWith(
      'openai',
      expect.objectContaining({
        defaultModel: 'gpt-5.2',
        status: 'connected',
        error: undefined,
        models: expect.arrayContaining([
          expect.objectContaining({
            id: 'gpt-4.1',
            name: 'GPT-4.1',
            contextWindow: 128000,
          }),
        ]),
      })
    )
    expect(notificationSuccessMock).toHaveBeenCalledWith(
      'Provider connected',
      expect.stringMatching(/^\d+ models available$/)
    )
  })

  it('tests OAuth-backed providers using the core auth-store token fallback', async () => {
    fetchModelsMock.mockResolvedValueOnce([
      { id: 'gpt-4.1-mini', name: 'GPT-4.1 Mini', contextWindow: 128000 },
    ])
    localStorage.setItem('ava_cred_ava:openai:oauth_token', 'core-oauth-token')

    dispose = render(() => <SettingsModal />, container)
    await openProvidersTab()

    click(getButtonByLabel('Mock test OpenAI provider'))
    await flush()

    expect(fetchModelsMock).toHaveBeenCalledWith('openai', {
      apiKey: 'core-oauth-token',
      authType: 'oauth-token',
      baseUrl: undefined,
    })
  })
})
