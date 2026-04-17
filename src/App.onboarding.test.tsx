import { type JSX, Show } from 'solid-js'
import { render } from 'solid-js/web'
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'

vi.mock('@kobalte/core/dialog', () => {
  type MockDialogProps = Record<string, unknown> & { children?: JSX.Element }
  const passthrough = (props: MockDialogProps) => props.children

  const Content = (props: MockDialogProps) => {
    const onEscapeKeyDown = props.onEscapeKeyDown as ((event: KeyboardEvent) => void) | undefined
    const onKeyDown = props.onKeyDown as ((event: KeyboardEvent) => void) | undefined

    return (
      <div
        {...props}
        role={
          typeof props.role === 'string'
            ? (props.role as JSX.HTMLAttributes<HTMLDivElement>['role'])
            : 'presentation'
        }
        tabIndex={typeof props.tabIndex === 'number' ? props.tabIndex : -1}
        ref={(element) => {
          element.addEventListener('keydown', (event) => {
            onKeyDown?.(event)
            if (event.key === 'Escape') onEscapeKeyDown?.(event)
          })
        }}
      >
        {props.children}
      </div>
    )
  }

  return {
    Dialog: Object.assign(passthrough, {
      Portal: passthrough,
      Overlay: (props: MockDialogProps) => <div {...props}>{props.children}</div>,
      Content,
      Title: (props: MockDialogProps) => <div {...props}>{props.children}</div>,
      Description: (props: MockDialogProps) => <div {...props}>{props.children}</div>,
    }),
  }
})

vi.mock('./components/AppDialogs', () => ({
  AppDialogs: () => null,
}))

vi.mock('./components/SplashScreen', () => ({
  SplashScreen: () => null,
}))

vi.mock('./components/project-hub/ProjectHub', () => ({
  ProjectHub: () => <div>Project Hub</div>,
}))

vi.mock('./components/layout', async () => {
  const { useLayout } = await import('./stores/layout')

  return {
    AppShell: () => {
      const { openSettings, settingsOpen, closeSettings } = useLayout()

      return (
        <div>
          <button type="button" aria-label="Settings" onClick={openSettings}>
            Settings
          </button>
          <Show when={settingsOpen()}>
            <div role="dialog" aria-label="Settings modal">
              <button type="button" onClick={closeSettings}>
                Close Settings
              </button>
            </div>
          </Show>
        </div>
      )
    },
  }
})

vi.mock('./components/dialogs/ChangelogDialog', () => ({
  shouldShowChangelog: () => false,
}))

vi.mock('./contexts/notification', () => ({
  useNotification: () => ({
    info: vi.fn(),
  }),
}))

vi.mock('./hooks/useAppInit', () => ({
  runAppInit: vi.fn(async () => ({ error: null, notTauri: false })),
}))

vi.mock('./hooks/useAppShortcuts', () => ({
  registerAppShortcuts: vi.fn(),
}))

vi.mock('./services/auto-updater', () => ({
  checkForUpdate: vi.fn(async () => ({ available: false })),
  downloadAndInstallUpdate: vi.fn(),
}))

vi.mock('./services/dev-console', () => ({
  setDevConsoleLogLevel: vi.fn(),
}))

vi.mock('./services/logger', () => ({
  setLogLevel: vi.fn(),
}))

vi.mock('@tauri-apps/api/path', () => ({
  homeDir: vi.fn(async () => '/home/test'),
}))

vi.mock('./stores/settings', async () => {
  const { createRoot, createSignal } = await import('solid-js')

  const initialSettings: {
    onboardingComplete: boolean
    logLevel: string
    mode: 'light' | 'dark' | 'system'
  } = {
    onboardingComplete: false,
    logLevel: 'info',
    mode: 'dark' as const,
  }

  const store = createRoot(() => {
    const [settings, setSettings] = createSignal(initialSettings)
    return { settings, setSettings }
  })

  const updateSettings = vi.fn((patch: Partial<typeof initialSettings>) => {
    store.setSettings((prev) => ({ ...prev, ...patch }))
  })
  const updateProvider: MockUpdateProvider = vi.fn()
  const updateAppearance = vi.fn()

  return {
    useSettings: () => ({
      settings: store.settings,
      updateSettings,
      updateProvider,
      updateAppearance,
    }),
    applyAppearance: vi.fn(),
    envKeysDetected: () => null,
    setupSystemThemeListener: () => () => {},
    __getSettingsSpies: () => ({ updateSettings, updateProvider, updateAppearance }),
    __resetSettingsMock: (patch?: Partial<typeof initialSettings>) => {
      store.setSettings({ ...initialSettings, ...patch })
      updateSettings.mockClear()
      updateProvider.mockClear()
      updateAppearance.mockClear()
    },
  }
})

