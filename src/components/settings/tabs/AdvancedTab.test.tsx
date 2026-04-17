import { createSignal, type JSX } from 'solid-js'
import { render } from 'solid-js/web'
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'

const setDebugDevModeMock = vi.fn()
const updateSettingsMock = vi.fn()

const [mockSettings, setMockSettings] = createSignal({
  devMode: false,
  logLevel: 'info',
})

vi.mock('../../../lib/debug-log', () => ({
  setDebugDevMode: (...args: unknown[]) => setDebugDevModeMock(...args),
}))

vi.mock('../../../stores/settings', () => ({
  useSettings: () => ({
    settings: mockSettings,
    updateSettings: (...args: unknown[]) => {
      // Simulate the store updating the setting
      const updates = args[0] as Record<string, unknown>
      if ('devMode' in updates) {
        setMockSettings((prev) => ({ ...prev, devMode: updates.devMode as boolean }))
      }
      updateSettingsMock(...args)
    },
  }),
}))

// Mock dependent components
vi.mock('../settings-about-section', () => ({
  AboutSection: () => <div data-testid="about-section">About</div>,
}))

vi.mock('../SettingsCard', () => ({
  SettingsCard: (props: { children: JSX.Element; title: string }) => (
    <div data-testid={`card-${props.title.toLowerCase()}`}>{props.children}</div>
  ),
}))

vi.mock('./DeveloperTab', () => ({
  DeveloperTab: (props: { showToggle?: boolean }) => (
    <div data-testid="developer-tab">
      DeveloperTab (showToggle: {props.showToggle !== false ? 'true' : 'false'})
    </div>
  ),
}))

import { AdvancedTab } from './AdvancedTab'

function getToggleButton(): HTMLButtonElement {
  const button = document.querySelector('[role="switch"]')
  if (!(button instanceof HTMLButtonElement)) {
    throw new Error('Could not find toggle button')
  }
  return button
}

describe('AdvancedTab Developer Mode integration', () => {
  let dispose: (() => void) | undefined

  beforeEach(() => {
    vi.clearAllMocks()
    setMockSettings({ devMode: false, logLevel: 'info' })
  })

  afterEach(() => {
    dispose?.()
    dispose = undefined
    document.body.innerHTML = ''
  })

  it('calls setDebugDevMode when toggling developer mode from AdvancedTab', async () => {
    const container = document.createElement('div')
    document.body.append(container)

    dispose = render(() => <AdvancedTab />, container)

    await Promise.resolve()
    await Promise.resolve()

    // Initially devMode is off
    expect(mockSettings().devMode).toBe(false)
    expect(setDebugDevModeMock).not.toHaveBeenCalled()

    // Click the toggle
    const toggle = getToggleButton()
    toggle.click()

    await Promise.resolve()
    await Promise.resolve()

    // Should update settings and call setDebugDevMode
    expect(updateSettingsMock).toHaveBeenCalledWith({ devMode: true })
    expect(setDebugDevModeMock).toHaveBeenCalledWith(true)
    expect(setDebugDevModeMock).toHaveBeenCalledTimes(1)
  })

  it('calls setDebugDevMode with false when toggling off', async () => {
    // Start with devMode enabled
    setMockSettings({ devMode: true, logLevel: 'info' })

    const container = document.createElement('div')
    document.body.append(container)

    dispose = render(() => <AdvancedTab />, container)

    await Promise.resolve()
    await Promise.resolve()

    // Click the toggle to turn off
    const toggle = getToggleButton()
    toggle.click()

    await Promise.resolve()
    await Promise.resolve()

    expect(updateSettingsMock).toHaveBeenCalledWith({ devMode: false })
    expect(setDebugDevModeMock).toHaveBeenCalledWith(false)
  })

  it('renders DeveloperTab with showToggle={false} when devMode is enabled', async () => {
    // Start with devMode enabled
    setMockSettings({ devMode: true, logLevel: 'info' })

    const container = document.createElement('div')
    document.body.append(container)

    dispose = render(() => <AdvancedTab />, container)

    await Promise.resolve()
    await Promise.resolve()

    // DeveloperTab should be rendered
    expect(container.textContent).toContain('DeveloperTab')
    // Should have showToggle set to false
    expect(container.textContent).toContain('showToggle: false')
  })

  it('does not render DeveloperTab when devMode is disabled', async () => {
    // Start with devMode disabled
    setMockSettings({ devMode: false, logLevel: 'info' })

    const container = document.createElement('div')
    document.body.append(container)

    dispose = render(() => <AdvancedTab />, container)

    await Promise.resolve()
    await Promise.resolve()

    // DeveloperTab should not be rendered
    expect(container.textContent).not.toContain('DeveloperTab')
    // Should show the enable prompt
    expect(container.textContent).toContain('Enable to access live console output')
  })

  it('does not render embedded Agents or Extension Surfaces sections', async () => {
    const container = document.createElement('div')
    document.body.append(container)

    dispose = render(() => <AdvancedTab />, container)

    await Promise.resolve()
    await Promise.resolve()

    expect(container.querySelector('[data-testid="card-agents"]')).toBeNull()
    expect(container.querySelector('[data-testid="card-extension surfaces"]')).toBeNull()
    expect(container.textContent).not.toContain('Plugins')
    expect(container.textContent).not.toContain('MCP Servers')
  })
})
