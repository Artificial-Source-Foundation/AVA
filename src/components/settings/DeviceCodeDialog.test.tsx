import { render } from 'solid-js/web'
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'

const openUrlMock = vi.fn()
const pollDeviceCodeAuthMock = vi.fn()
const storeOAuthCredentialsMock = vi.fn()

vi.mock('@tauri-apps/plugin-opener', () => ({
  openUrl: (...args: unknown[]) => openUrlMock(...args),
}))

vi.mock('lucide-solid', () => ({
  Check: () => null,
  Clipboard: () => null,
  ExternalLink: () => null,
  Loader2: () => null,
  X: () => null,
}))

vi.mock('../../services/auth/oauth', () => ({
  pollDeviceCodeAuth: (...args: unknown[]) => pollDeviceCodeAuthMock(...args),
  storeOAuthCredentials: (...args: unknown[]) => storeOAuthCredentialsMock(...args),
}))

import { DeviceCodeDialog } from './DeviceCodeDialog'

async function flush(): Promise<void> {
  await Promise.resolve()
  await Promise.resolve()
}

describe('DeviceCodeDialog', () => {
  let container: HTMLDivElement
  let dispose: (() => void) | undefined

  beforeEach(() => {
    vi.clearAllMocks()
    container = document.createElement('div')
    document.body.appendChild(container)
  })

  afterEach(() => {
    dispose?.()
    dispose = undefined
    document.body.innerHTML = ''
  })

  it('calls onSuccess immediately after storing device-code credentials', async () => {
    pollDeviceCodeAuthMock.mockResolvedValueOnce({
      accessToken: 'oauth-token',
      refreshToken: 'refresh-token',
      expiresAt: 123,
    })
    storeOAuthCredentialsMock.mockResolvedValueOnce(undefined)
    const onSuccess = vi.fn()

    dispose = render(
      () => (
        <DeviceCodeDialog
          provider="copilot"
          deviceCode={{
            deviceCode: 'device-code',
            userCode: 'user-code',
            verificationUri: 'https://github.com/login/device',
            expiresIn: 900,
            interval: 5,
          }}
          onClose={vi.fn()}
          onSuccess={onSuccess}
        />
      ),
      container
    )

    await flush()

    expect(storeOAuthCredentialsMock).toHaveBeenCalledWith('copilot', {
      accessToken: 'oauth-token',
      refreshToken: 'refresh-token',
      expiresAt: 123,
    })
    expect(onSuccess).toHaveBeenCalledTimes(1)
    expect(storeOAuthCredentialsMock.mock.invocationCallOrder[0]).toBeLessThan(
      onSuccess.mock.invocationCallOrder[0] ?? Number.POSITIVE_INFINITY
    )
  })

  it('adds accessible names to icon-only action buttons', () => {
    dispose = render(
      () => (
        <DeviceCodeDialog
          provider="copilot"
          deviceCode={{
            deviceCode: 'device-code',
            userCode: 'user-code',
            verificationUri: 'https://github.com/login/device',
            expiresIn: 900,
            interval: 5,
          }}
          onClose={vi.fn()}
          onSuccess={vi.fn()}
        />
      ),
      container
    )

    expect(container.querySelector('button[aria-label="Close device code dialog"]')).toBeTruthy()
    expect(container.querySelector('button[aria-label="Copy device code"]')).toBeTruthy()
  })
})
