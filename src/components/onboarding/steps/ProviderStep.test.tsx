import { render } from 'solid-js/web'
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'

const startOAuthFlowMock = vi.fn()

vi.mock('../../../services/auth/oauth', () => ({
  isOAuthSupported: (provider: string) => provider === 'openai' || provider === 'copilot',
  startOAuthFlow: (...args: unknown[]) => startOAuthFlowMock(...args),
}))

vi.mock('../../settings/DeviceCodeDialog', () => ({
  DeviceCodeDialog: (props: { provider: string }) => (
    <div data-testid="device-code-dialog">Device code for {props.provider}</div>
  ),
}))

import { ProviderStep } from './ProviderStep'

function getButtonByText(container: HTMLElement, text: string): HTMLButtonElement {
  const button = Array.from(container.querySelectorAll('button')).find((candidate) =>
    candidate.textContent?.replace(/\s+/g, ' ').trim().includes(text)
  )

  if (!(button instanceof HTMLButtonElement)) {
    throw new Error(`Could not find button with text: ${text}`)
  }

  return button
}

async function flush(): Promise<void> {
  await Promise.resolve()
  await Promise.resolve()
}

describe('ProviderStep OAuth handling', () => {
  let dispose: (() => void) | undefined

  beforeEach(() => {
    vi.clearAllMocks()
  })

  afterEach(() => {
    dispose?.()
    dispose = undefined
    document.body.innerHTML = ''
  })

  it('marks PKCE OAuth providers connected without persisting a fake API key', async () => {
    startOAuthFlowMock.mockResolvedValueOnce({
      kind: 'connected',
      tokens: { accessToken: 'oauth-token' },
    })
    const onSetProviderKey = vi.fn()
    const onSetProviderConnected = vi.fn()

    const container = document.createElement('div')
    document.body.append(container)
    dispose = render(
      () => (
        <ProviderStep
          onPrev={vi.fn()}
          onNext={vi.fn()}
          onSkip={vi.fn()}
          providerKeys={{}}
          oauthProviders={[]}
          onSetProviderKey={onSetProviderKey}
          onSetProviderConnected={onSetProviderConnected}
        />
      ),
      container
    )

    // OpenAI has multiple auth options, so we need to click the OAuth button specifically
    getButtonByText(container, 'OAuth').click()
    await flush()

    expect(onSetProviderKey).toHaveBeenCalledWith('openai', '')
    expect(onSetProviderConnected).toHaveBeenCalledWith('openai', true)
  })

  it('expands API key input when API Key option is clicked for multi-auth providers', async () => {
    const onSetProviderKey = vi.fn()
    const onSetProviderConnected = vi.fn()

    const container = document.createElement('div')
    document.body.append(container)
    dispose = render(
      () => (
        <ProviderStep
          onPrev={vi.fn()}
          onNext={vi.fn()}
          onSkip={vi.fn()}
          providerKeys={{}}
          oauthProviders={[]}
          onSetProviderKey={onSetProviderKey}
          onSetProviderConnected={onSetProviderConnected}
        />
      ),
      container
    )

    // OpenAI has multiple auth options, click API Key to expand the input
    getButtonByText(container, 'API Key').click()
    await flush()

    // Should show the API key input
    const apiInput = container.querySelector('input[placeholder="Enter OpenAI API key..."]')
    expect(apiInput).toBeInstanceOf(HTMLInputElement)
  })

  it('shows device-code UI without marking Copilot connected immediately', async () => {
    startOAuthFlowMock.mockResolvedValueOnce({
      kind: 'pending',
      deviceCode: {
        deviceCode: 'device-code',
        userCode: 'user-code',
        verificationUri: 'https://github.com/login/device',
        expiresIn: 900,
        interval: 5,
      },
    })
    const onSetProviderKey = vi.fn()
    const onSetProviderConnected = vi.fn()

    const container = document.createElement('div')
    document.body.append(container)
    dispose = render(
      () => (
        <ProviderStep
          onPrev={vi.fn()}
          onNext={vi.fn()}
          onSkip={vi.fn()}
          providerKeys={{}}
          oauthProviders={[]}
          onSetProviderKey={onSetProviderKey}
          onSetProviderConnected={onSetProviderConnected}
        />
      ),
      container
    )

    // Copilot has a single OAuth option - the card has an aria-label for accessibility
    // Find the button with aria-label for Copilot
    const copilotBtn = container.querySelector('button[aria-label="Connect Copilot"]')
    if (!(copilotBtn instanceof HTMLButtonElement)) {
      throw new Error('Could not find Copilot connect button')
    }
    copilotBtn.click()
    await flush()

    expect(onSetProviderConnected).not.toHaveBeenCalled()
    expect(onSetProviderKey).not.toHaveBeenCalled()
    expect(document.querySelector('[data-testid="device-code-dialog"]')?.textContent).toContain(
      'copilot'
    )
  })

  it('scopes the single-auth full-card button to its own provider tile', () => {
    const container = document.createElement('div')
    document.body.append(container)
    dispose = render(
      () => (
        <ProviderStep
          onPrev={vi.fn()}
          onNext={vi.fn()}
          onSkip={vi.fn()}
          providerKeys={{}}
          oauthProviders={[]}
          onSetProviderKey={vi.fn()}
          onSetProviderConnected={vi.fn()}
        />
      ),
      container
    )

    const copilotBtn = container.querySelector('button[aria-label="Connect Copilot"]')
    if (!(copilotBtn instanceof HTMLButtonElement)) {
      throw new Error('Could not find Copilot connect button')
    }

    expect(copilotBtn.parentElement).toBeInstanceOf(HTMLDivElement)
    expect((copilotBtn.parentElement as HTMLDivElement).style.position).toBe('relative')
  })
})
