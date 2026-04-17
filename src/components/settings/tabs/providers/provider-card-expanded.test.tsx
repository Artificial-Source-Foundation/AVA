import { render } from 'solid-js/web'
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'

const startOAuthFlowMock = vi.fn()

vi.mock('../../../../services/auth/oauth', () => ({
  isOAuthSupported: () => true,
  startOAuthFlow: (...args: unknown[]) => startOAuthFlowMock(...args),
}))

vi.mock('../../../../services/logger', () => ({
  logError: vi.fn(),
}))

vi.mock('../../../../lib/auth-helpers', () => ({
  removeStoredAuth: vi.fn().mockResolvedValue(undefined),
}))

vi.mock('../../DeviceCodeDialog', () => ({
  DeviceCodeDialog: () => null,
}))

vi.mock('../../OllamaModelBrowser', () => ({
  OllamaModelBrowser: (props: { open: boolean }) => (
    <div data-testid="ollama-browser" data-open={props.open ? 'true' : 'false'}>
      Ollama browser
    </div>
  ),
}))

vi.mock('../provider-row-api-key-input', () => ({
  ProviderRowApiKeyInput: () => null,
}))

vi.mock('../provider-row-clear-confirm', () => ({
  ProviderRowClearConfirm: () => null,
}))

import { ProviderCardExpanded } from './provider-card-expanded'

async function flush(): Promise<void> {
  await Promise.resolve()
  await Promise.resolve()
}

describe('ProviderCardExpanded OAuth handling', () => {
  let container: HTMLDivElement
  let dispose: (() => void) | undefined

  beforeEach(() => {
    startOAuthFlowMock.mockReset()
    localStorage.clear()
    container = document.createElement('div')
    document.body.appendChild(container)
  })

  afterEach(() => {
    dispose?.()
    dispose = undefined
    document.body.innerHTML = ''
    localStorage.clear()
  })

  it('notifies provider settings when OAuth succeeds immediately', async () => {
    const onOAuthConnected = vi.fn()
    startOAuthFlowMock.mockResolvedValueOnce({
      kind: 'connected',
      tokens: { accessToken: 'oauth-token' },
    })

    dispose = render(
      () => (
        <ProviderCardExpanded
          provider={{
            id: 'openai',
            name: 'OpenAI',
            icon: (() => null) as never,
            description: 'OpenAI provider',
            enabled: true,
            status: 'disconnected',
            models: [],
            apiKey: 'sk-stale',
          }}
          onOAuthConnected={onOAuthConnected}
        />
      ),
      container
    )

    const button = Array.from(container.querySelectorAll('button')).find((candidate) =>
      candidate.textContent?.includes('Sign in with ChatGPT')
    )
    if (!(button instanceof HTMLButtonElement)) {
      throw new Error('OAuth button was not rendered')
    }

    button.click()
    await flush()

    expect(onOAuthConnected).toHaveBeenCalledTimes(1)
  })

  it('shows logout for OAuth-backed providers without an API key', () => {
    localStorage.setItem(
      'ava_credentials',
      JSON.stringify({ openai: { type: 'oauth-token', value: 'oauth-token' } })
    )

    dispose = render(
      () => (
        <ProviderCardExpanded
          provider={{
            id: 'openai',
            name: 'OpenAI',
            icon: (() => null) as never,
            description: 'OpenAI provider',
            enabled: true,
            status: 'connected',
            models: [],
          }}
        />
      ),
      container
    )

    const button = Array.from(container.querySelectorAll('button')).find((candidate) =>
      candidate.textContent?.includes('Log out')
    )
    if (!(button instanceof HTMLButtonElement)) {
      throw new Error('Log out button was not rendered')
    }
  })

  it('restores base URL labeling, default-model selection, and Ollama model management', async () => {
    const onSetDefaultModel = vi.fn()

    dispose = render(
      () => (
        <ProviderCardExpanded
          provider={{
            id: 'ollama',
            name: 'Ollama',
            icon: (() => null) as never,
            description: 'Local models',
            enabled: true,
            status: 'connected',
            baseUrl: 'http://localhost:11434',
            defaultModel: 'llama3.2:latest',
            models: [
              { id: 'llama3.2:latest', name: 'llama3.2:latest', contextWindow: 8192 },
              { id: 'mistral:latest', name: 'mistral:latest', contextWindow: 8192 },
            ],
          }}
          onSetDefaultModel={onSetDefaultModel}
        />
      ),
      container
    )

    const label = Array.from(container.querySelectorAll('label')).find(
      (candidate) => candidate.textContent === 'Base URL'
    )
    const baseUrlInput = container.querySelector('input')

    expect(label).toBeTruthy()
    expect(baseUrlInput?.getAttribute('id')).toBe(label?.getAttribute('for'))

    const defaultModelSelect = Array.from(container.querySelectorAll('select')).find(
      (candidate) => candidate.options.length === 2
    )
    if (!(defaultModelSelect instanceof HTMLSelectElement)) {
      throw new Error('Default model select was not rendered')
    }

    defaultModelSelect.value = 'mistral:latest'
    defaultModelSelect.dispatchEvent(new Event('change', { bubbles: true }))

    expect(onSetDefaultModel).toHaveBeenCalledWith('mistral:latest')

    const manageButton = Array.from(container.querySelectorAll('button')).find((candidate) =>
      candidate.textContent?.includes('Manage Local Models')
    )
    if (!(manageButton instanceof HTMLButtonElement)) {
      throw new Error('Manage Local Models button was not rendered')
    }

    manageButton.click()
    await flush()

    expect(
      container.querySelector('[data-testid="ollama-browser"]')?.getAttribute('data-open')
    ).toBe('true')
  })
})