import App, { applyOnboardingProviderSelections } from './App'
import { runAppInit } from './hooks/useAppInit'
import { useLayout } from './stores/layout'
import * as settingsStore from './stores/settings'

const startOAuthFlowMock = vi.fn()

vi.mock('./services/auth/oauth', () => ({
  isOAuthSupported: (provider: string) => provider === 'openai',
  startOAuthFlow: (...args: unknown[]) => startOAuthFlowMock(...args),
}))

type MockUpdateProvider = ReturnType<
  typeof vi.fn<
    (
      providerId: string,
      patch: {
        status?: 'connected' | 'disconnected' | 'error'
        apiKey?: string
        enabled?: boolean
        [key: string]: unknown
      }
    ) => void
  >
>

function getButtonByText(container: HTMLElement, text: string): HTMLButtonElement {
  const button = Array.from(container.querySelectorAll('button')).find((candidate) =>
    candidate.textContent?.replace(/\s+/g, ' ').trim().includes(text)
  )

  if (!(button instanceof HTMLButtonElement)) {
    throw new Error(`Could not find button with text: ${text}`)
  }

  return button
}

function clickButton(button: HTMLButtonElement): void {
  button.dispatchEvent(new MouseEvent('click', { bubbles: true, cancelable: true }))
}

async function flushApp(): Promise<void> {
  await Promise.resolve()
  await Promise.resolve()
  await Promise.resolve()
  await new Promise((resolve) => setTimeout(resolve, 0))
  await new Promise((resolve) => requestAnimationFrame(() => requestAnimationFrame(resolve)))
}

