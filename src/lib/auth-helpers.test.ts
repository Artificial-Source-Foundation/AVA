import { beforeEach, describe, expect, it, vi } from 'vitest'

let isTauriRuntime = false
const invokeMock = vi.fn()

vi.mock('@tauri-apps/api/core', () => ({
  invoke: (...args: unknown[]) => invokeMock(...args),
  isTauri: () => isTauriRuntime,
}))

import { removeStoredAuth, setStoredAuth } from './auth-helpers'

describe('auth helpers', () => {
  beforeEach(() => {
    isTauriRuntime = false
    invokeMock.mockReset()
    localStorage.clear()
  })

  it('falls back to localStorage writes when native auth bridge is unavailable', async () => {
    await setStoredAuth('openai', {
      type: 'oauth',
      accessToken: 'oauth-token',
      refreshToken: 'refresh-token',
      expiresAt: 123,
      accountId: 'acct-1',
    })

    expect(localStorage.getItem('ava_cred_ava:openai:oauth_token')).toBe('oauth-token')
    expect(localStorage.getItem('ava_cred_ava:openai:oauth_refresh_token')).toBe('refresh-token')
    expect(localStorage.getItem('ava_cred_ava:openai:oauth_expires_at')).toBe('123')
    expect(localStorage.getItem('ava_cred_ava:openai:account_id')).toBe('acct-1')
    expect(invokeMock).not.toHaveBeenCalled()
  })

  it('uses the native bridge when Tauri is available', async () => {
    isTauriRuntime = true
    invokeMock.mockResolvedValue(undefined)

    await setStoredAuth('openai', {
      type: 'oauth',
      accessToken: 'oauth-token',
    })

    expect(invokeMock).toHaveBeenCalledWith('store_provider_auth', {
      provider: 'openai',
      auth: {
        type: 'oauth',
        accessToken: 'oauth-token',
      },
    })
    expect(localStorage.getItem('ava_cred_ava:openai:oauth_token')).toBeNull()
  })

  it('surfaces native write failures instead of silently falling back', async () => {
    isTauriRuntime = true
    invokeMock.mockRejectedValue(new Error('native store unavailable'))

    await expect(
      setStoredAuth('openai', {
        type: 'oauth',
        accessToken: 'oauth-token',
      })
    ).rejects.toThrow('[auth-bridge:store_provider_auth] native store unavailable')

    expect(localStorage.getItem('ava_cred_ava:openai:oauth_token')).toBeNull()
  })

  it('falls back to localStorage deletes only outside Tauri', async () => {
    localStorage.setItem('ava_cred_ava:openai:oauth_token', 'oauth-token')
    localStorage.setItem('ava_cred_ava:openai:api_key', 'sk-old')

    await removeStoredAuth('openai')

    expect(localStorage.getItem('ava_cred_ava:openai:oauth_token')).toBeNull()
    expect(localStorage.getItem('ava_cred_ava:openai:api_key')).toBeNull()
    expect(invokeMock).not.toHaveBeenCalled()
  })

  it('uses the native delete bridge when Tauri is available', async () => {
    isTauriRuntime = true
    invokeMock.mockResolvedValue(undefined)

    await removeStoredAuth('openai')

    expect(invokeMock).toHaveBeenCalledWith('delete_provider_auth', {
      provider: 'openai',
    })
  })

  it('surfaces native delete failures instead of clearing local state', async () => {
    isTauriRuntime = true
    localStorage.setItem('ava_cred_ava:openai:oauth_token', 'oauth-token')
    invokeMock.mockRejectedValue(new Error('delete failed'))

    await expect(removeStoredAuth('openai')).rejects.toThrow(
      '[auth-bridge:delete_provider_auth] delete failed'
    )

    expect(localStorage.getItem('ava_cred_ava:openai:oauth_token')).toBe('oauth-token')
  })
})
