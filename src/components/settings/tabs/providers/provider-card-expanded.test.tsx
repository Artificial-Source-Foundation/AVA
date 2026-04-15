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
  OllamaModelBrowser: () => null,
}))

vi.mock('../provider-row-api-key-input', () => ({
  ProviderRowApiKeyInput: () => null,
}))

vi.mock('../provider-row-clear-confirm', () => ({
  ProviderRowClearConfirm: () => null,
}))

vi.mock('./ModelsListSection', () => ({
  ModelsListSection: () => null,
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
})