describe('App onboarding flow', () => {
  let dispose: (() => void) | undefined
  const runAppInitMock = vi.mocked(runAppInit)

  beforeEach(() => {
    ;(
      settingsStore as typeof settingsStore & { __resetSettingsMock: (patch?: object) => void }
    ).__resetSettingsMock()

    const layout = useLayout()
    layout.closeSettings()
    layout.setProjectHubVisible(false)
    runAppInitMock.mockResolvedValue({ error: null, notTauri: false })
  })

  afterEach(() => {
    dispose?.()
    dispose = undefined

    document.body.innerHTML = ''

    const layout = useLayout()
    layout.closeSettings()
  })

  it('unmounts onboarding after skipping and allows Settings to open', async () => {
    const container = document.createElement('div')
    document.body.append(container)
    dispose = render(() => <App />, container)

    await flushApp()

    expect(document.querySelector('[data-testid="onboarding-overlay"]')).not.toBeNull()
    expect(document.activeElement?.textContent).toContain('Welcome to AVA')

    clickButton(getButtonByText(document.body, 'Skip setup for now'))

    await flushApp()

    expect(document.querySelector('[data-testid="onboarding-overlay"]')).toBeNull()

    const settingsButton = container.querySelector('button[aria-label="Settings"]')
    expect(settingsButton).toBeInstanceOf(HTMLButtonElement)
    expect(document.activeElement).toBe(settingsButton)

    clickButton(settingsButton as HTMLButtonElement)

    expect(container.querySelector('[aria-label="Settings modal"]')).not.toBeNull()
  })

  it('unmounts onboarding after completion and allows Settings to open', async () => {
    const container = document.createElement('div')
    document.body.append(container)
    dispose = render(() => <App />, container)

    await flushApp()

    clickButton(getButtonByText(document.body, 'Get Started'))
    await flushApp()
    expect(document.activeElement?.textContent).toContain('Connect a Provider')
    clickButton(getButtonByText(document.body, 'Skip'))
    await flushApp()
    expect(document.activeElement?.textContent).toContain('Make It Yours')
    clickButton(getButtonByText(document.body, 'Continue'))
    await flushApp()
    expect(document.activeElement?.textContent).toContain('Set Up Workspace')
    clickButton(getButtonByText(document.body, 'Continue'))
    await flushApp()
    expect(document.activeElement?.textContent).toContain("You're All Set")
    clickButton(getButtonByText(document.body, 'Start Coding'))

    await flushApp()

    expect(document.querySelector('[data-testid="onboarding-overlay"]')).toBeNull()

    const settingsButton = container.querySelector('button[aria-label="Settings"]')
    expect(settingsButton).toBeInstanceOf(HTMLButtonElement)
    expect(document.activeElement).toBe(settingsButton)

    clickButton(settingsButton as HTMLButtonElement)

    expect(container.querySelector('[aria-label="Settings modal"]')).not.toBeNull()
  })

  it('applies the selected onboarding theme mode on completion', async () => {
    const container = document.createElement('div')
    document.body.append(container)
    dispose = render(() => <App />, container)

    await flushApp()

    clickButton(getButtonByText(document.body, 'Get Started'))
    await flushApp()
    clickButton(getButtonByText(document.body, 'Skip'))
    await flushApp()
    clickButton(getButtonByText(document.body, 'Light'))
    clickButton(getButtonByText(document.body, 'Continue'))
    await flushApp()
    clickButton(getButtonByText(document.body, 'Continue'))
    await flushApp()
    clickButton(getButtonByText(document.body, 'Start Coding'))

    await flushApp()

    expect(settingsStore.useSettings().settings().mode).toBe('light')
  })

  it('prefers API keys over stale oauth markers at onboarding completion', () => {
    const { updateProvider } = (
      settingsStore as typeof settingsStore & {
        __getSettingsSpies: () => {
          updateProvider: MockUpdateProvider
        }
      }
    ).__getSettingsSpies()

    applyOnboardingProviderSelections(
      {
        providerKeys: { openai: '  sk-live  ' },
        oauthProviders: ['openai'],
      },
      updateProvider
    )

    expect(updateProvider).toHaveBeenCalledTimes(1)
    expect(updateProvider).toHaveBeenCalledWith('openai', {
      apiKey: 'sk-live',
      status: 'connected',
      enabled: true,
    })
  })

  it('closes Settings before reopening onboarding from the in-app guide action', async () => {
    const container = document.createElement('div')
    document.body.append(container)
    dispose = render(() => <App />, container)

    await flushApp()

    clickButton(getButtonByText(document.body, 'Skip setup for now'))
    await flushApp()

    const settingsButton = container.querySelector('button[aria-label="Settings"]')
    expect(settingsButton).toBeInstanceOf(HTMLButtonElement)

    clickButton(settingsButton as HTMLButtonElement)
    expect(container.querySelector('[aria-label="Settings modal"]')).not.toBeNull()

    window.dispatchEvent(
      new CustomEvent('ava:open-onboarding', {
        detail: { returnFocusSelector: 'button[aria-label="Settings"]' },
      })
    )
    await flushApp()

    expect(container.querySelector('[aria-label="Settings modal"]')).toBeNull()
    expect(document.querySelector('[data-testid="onboarding-overlay"]')).not.toBeNull()
    expect(
      document.querySelector('[role="dialog"][aria-modal="true"][aria-label="Onboarding"]')
    ).not.toBeNull()
    expect(document.activeElement?.textContent).toContain('Welcome to AVA')

    clickButton(getButtonByText(document.body, 'Close guide'))
    await flushApp()
  })

  it('dismisses reopened onboarding from later steps and restores focus to Settings', async () => {
    const container = document.createElement('div')
    document.body.append(container)
    dispose = render(() => <App />, container)

    await flushApp()

    clickButton(getButtonByText(document.body, 'Skip setup for now'))
    await flushApp()

    const settingsButton = container.querySelector('button[aria-label="Settings"]')
    expect(settingsButton).toBeInstanceOf(HTMLButtonElement)

    clickButton(settingsButton as HTMLButtonElement)
    expect(container.querySelector('[aria-label="Settings modal"]')).not.toBeNull()

    const closeSettingsButton = getButtonByText(container, 'Close Settings')
    closeSettingsButton.focus()

    window.dispatchEvent(
      new CustomEvent('ava:open-onboarding', {
        detail: { returnFocusSelector: 'button[aria-label="Settings"]' },
      })
    )
    await flushApp()

    clickButton(getButtonByText(document.body, 'Get Started'))
    await flushApp()
    expect(document.activeElement?.textContent).toContain('Connect a Provider')

    clickButton(getButtonByText(document.body, 'Continue'))
    await flushApp()
    expect(document.activeElement?.textContent).toContain('Make It Yours')

    clickButton(getButtonByText(document.body, 'Close guide'))
    await flushApp()

    expect(document.querySelector('[data-testid="onboarding-overlay"]')).toBeNull()
    expect(document.activeElement).toBe(settingsButton)
  })

  it('dismisses guide-mode onboarding with Escape and restores focus to Settings', async () => {
    const container = document.createElement('div')
    document.body.append(container)
    dispose = render(() => <App />, container)

    await flushApp()

    clickButton(getButtonByText(document.body, 'Skip setup for now'))
    await flushApp()

    const settingsButton = container.querySelector('button[aria-label="Settings"]')
    expect(settingsButton).toBeInstanceOf(HTMLButtonElement)

    clickButton(settingsButton as HTMLButtonElement)
    expect(container.querySelector('[aria-label="Settings modal"]')).not.toBeNull()

    window.dispatchEvent(
      new CustomEvent('ava:open-onboarding', {
        detail: { returnFocusSelector: 'button[aria-label="Settings"]' },
      })
    )
    await flushApp()

    const onboardingDialog = document.querySelector('[role="dialog"][aria-label="Onboarding"]')
    expect(onboardingDialog).toBeInstanceOf(HTMLDivElement)

    onboardingDialog?.dispatchEvent(new KeyboardEvent('keydown', { key: 'Escape', bubbles: true }))
    await flushApp()

    expect(document.querySelector('[data-testid="onboarding-overlay"]')).toBeNull()
    expect(document.activeElement).toBe(settingsButton)
  })

  it('applies provider API-key changes when guide is dismissed', async () => {
    const { updateProvider } = (
      settingsStore as typeof settingsStore & {
        __getSettingsSpies: () => { updateProvider: MockUpdateProvider }
      }
    ).__getSettingsSpies()

    const container = document.createElement('div')
    document.body.append(container)
    dispose = render(() => <App />, container)

    await flushApp()

    clickButton(getButtonByText(document.body, 'Skip setup for now'))
    await flushApp()

    const settingsButton = container.querySelector('button[aria-label="Settings"]')
    expect(settingsButton).toBeInstanceOf(HTMLButtonElement)
    clickButton(settingsButton as HTMLButtonElement)

    window.dispatchEvent(
      new CustomEvent('ava:open-onboarding', {
        detail: { returnFocusSelector: 'button[aria-label="Settings"]' },
      })
    )
    await flushApp()

    clickButton(getButtonByText(document.body, 'Get Started'))
    await flushApp()

    // Anthropic has a single API Key option - use the aria-label to find the transparent overlay button
    const anthropicBtn = document.body.querySelector('button[aria-label="Connect Anthropic"]')
    if (!(anthropicBtn instanceof HTMLButtonElement)) {
      throw new Error('Could not find Anthropic connect button')
    }
    clickButton(anthropicBtn)
    const apiInput = container.querySelector<HTMLInputElement>(
      'input[placeholder="Enter Anthropic API key..."]'
    )
    if (!(apiInput instanceof HTMLInputElement)) {
      throw new Error('Anthropic API key input was not found')
    }

    apiInput.value = '  sk-anthropic-REVEALABLE  '
    apiInput.dispatchEvent(new Event('input', { bubbles: true }))
    await flushApp()

    expect(updateProvider).not.toHaveBeenCalled()

    clickButton(getButtonByText(document.body, 'Close guide'))
    await flushApp()

    expect(updateProvider).toHaveBeenCalledTimes(1)
    expect(updateProvider).toHaveBeenCalledWith('anthropic', {
      apiKey: 'sk-anthropic-REVEALABLE',
      status: 'connected',
      enabled: true,
    })
  })

  it('applies OAuth provider connection when guide is dismissed', async () => {
    startOAuthFlowMock.mockResolvedValueOnce({
      kind: 'connected',
      tokens: {
        accessToken: 'oauth-token',
      },
    })

    const { updateProvider } = (
      settingsStore as typeof settingsStore & {
        __getSettingsSpies: () => { updateProvider: MockUpdateProvider }
      }
    ).__getSettingsSpies()

    const container = document.createElement('div')
    document.body.append(container)
    dispose = render(() => <App />, container)

    await flushApp()

    clickButton(getButtonByText(document.body, 'Skip setup for now'))
    await flushApp()

    const settingsButton = container.querySelector('button[aria-label="Settings"]')
    expect(settingsButton).toBeInstanceOf(HTMLButtonElement)
    clickButton(settingsButton as HTMLButtonElement)

    window.dispatchEvent(
      new CustomEvent('ava:open-onboarding', {
        detail: { returnFocusSelector: 'button[aria-label="Settings"]' },
      })
    )
    await flushApp()

    clickButton(getButtonByText(document.body, 'Get Started'))
    await flushApp()

    // OpenAI has multiple auth options (OAuth and API Key), click OAuth specifically
    clickButton(getButtonByText(document.body, 'OAuth'))
    await flushApp()

    expect(updateProvider).not.toHaveBeenCalled()

    clickButton(getButtonByText(document.body, 'Close guide'))
    await flushApp()

    expect(updateProvider).toHaveBeenCalledTimes(1)
    expect(updateProvider).toHaveBeenCalledWith('openai', {
      apiKey: undefined,
      status: 'connected',
      enabled: true,
    })
  })

  it('renders web-mode init error copy and details', async () => {
    ;(
      settingsStore as typeof settingsStore & { __resetSettingsMock: (patch?: object) => void }
    ).__resetSettingsMock({ onboardingComplete: true })
    runAppInitMock.mockResolvedValueOnce({
      error: 'Cannot reach backend: ECONNREFUSED',
      notTauri: true,
    })

    const container = document.createElement('div')
    document.body.append(container)
    dispose = render(() => <App />, container)

    await flushApp()

    expect(document.body.textContent).toContain('Web Mode Issue')
    expect(document.body.textContent).toContain('AVA web backend is not running')
    expect(document.body.textContent).toContain(
      'This browser view expects the HTTP backend. Start AVA in web mode, then reload this page.'
    )
    expect(document.body.textContent).toContain('Cannot reach backend: ECONNREFUSED')
    expect(document.body.textContent).toContain(
      'For desktop testing, use `pnpm tauri dev`. `ava serve` is only for browser-based web mode.'
    )
    expect(getButtonByText(document.body, 'Retry')).toBeInstanceOf(HTMLButtonElement)
  })

  it('renders desktop startup error copy and details', async () => {
    ;(
      settingsStore as typeof settingsStore & { __resetSettingsMock: (patch?: object) => void }
    ).__resetSettingsMock({ onboardingComplete: true })
    runAppInitMock.mockResolvedValueOnce({
      error: 'Rust backend initialization timed out',
      notTauri: false,
    })

    const container = document.createElement('div')
    document.body.append(container)
    dispose = render(() => <App />, container)

    await flushApp()

    expect(document.body.textContent).toContain('Startup Issue')
    expect(document.body.textContent).toContain('Desktop backend is not ready yet')
    expect(document.body.textContent).toContain(
      'AVA could not complete initialization. If Cargo is still building the backend, this screen can appear briefly before everything comes online.'
    )
    expect(document.body.textContent).toContain('Rust backend initialization timed out')
    expect(document.body.textContent).toContain(
      'First launch or post-refactor rebuilds can take a little longer while the Rust side starts up.'
    )
    expect(getButtonByText(document.body, 'Retry')).toBeInstanceOf(HTMLButtonElement)
  })
})
